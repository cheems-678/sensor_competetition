"""Opt-in frozen-app verification; memory storage and no physical UART access."""
from __future__ import annotations

import json
import sys
import threading
import time
from pathlib import Path

from backend.demo import DemoSerial
from backend.service import ControllerService, DesktopAPI
from backend.storage import TelemetryDatabase


class PackagingCheck:
    SAMPLE_SOUNDS = ["0", "186", "93", "47", "25"]
    SAMPLE_LABELS = [f"声音 {i} · {pin}" for i, pin in
                     enumerate(("PA0", "PA4", "PA6", "PB0", "PA5"), 1)]

    def __init__(self, output: str, sample: bool, *, model=False):
        self.output = Path(output).resolve()
        self.sample = sample
        self.model = model
        self.ready = threading.Event()
        self.info = {"frozen": bool(getattr(sys, "frozen", False)), "physical_serial_opened": False,
                     "database_path": ":memory:", "sample_transport": bool(sample)}
        self.inserts = 0
        self.closed = False
        self.api_calls = 0
        self.error = None

    def database_factory(self, path):
        check = self

        class MemoryDatabase(TelemetryDatabase):
            def insert(self, flow_id, values, raw_frame):
                super().insert(flow_id, values, raw_frame)
                check.inserts += 1

            def close(self):
                super().close()
                check.closed = True

        if path != ":memory:":
            raise AssertionError("packaging verification must use memory SQLite")
        return MemoryDatabase(path)

    def serial_factory(self, *args, **kwargs):
        if not self.sample:
            raise AssertionError("packaging port scan must never open a serial port")
        return DemoSerial()  # Every byte stays in process; never calls pyserial.Serial.

    def service(self):
        return ControllerService(demo=False, db_path=":memory:",
                                 database_factory=self.database_factory,
                                 serial_factory=self.serial_factory)

    def api(self, service):
        check = self

        class VerifiedAPI(DesktopAPI):
            def get_snapshot(self, after_log_id=0):
                snapshot = super().get_snapshot(after_log_id)
                check.api_calls += 1
                check.info.update({"native_api_polls": check.api_calls,
                                   "demo": snapshot["demo"], "ports": snapshot["ports"],
                                   "port": snapshot["port"], "connected": snapshot["connected"],
                                   "warnings": snapshot["warnings"], "ai_settings": snapshot["ai_settings"],
                                   "telemetry": snapshot["telemetry"]})
                if not check.sample or check.sample_ready(snapshot):
                    check.ready.set()
                return snapshot

        return VerifiedAPI(service)

    @classmethod
    def sample_ready(cls, snapshot):
        telemetry = snapshot["telemetry"]
        return (snapshot["connected"]
                and [telemetry["sounds"].get(f"sound_p2p_{i}") for i in range(1, 6)] == cls.SAMPLE_SOUNDS
                and telemetry["mq2"]["valid"] and telemetry["ultrasonic"]["valid"]
                and telemetry["ultrasonic"]["distance_mm"] == 250)

    def prepare_sample(self, service):
        if self.sample:
            ports = service.get_snapshot()["ports"]
            service.submit("connect", "COM32" if "COM32" in ports else ports[0] if ports else "PACKAGING")

    def observe_and_close(self, window):
        try:
            if not self.ready.wait(timeout=20):
                raise RuntimeError("native frontend did not complete its Python bridge handshake/sample")
            if self.sample:
                # Navigation only: no device controls, configuration writes, or network requests.
                window.evaluate_js("document.querySelector('[aria-label=\"切换到声学监测\"]').click()")
                deadline = time.monotonic() + 5
                while True:
                    view = window.evaluate_js("""(() => {
                        const page = document.querySelector('[data-page="acoustic"]');
                        return {visible: !!page && !page.hidden,
                            title: document.querySelector('#acoustic-title')?.textContent,
                            labels: Array.from(page?.querySelectorAll('.sound-metric .metric-label') ?? [], e => e.textContent),
                            values: Array.from(page?.querySelectorAll('.sound-value') ?? [], e => e.childNodes[0]?.textContent),
                            units: Array.from(page?.querySelectorAll('.sound-value span') ?? [], e => e.textContent)};
                    })()""")
                    self.info["acoustic_view"] = view
                    if (view and view.get("visible") and view.get("title") == "主机声音 · MAX4466"
                            and view.get("labels") == self.SAMPLE_LABELS
                            and view.get("values") == self.SAMPLE_SOUNDS
                            and view.get("units") == ["ADC计数"] * 5):
                        self.info["five_channel_dom_verified"] = True
                        break
                    if time.monotonic() >= deadline:
                        raise RuntimeError("native MAX4466 five-channel DOM did not match sample")
                    threading.Event().wait(0.1)
                if self.model:
                    self.verify_acoustic_model(window)
        except Exception as exc:
            self.error = str(exc)
        finally:
            window.destroy()

    def await_js(self, window, script, condition, label):
        deadline = time.monotonic() + 5
        while True:
            result = window.evaluate_js(script)
            if condition(result):
                return result
            if time.monotonic() >= deadline:
                raise RuntimeError(f"acoustic model check failed: {label}: {result}")
            threading.Event().wait(0.1)

    def verify_acoustic_model(self, window):
        state_script = """(() => {
            const page = document.querySelector('[data-page="acoustic"]');
            const points = Array.from(page.querySelectorAll('.acoustic-point'));
            const canvas = page.querySelector('.acoustic-canvas');
            return {states: points.map(e => e.dataset.state), pins: points.map(e => e.dataset.pin),
                cards: Array.from(page.querySelectorAll('.acoustic-channel'), e => e.dataset.state),
                positions: points.map(e => e.getAttribute('style')),
                drafts: Array.from(page.querySelectorAll('.acoustic-threshold input'), e => e.value),
                running: page.querySelector('.acoustic-model-stage').dataset.running,
                visible: !page.hidden, canvas_ready: canvas.width > 300 && canvas.height > 200,
                error: page.querySelector('[role="alert"]')?.textContent ?? ''};
        })()"""
        initial = self.await_js(window, state_script, lambda v: v and v["canvas_ready"] and v["states"] == ["unset"] * 5, "initial five markers")
        assert initial["pins"] == ["PA0", "PA4", "PA6", "PB0", "PA5"]
        window.evaluate_js("""(() => {
            const page = document.querySelector('[data-page="acoustic"]');
            const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
            Array.from(page.querySelectorAll('.acoustic-threshold input')).forEach((input, i) => {
                setter.call(input, ['0','185','93','46','24'][i]); input.dispatchEvent(new Event('input', {bubbles:true}));
            });
        })()""")
        self.await_js(window, state_script, lambda v: v["drafts"] == ["0", "185", "93", "46", "24"] and v["states"] == ["unset"] * 5, "drafts do not apply themselves")
        apply_script = "Array.from(document.querySelectorAll('[data-page=\"acoustic\"] button')).find(e => e.textContent === '应用阈值').click()"
        window.evaluate_js(apply_script)
        expected = ["normal", "abnormal", "normal", "abnormal", "abnormal"]
        applied = self.await_js(window, state_script, lambda v: v["states"] == expected and v["cards"] == expected, "threshold point/card parity")
        window.evaluate_js("document.querySelector('.acoustic-canvas').dispatchEvent(new KeyboardEvent('keydown', {key:'ArrowRight', bubbles:true}))")
        rotated = self.await_js(window, state_script, lambda v: v["positions"] != applied["positions"] and v["states"] == expected, "rotation")
        window.evaluate_js("document.querySelector('[aria-label=\"切换到环境监测\"]').click()")
        self.await_js(window, state_script, lambda v: not v["visible"] and v["running"] == "false", "hidden page stops blink")
        window.evaluate_js("document.querySelector('[aria-label=\"切换到声学监测\"]').click()")
        self.await_js(window, state_script, lambda v: v["visible"] and v["states"] == expected and v["positions"] == rotated["positions"], "navigation preserves camera/thresholds")
        window.evaluate_js("""(() => {
            const input = document.querySelector('[aria-label="声音 2 阈值"]');
            Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, '4096');
            input.dispatchEvent(new Event('input', {bubbles:true}));
        })()""")
        self.await_js(window, state_script, lambda v: v["drafts"][1] == "4096", "invalid draft")
        window.evaluate_js(apply_script)
        invalid = self.await_js(window, state_script, lambda v: bool(v["error"]) and v["states"] == expected, "invalid apply keeps old thresholds")
        window.evaluate_js("Array.from(document.querySelectorAll('.connection-bar button')).find(e => e.textContent === '断开连接').click()")
        disconnected = self.await_js(window, state_script, lambda v: v["states"] == ["unavailable"] * 5 and v["cards"] == ["unavailable"] * 5, "disconnect clears red states")
        window.evaluate_js("Array.from(document.querySelectorAll('.connection-bar button')).find(e => e.textContent === '连接设备').click()")
        reconnected = self.await_js(window, state_script, lambda v: v["states"] == expected and v["drafts"] == disconnected["drafts"], "reconnect keeps applied thresholds")
        self.info["acoustic_model"] = {"initial": initial, "applied": applied, "rotated": rotated,
                                       "invalid": invalid, "disconnected": disconnected, "reconnected": reconnected}
        self.info["acoustic_model_verified"] = True

    def save(self, renderer=None, error=None):
        self.info.update({"renderer": renderer, "database_inserts": self.inserts,
                          "database_closed": self.closed, "error": str(error or self.error) if error or self.error else None})
        self.info["passed"] = bool(not self.info["error"] and self.api_calls > 0 and self.closed
                                   and renderer == "edgechromium" and self.info.get("demo") is False
                                   and (not self.sample or (self.inserts > 0 and self.info.get("five_channel_dom_verified")))
                                   and (not self.model or self.info.get("acoustic_model_verified")))
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_text(json.dumps(self.info, ensure_ascii=False, indent=2), encoding="utf-8")
        return self.info["passed"]
