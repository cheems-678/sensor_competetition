"""Warning behaviour, asynchronous isolation and forward-only replay; no hardware/network."""
import copy
import pathlib
import queue
import sqlite3
import struct
import sys
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from backend.warning_engine import WarningEngine
from backend.warning_service import ExplanationWorker
from backend.warning_replay import evaluate_synthetic, run_scenario, replay_database, SCENARIOS
from backend.controller import Controller
from backend.storage import TelemetryDatabase
from backend.protocol import LoRaProtocol as P
from backend.windows_platform import bounded_wmi_query
from types import SimpleNamespace
from test_controller import Clock, FakeSerial, response, telemetry_payload


class EngineTests(unittest.TestCase):
    def sample(self, engine, second, rate=0.6, sample_id=None):
        engine.observe(second + 1 if sample_id is None else sample_id, second, str(second),
                       {"master_temp": 24 + rate * second / 60, "slave_temp": 24,
                        "master_humidity": 50, "slave_humidity": 51})

    def test_full_window_and_confirmation_then_one_event(self):
        engine = WarningEngine(True)
        for second in range(150):
            self.sample(engine, second)
        self.assertEqual(engine.events, [])
        self.sample(engine, 150)
        self.assertEqual(len(engine.events), 1)
        self.assertAlmostEqual(engine.events[0]["evidence"]["rate"], 0.6)
        for second in range(151, 450):
            self.sample(engine, second)
        self.assertEqual(len(engine.events), 1)

    def test_disabled_default_and_sample_deduplication(self):
        engine = WarningEngine()
        for second in range(400):
            self.sample(engine, second)
        self.assertFalse(engine.events)
        before = list(engine.series["master_temp"])
        engine.set_enabled(True, "enable")
        self.assertEqual(list(engine.series["master_temp"]), before)
        self.sample(engine, 400, sample_id=400)
        self.assertEqual(list(engine.series["master_temp"]), before)
        self.sample(engine, 400)
        self.assertEqual(engine.series["master_temp"][-1][0], 400)
        self.assertFalse(engine.events)  # Automatic confirmation starts only after enabling.

    def test_invalid_gap_does_not_restore_active_warning(self):
        engine = WarningEngine(True)
        for second in range(331):
            self.sample(engine, second)
        engine.observe(332, 331, "331", {"master_temp": None})
        self.assertEqual(engine.events[0]["status"], "unavailable")
        engine.expire(342)
        self.assertEqual(len(engine.series["master_temp"]), 0)
        self.sample(engine, 343)
        self.assertIsNone(engine.metrics["master_temp"]["rate"])
        self.assertEqual(engine.events[0]["status"], "unavailable")

    def test_hysteresis_requires_sixty_seconds_and_stops_are_distinct(self):
        engine = WarningEngine(True)
        run_scenario(engine, "warming")
        # A flat series below recovery threshold must persist a full minute.
        engine.reset_window()
        for second in range(120):
            self.sample(engine, second, rate=0)
        self.sample(engine, 120, rate=0)
        self.sample(engine, 129, rate=0)
        for second in range(130, 180):
            self.sample(engine, second, rate=0)
        self.assertEqual(engine.events[0]["status"], "active")
        self.sample(engine, 180, rate=0)
        self.assertEqual(engine.events[0]["status"], "resolved")
        run_scenario(engine, "warming")
        engine.stop("disconnect")
        self.assertEqual(engine.events[-1]["status"], "stopped")

    def test_zero_nan_and_wall_clock_strings(self):
        engine = WarningEngine(True)
        engine.observe(1, 0, "2099", {"master_temp": 0})
        engine.observe(2, 1, "1900", {"master_temp": float("nan")})
        self.assertEqual(list(engine.series["master_temp"]), [(0, 0)])
        for bad in (0, -1, float("inf"), True):
            with self.assertRaises(ValueError):
                engine.set_rates(bad, 1, "x")

    def test_all_scenarios_and_read_do_not_change_status(self):
        for name in SCENARIOS:
            engine = WarningEngine(True)
            run_scenario(engine, name)
            if name in ("normal", "spike", "missing"):
                self.assertFalse(engine.events, name)
            if name in ("warming", "humidity"):
                self.assertEqual(engine.events[-1]["status"], "active")
            if name in ("timeout", "recovery"):
                self.assertEqual(engine.events[-1]["status"], "resolved")
            if name == "reconnect":
                self.assertEqual(engine.events[-1]["status"], "stopped")

    def test_event_limit_retains_active_events(self):
        engine = WarningEngine(True)
        engine.connected = True
        engine.communication_timeout("x", 0)
        active_id = engine.events[0]["id"]
        for index in range(205):
            event = engine._create("master_temp", "master", "temp", "x", index, {})
            event["status"] = "resolved"
        self.assertEqual(len(engine.events), 200)
        self.assertIn(active_id, [e["id"] for e in engine.events])
        snapshot = engine.snapshot()
        snapshot["events"][0]["status"] = "injected"
        self.assertNotEqual(engine.events[-1]["status"], "injected")


class ControllerWarningTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.db = TelemetryDatabase(":memory:")
        self.controller = Controller(self.db, clock=self.clock, serial_factory=FakeSerial,
                                     port_provider=lambda: ["MOCK"], start_readers=False)
        self.controller.ai_mode = "simulation"  # Explicit offline baseline; API cases use injected transport.

    def tearDown(self):
        self.controller.close()

    def event(self):
        self.controller.warning_engine.set_enabled(True, "x")
        run_scenario(self.controller.warning_engine, "warming")
        return self.controller.warning_engine.events[-1]

    def test_accepted_protocol_sources_and_no_implicit_device_commands(self):
        self.controller.update_warning_settings(True, 0.4, 1)
        self.controller.connect("MOCK")
        self.controller.read_once()
        flow = self.controller.telemetry_pending[0]
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow + 1, telemetry_payload()))
        self.assertFalse(self.controller.warning_engine.series["master_temp"])
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, telemetry_payload(slave_temp=260)))
        self.assertEqual(self.controller.warning_engine.series["master_temp"][-1][1], 23.6)
        self.assertEqual(self.controller.warning_engine.series["slave_temp"][-1][1], 26)
        self.assertEqual(len(self.controller.serial_port.writes), 1)
        self.assertEqual(P.parse_packet(self.controller.serial_port.writes[0])["type"], P.MSG_READ_TELEMETRY)

    def test_timeout_recovery_and_intentional_disconnect(self):
        self.controller.connect("MOCK")
        self.controller.read_once()
        self.clock.advance(5)
        self.controller.tick()
        event = self.controller.warning_engine.events[-1]
        self.assertEqual(event["kind"], "communication")
        self.controller.read_once()
        flow = self.controller.telemetry_pending[0]
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, telemetry_payload()))
        self.assertEqual(event["status"], "resolved")
        count = len(self.controller.warning_engine.events)
        self.controller.disconnect()
        self.controller.tick()
        self.assertEqual(len(self.controller.warning_engine.events), count)

    def test_settings_independent_no_keys_and_real_scenarios_rejected(self):
        self.controller.update_ai_settings("kimi", "https://api.moonshot.cn/v1", "kimi-test")
        self.controller.update_ai_settings("deepseek", "https://api.deepseek.com", "deepseek-flash")
        self.assertEqual(self.controller.ai_settings["profiles"]["kimi"]["model"], "kimi-test")
        self.controller.update_ai_settings("kimi")
        self.assertEqual(self.controller.ai_settings["profiles"]["kimi"]["model"], "kimi-test")
        with self.assertRaises(ValueError):
            self.controller.update_ai_settings("custom", "https://secret@host/path", "x")
        with self.assertRaises(ValueError):
            self.controller.set_warning_scenario("warming")
        self.assertFalse(self.controller.warning_engine.enabled)
        self.assertNotIn("api_key", str(self.controller.snapshot()))

    def test_async_duplicate_merge_stale_results_and_failure(self):
        event = self.event()
        entered, release = threading.Event(), threading.Event()
        def explain(value):
            entered.set()
            release.wait(2)
            return dict(summary="test", limitations="offline", possible_causes=[], suggested_checks=[])
        self.controller.explanations = ExplanationWorker(explain)
        self.controller.analyze_warning(event["id"])
        self.assertTrue(entered.wait(1))
        token = self.controller._analysis_tokens[event["id"]]
        self.controller.analyze_warning(event["id"])
        self.assertEqual(token, self.controller._analysis_tokens[event["id"]])
        self.controller.mark_warning_read(event["id"])
        self.assertEqual(event["status"], "active")
        self.controller.update_ai_settings("kimi", "https://api.moonshot.cn/v1", "kimi-k3")
        release.set()
        self.controller.explanations.thread.join(0.15)
        self.controller._drain_explanations()
        self.assertEqual(event["analysis"]["status"], "stale")
        self.controller.explanations.close()
        self.controller.explanations = ExplanationWorker(lambda value: {"bad": True})
        self.controller.analyze_warning(event["id"])
        token, result, error = self.controller.explanations.results.get(timeout=1)
        self.controller.explanations.results.put((token, result, error))
        self.controller._drain_explanations()
        self.assertEqual(event["analysis"]["status"], "error")
        self.assertEqual(event["status"], "active")

    def test_reconnect_invalidates_and_complete_analysis_is_offline(self):
        event = self.event()
        self.controller.analyze_warning(event["id"])
        token, result, error = self.controller.explanations.results.get(timeout=1)
        self.controller.explanations.results.put((token, result, error))
        self.controller._drain_explanations()
        self.assertEqual(event["analysis"]["status"], "complete")
        self.assertIn("未调用 API", event["analysis"]["result"]["limitations"])
        self.controller.connect("MOCK")
        self.assertEqual(event["analysis"]["status"], "stale")
        self.assertFalse(self.controller.serial_port.writes)

    def test_demo_scenarios_do_not_send_commands_or_write_database(self):
        self.controller.demo = True
        self.controller.update_warning_settings(True, 0.4, 1)
        self.controller.connect("MOCK")
        generation = self.controller.connection_generation
        with patch("socket.socket", side_effect=AssertionError("network forbidden")):
            for name in SCENARIOS:
                self.controller.set_warning_scenario(name)
        self.assertEqual(generation, self.controller.connection_generation)
        self.assertFalse(self.controller.serial_port.writes)
        self.assertEqual(self.db.connection.execute("SELECT count(*) FROM telemetry_v4").fetchone()[0], 0)


class ReplayTests(unittest.TestCase):
    def test_labelled_report_lead_and_spike_false_alarm(self):
        report = evaluate_synthetic()
        warming = next(s for s in report["scenarios"] if s["scenario"] == "warming")
        self.assertEqual(warming["lead_seconds"], [450])
        self.assertEqual(warming["trend"]["missed_events"], 0)
        spike = next(s for s in report["scenarios"] if s["scenario"] == "spike")
        self.assertEqual(spike["fixed"]["false_events"], 1)
        self.assertEqual(spike["trend"]["false_events"], 0)

    def test_history_uses_readonly_uri_redecodes_wire_slots(self):
        memory = sqlite3.connect(":memory:")
        memory.execute("CREATE TABLE telemetry_v4(id INTEGER, timestamp TEXT, raw_frame TEXT)")
        for second in range(401):
            # Master occupies legacy slave slot; column names cannot be trusted.
            payload = telemetry_payload()
            payload = bytearray(payload)
            struct.pack_into("<h", payload, 1, round(240 + second / 10))
            frame = response(P.MSG_TELEMETRY, second, payload)
            from datetime import datetime, timedelta
            stamp = (datetime(2026, 1, 1) + timedelta(seconds=second)).isoformat()
            memory.execute("INSERT INTO telemetry_v4 VALUES(?,?,?)", (second, stamp, frame.hex()))
        memory.commit()
        source = pathlib.Path(__file__).parents[1] / "README.md"
        with patch("backend.warning_replay.sqlite3.connect", return_value=memory) as connect:
            report = replay_database(source)
        self.assertIn("?mode=ro", connect.call_args.args[0])
        self.assertTrue(connect.call_args.kwargs["uri"])
        self.assertEqual(report["events"][0]["source"], "master")
        self.assertEqual(report["source"], "unlabelled_history")
        memory.close()


class PlatformQueryTests(unittest.TestCase):
    def test_preserves_query_values_and_actual_exception(self):
        owner = SimpleNamespace(_wmi=object())
        query = bounded_wmi_query(lambda *args: iter(("AMD64", "Windows")), owner, 0.1)
        self.assertEqual(query("CPU"), ("AMD64", "Windows"))
        error = ValueError("original metadata failure")
        def failed(*args):
            raise error
        with self.assertRaises(ValueError) as captured:
            bounded_wmi_query(failed, owner, 0.1)("CPU")
        self.assertIs(captured.exception, error)

    def test_timeout_returns_control_for_native_fallback(self):
        owner = SimpleNamespace(_wmi=object())
        release = threading.Event()
        def stalled(*args):
            release.wait(1)
            return ("late",)
        try:
            start = time.monotonic()
            with self.assertRaisesRegex(OSError, "timed out"):
                bounded_wmi_query(stalled, owner, 0.02)("OS")
            self.assertLess(time.monotonic() - start, 0.5)
            self.assertIsNone(owner._wmi)
        finally:
            release.set()


if __name__ == "__main__":
    unittest.main()
