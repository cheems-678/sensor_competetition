"""Manual reports from real decoding with virtual clocks, fake UART/HTTP and memory SQLite."""
import json
import pathlib
import struct
import sys
import threading
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from backend.warning_engine import WarningEngine
from backend.controller import Controller
from backend.service import DesktopAPI
from backend.storage import TelemetryDatabase
from backend.warning_service import ExplanationWorker
from backend.ai_client import ChatAPIClient
from test_controller import Clock, FakeSerial, response, telemetry_payload
from test_ai_api import FakeOpener, ANSWER, FAKE_KEY
from backend.protocol import LoRaProtocol as P
from unittest.mock import Mock, patch


class ManualEngineTests(unittest.TestCase):
    def fill(self, engine, rate=0, humidity_rate=0, length=120):
        for second in range(length + 1):
            engine.observe(second + 1, second, str(second), dict(master_temp=24 + rate*second/60,
                           master_humidity=50 + humidity_rate*second/60, slave_temp=24.2, slave_humidity=51))

    def test_two_minute_boundary_and_event_evidence(self):
        engine = WarningEngine()
        self.fill(engine, rate=0.6, length=119)
        report = engine.inspect(119, "before", True)
        self.assertEqual(report["status"], "insufficient")
        self.assertIsNone(engine.metrics["master_temp"]["rate"])
        self.assertFalse(engine.events)
        self.fill(engine, rate=0.6, length=120)
        report = engine.inspect(120, "ready", True)
        self.assertEqual(report["status"], "abnormal")
        self.assertEqual(report["channels"]["master_temp"]["span_seconds"], 120)
        self.assertEqual(engine.events[0]["evidence"]["window_seconds"], 120)
        self.assertEqual(engine.snapshot()["window_seconds"], 120)

    def test_disabled_collects_and_manual_fires_without_automatic_confirmation(self):
        engine = WarningEngine()
        self.fill(engine, rate=0.6, humidity_rate=2)
        self.assertFalse(engine.enabled)
        self.assertFalse(engine.events)
        report = engine.inspect(120, "check", True)
        self.assertEqual(report["status"], "abnormal")
        self.assertEqual(len(engine.events), 2)
        self.assertTrue(all(e["trigger"] == "manual" for e in engine.events))
        self.assertEqual(engine.inspect(120, "again", True)["event_ids"], report["event_ids"])
        self.assertEqual(len(engine.events), 2)

    def test_normal_and_falling_have_results_without_events(self):
        for rate, direction in ((0, "stable"), (-0.2, "falling")):
            engine = WarningEngine()
            self.fill(engine, rate=rate)
            report = engine.inspect(120, "check", True)
            self.assertEqual(report["status"], "normal")
            self.assertEqual(report["channels"]["master_temp"]["direction"], direction)
            self.assertFalse(engine.events)

    def test_short_window_zero_reading_invalid_partial_and_gap_are_not_normal(self):
        engine = WarningEngine()
        self.fill(engine, rate=1, length=10)
        report = engine.inspect(10, "check", True)
        self.assertEqual(report["status"], "insufficient")
        self.assertAlmostEqual(report["channels"]["master_temp"]["rate"], 1)
        self.assertFalse(engine.events)
        self.assertEqual(engine.inspect(21, "gap", True)["status"], "unavailable")
        engine.reset_window()
        self.fill(engine)
        engine.observe(122, 121, "invalid", dict(master_temp=None, master_humidity=50, slave_temp=24.2, slave_humidity=51))
        report = engine.inspect(121, "check", True)
        self.assertEqual(report["status"], "partial")
        self.assertIsNone(report["channels"]["master_temp"]["current"])
        self.assertEqual(engine.inspect(121, "offline", False)["status"], "unavailable")
        zero = WarningEngine()
        zero.observe(1, 0, "0", {"master_temp": 0})
        self.assertEqual(zero.inspect(0, "check", True)["channels"]["master_temp"]["current"], 0)

    def test_switch_and_threshold_keep_history_manual_recovery_and_disconnect_stop(self):
        engine = WarningEngine()
        self.fill(engine, rate=0.6)
        engine.inspect(120, "manual", True)
        event = engine.events[0]
        engine.set_enabled(True, "on"); engine.set_enabled(False, "off")
        self.assertEqual(event["status"], "active")
        self.assertEqual(engine.metrics["master_temp"]["span_seconds"], 120)
        # Keep the window valid and eventually flat; recovery remains operational with automatic mode off.
        for second in range(121, 800):
            engine.observe(second + 1, second, str(second), dict(master_temp=25.2, master_humidity=50, slave_temp=24.2, slave_humidity=51))
        self.assertEqual(event["status"], "resolved")
        count = len(engine.series["master_temp"])
        engine.set_rates(0.2, 1, "settings")
        self.assertEqual(len(engine.series["master_temp"]), count)
        engine.stop("disconnect")
        self.assertEqual(engine.inspect(800, "check", False)["status"], "unavailable")


class ManualControllerTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.controller = Controller(TelemetryDatabase(":memory:"), clock=self.clock,
            serial_factory=FakeSerial, port_provider=lambda:["MOCK"], start_readers=False)
        self.transport = FakeOpener()
        self.controller.explanations = ExplanationWorker(client=ChatAPIClient(self.transport))
        self.controller.save_ai_config("deepseek", "https://api.deepseek.com", "deepseek-flash", "api", FAKE_KEY, False)
        self.controller.connect("MOCK")

    def tearDown(self):
        self.controller.close()

    def fill(self, rising=False, length=120):
        for second in range(length + 1):
            self.clock.now = 100 + second
            self.controller.read_once()
            flow = self.controller.telemetry_pending[0]
            payload = bytearray(telemetry_payload())
            struct.pack_into("<h", payload, 1, round(240 + (second / 10 if rising else 0)))
            self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, payload))

    def drain(self):
        item = self.controller.explanations.results.get(timeout=2)
        self.controller.explanations.results.put(item)
        self.controller._drain_explanations()

    def test_real_decoded_data_checks_normal_and_abnormal_with_auto_disabled(self):
        for rising in (False, True):
            if rising:
                self.controller.disconnect(); self.controller.connect("MOCK")
            self.fill(rising)
            self.assertFalse(self.controller.warning_engine.enabled)
            self.assertFalse([e for e in self.controller.warning_engine.events if e["status"]=="active"])
            writes = list(self.controller.serial_port.writes)
            rows = self.controller.database.connection.execute("SELECT count(*) FROM telemetry_v4").fetchone()[0]
            self.controller.monitor_trends(); self.drain()
            report = self.controller.snapshot()["warnings"]["manual"]
            self.assertEqual(report["status"], "abnormal" if rising else "normal")
            self.assertEqual(report["analysis"]["result"], ANSWER)
            self.assertEqual(report["analysis"]["data_source"], "telemetry")
            self.assertEqual(self.controller.serial_port.writes, writes)
            self.assertEqual(self.controller.database.connection.execute("SELECT count(*) FROM telemetry_v4").fetchone()[0], rows)
            body = json.loads(self.transport.requests[-1][0].data)
            context = json.loads(body["messages"][1]["content"])
            self.assertEqual(context["event"]["purpose"], "manual_trend_review")
            self.assertNotIn(FAKE_KEY, str(report))
            self.assertNotIn("raw_frame", str(context))
            self.assertAlmostEqual(report["channels"]["slave_temp"]["current"], 24.2)

    def test_no_data_and_timeout_do_not_call_api_short_data_can_be_explained(self):
        self.controller.monitor_trends()
        self.assertEqual(self.controller.manual_trend["status"], "unavailable")
        self.assertFalse(self.transport.requests)
        self.fill(rising=True, length=10)
        self.controller.monitor_trends(); self.drain()
        self.assertEqual(self.controller.manual_trend["status"], "insufficient")
        self.assertFalse(self.controller.warning_engine.events)
        self.controller._clear_telemetry()
        self.controller.monitor_trends()
        self.assertEqual(self.controller.manual_trend["status"], "unavailable")
        self.assertEqual(len(self.transport.requests), 1)

    def test_api_failure_and_missing_key_preserve_local_warning_and_report(self):
        self.fill(rising=True)
        self.controller.explanations = ExplanationWorker(client=ChatAPIClient(FakeOpener(error=TimeoutError())))
        self.controller.monitor_trends(); self.drain()
        self.assertEqual(self.controller.manual_trend["analysis"]["status"], "error")
        self.assertEqual(self.controller.warning_engine.events[-1]["status"], "active")
        self.controller._ai_credentials.clear()
        self.controller.monitor_trends()
        self.assertIn("API Key", self.controller.manual_trend["analysis"]["result"]["error"])
        self.assertEqual(len(self.controller.warning_engine.events), 1)

    def test_duplicate_clicks_merge_and_rules_provider_or_reconnect_discard_old_result(self):
        self.fill(rising=True)
        for mutate in (lambda:self.controller.update_ai_settings("kimi"),
                       lambda:self.controller.update_warning_settings(False,0.5,1),
                       lambda:(self.controller.disconnect(), self.controller.connect("MOCK"))):
            self.controller.explanations.close()
            entered, release = threading.Event(), threading.Event()
            class Blocking:
                def explain(self, *args):
                    entered.set(); release.wait(2); return ANSWER
            self.controller.explanations = ExplanationWorker(client=Blocking())
            self.controller.update_ai_settings("deepseek")
            self.controller.monitor_trends()
            self.assertTrue(entered.wait(1))
            token = self.controller._manual_token
            self.controller.monitor_trends()
            self.assertEqual(self.controller._manual_token, token)
            mutate()
            release.set(); self.drain()
            self.assertTrue(self.controller.manual_trend["stale"])
            self.assertEqual(self.controller.manual_trend["analysis"]["status"], "stale")

    def test_bridge_only_queues_owner_command(self):
        service = Mock()
        service.submit.return_value = {"accepted":True}
        self.assertEqual(DesktopAPI(service).monitor_trends(), {"accepted":True})
        service.submit.assert_called_once_with("monitor_trends")


if __name__ == "__main__":
    unittest.main()
