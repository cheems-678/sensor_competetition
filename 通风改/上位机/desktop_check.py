"""Opt-in frozen-app verification; memory storage and no physical UART access."""
from __future__ import annotations

import json
import sys
import threading
from pathlib import Path

from backend.demo import DemoSerial
from backend.service import ControllerService, DesktopAPI
from backend.storage import TelemetryDatabase


class PackagingCheck:
    def __init__(self, output: str, sample: bool):
        self.output = Path(output).resolve()
        self.sample = sample
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
                                   "telemetry": snapshot["telemetry"]})
                if not check.sample or snapshot["telemetry"]["sounds"]["sound_rms_1"] == "0":
                    check.ready.set()
                return snapshot

        return VerifiedAPI(service)

    def prepare_sample(self, service):
        if self.sample:
            ports = service.get_snapshot()["ports"]
            service.submit("connect", "COM32" if "COM32" in ports else ports[0] if ports else "PACKAGING")

    def observe_and_close(self, window):
        if not self.ready.wait(timeout=20):
            self.error = "native frontend did not complete its Python bridge handshake"
        window.destroy()

    def save(self, renderer=None, error=None):
        self.info.update({"renderer": renderer, "database_inserts": self.inserts,
                          "database_closed": self.closed, "error": str(error or self.error) if error or self.error else None})
        self.info["passed"] = bool(not self.info["error"] and self.api_calls > 0 and self.closed
                                   and renderer == "edgechromium" and self.info.get("demo") is False
                                   and (not self.sample or self.inserts > 0))
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_text(json.dumps(self.info, ensure_ascii=False, indent=2), encoding="utf-8")
        return self.info["passed"]
