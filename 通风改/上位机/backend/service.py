"""Single-owner worker and the small pywebview API surface."""

from __future__ import annotations

import copy
import queue
import threading
import time
from typing import Callable

from .controller import Controller
from .demo import DemoSerial
from .storage import TelemetryDatabase


class ControllerService:
    def __init__(self, *, demo=False, db_path="sensor_data.db", database_factory: Callable = TelemetryDatabase,
                 serial_factory: Callable | None = None, port_provider: Callable | None = None,
                 controller_factory: Callable = Controller, ai_store_factory: Callable | None = None):
        self._demo, self._db_path = bool(demo), ":memory:" if demo else db_path
        self._database_factory = database_factory
        self._serial_factory = DemoSerial if demo else serial_factory
        self._port_provider = (lambda: ["DEMO"]) if demo else port_provider
        self._controller_factory = controller_factory
        self._ai_store_factory = ai_store_factory
        self._commands: queue.Queue[tuple] = queue.Queue()
        self._lock = threading.Lock()
        self._dispatch_lock = threading.Lock()
        self._closing = threading.Event()
        self._ready = threading.Event()
        self._finished = threading.Event()
        self._thread = None
        self._start_error = None
        self._cached = None

    def start(self):
        with self._dispatch_lock:
            if self._closing.is_set():
                raise RuntimeError("controller service is closed")
            if self._thread is None:
                self._thread = threading.Thread(target=self._run, name="LoRa controller", daemon=True)
                self._thread.start()
        if not self._ready.wait(timeout=5):
            raise RuntimeError("controller service startup timed out")
        if self._start_error is not None:
            raise RuntimeError("controller service startup failed") from self._start_error

    def _publish(self, controller: Controller):
        # Reuse the immutable cache when a tick has not changed any observable state.
        if self._cached is not None and self._cached["revision"] == controller.revision:
            return
        snapshot = controller.snapshot()
        with self._lock:
            self._cached = snapshot

    def _run(self):
        database = None
        controller = None
        try:
            # Construction, writes and closure all happen on this same worker.
            database = self._database_factory(self._db_path)
            controller = self._controller_factory(
                database, serial_factory=self._serial_factory, port_provider=self._port_provider,
                demo=self._demo, closing=self._closing.is_set,
                **({"ai_store": self._ai_store_factory()} if self._ai_store_factory else {}))
            controller.refresh_ports()
            self._publish(controller)
            self._ready.set()
            next_tick = time.monotonic() + 0.05
            while not self._closing.is_set():
                try:
                    command = self._commands.get(timeout=max(0.0, next_tick - time.monotonic()))
                except queue.Empty:
                    command = None
                if self._closing.is_set():
                    break
                if command is not None:
                    method, arguments = command
                    try:
                        getattr(controller, method)(*arguments)
                    except Exception as exc:
                        controller._show_notice("操作失败", str(exc))
                if self._closing.is_set():
                    break
                if time.monotonic() >= next_tick:
                    try:
                        controller.tick()
                    except Exception as exc:
                        controller._append_log("控制器错误: " + str(exc))
                        controller.disconnect()
                        controller._show_notice("操作失败", str(exc))
                    next_tick = time.monotonic() + 0.05
                self._publish(controller)
        except Exception as exc:
            self._start_error = exc
        finally:
            self._ready.set()
            if controller is not None:
                try:
                    controller.close()
                    self._publish(controller)
                except Exception as exc:
                    self._start_error = self._start_error or exc
            elif database is not None:
                database.close()
            self._finished.set()

    def submit(self, method: str, *arguments) -> dict:
        if method not in ("refresh_ports", "connect", "disconnect", "read_once", "set_fan", "set_window",
                          "update_warning_settings", "update_ai_settings", "mark_warning_read", "analyze_warning", "set_warning_scenario",
                          "save_ai_config", "test_ai_connection", "monitor_trends"):
            raise ValueError("unsupported desktop command")
        with self._dispatch_lock:
            accepted = (not self._closing.is_set() and self._ready.is_set()
                        and self._start_error is None and not self._finished.is_set())
            if accepted:
                self._commands.put((method, arguments))
        return {"accepted": accepted}

    def get_snapshot(self, after_log_id=0) -> dict:
        try:
            cursor = max(0, int(after_log_id))
        except (ValueError, TypeError, OverflowError):
            cursor = 0
        with self._lock:
            if self._cached is None:
                raise RuntimeError("controller service has not started")
            snapshot = copy.deepcopy({key: value for key, value in self._cached.items() if key != "logs"})
            snapshot["logs"] = [dict(item) for item in self._cached["logs"] if item["id"] > cursor]
            return snapshot

    def close(self):
        with self._dispatch_lock:
            self._closing.set()
            self._commands.put(("stop", ()))  # Wake the worker; it never executes this command.
        thread = self._thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=5)
            if thread.is_alive():
                raise RuntimeError("controller did not stop within its serial timeout")


class DesktopAPI:
    """Read cached snapshots; all mutations are queued to the controller owner."""
    def __init__(self, service: ControllerService):
        self._service = service

    def refresh_ports(self):
        return self._service.submit("refresh_ports")

    def connect(self, port):
        return self._service.submit("connect", port)

    def disconnect(self):
        return self._service.submit("disconnect")

    def read_once(self):
        return self._service.submit("read_once")

    def set_fan(self, channel, duty):
        return self._service.submit("set_fan", channel, duty)

    def set_window(self, action, servo_id=1):
        return self._service.submit("set_window", action, servo_id)

    def get_snapshot(self, after_log_id=0):
        return self._service.get_snapshot(after_log_id)

    def get_ai_settings(self):
        return self._service.get_snapshot()["ai_settings"]

    def update_ai_settings(self, provider, base_url=None, model=None):
        return self._service.submit("update_ai_settings", provider, base_url, model)

    def update_warning_settings(self, enabled, temp_rate, humidity_rate):
        return self._service.submit("update_warning_settings", enabled, temp_rate, humidity_rate)

    def mark_warning_read(self, event_id):
        return self._service.submit("mark_warning_read", event_id)

    def analyze_warning(self, event_id):
        return self._service.submit("analyze_warning", event_id)

    def set_warning_scenario(self, name):
        return self._service.submit("set_warning_scenario", name)

    def save_ai_config(self, provider, base_url, model, mode, api_key="", remember=True):
        return self._service.submit("save_ai_config", provider, base_url, model, mode, api_key, remember)

    def test_ai_connection(self):
        return self._service.submit("test_ai_connection")

    def monitor_trends(self):
        return self._service.submit("monitor_trends")
