"""Sensor warnings with virtual time, fake serial and fake HTTP only."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parents[1]))
from backend.ai_client import ChatAPIClient
from backend.controller import Controller
from backend.protocol import LoRaProtocol as P
from backend.smoke_index import smoke_index
from backend.storage import TelemetryDatabase
from backend.trend_archive import FileTrendArchive, archive_record
from backend.warning_engine import WarningEngine
from backend.warning_service import ExplanationWorker, simulated_explanation
from test_ai_api import FakeOpener, FAKE_KEY
from test_controller import Clock, FakeSerial, response, telemetry_payload


def packet(rain=0, mv=550, distance=100, online=True, age=0):
    base = telemetry_payload(flags=0x3F if online else 0x3B, rain=rain)
    mq = struct.pack('<4H', 1000, mv, mv * 2, age) if mv is not None else struct.pack('<4H', *([65535] * 4))
    dist = struct.pack('<4H', distance, distance, 583, age) if distance is not None else struct.pack('<4H', *([65535] * 4))
    return base + mq + dist


class SensorEngineTests(unittest.TestCase):
    def test_rain_transitions_deduplicate_and_preserve_trigger(self):
        engine = WarningEngine()
        engine.observe_rain(0, 'dry', 0)
        self.assertEqual(engine.sensors['rain']['state'], 'normal')
        self.assertEqual(engine.events, [])
        for t in range(1, 25): engine.observe_rain(1, str(t), t)
        self.assertEqual(len(engine.events), 1)
        event = engine.events[0]
        self.assertEqual((event['source'], event['kind'], event['occurred_at']), ('master', 'rain', '1'))
        engine.observe_rain(0, 'dry-again', 25)
        self.assertEqual((event['status'], event['ended_at']), ('resolved', 'dry-again'))
        self.assertEqual((event['evidence']['trigger_value'], event['evidence']['current']), (1, 0))
        engine.observe_rain(1, 'new-rain', 26)
        self.assertEqual(len(engine.events), 2)

    def test_unknown_is_not_normal_or_recovery(self):
        engine = WarningEngine()
        observers = (engine.observe_rain, engine.observe_smoke, engine.observe_distance)
        for observe, value in zip(observers, (1, 11, 99)): observe(value, 'trigger', 0)
        for observe in observers:
            for value in (None, True, float('nan')): observe(value, 'invalid', 1)
        self.assertTrue(all(s['state'] == 'unavailable' and s['current'] is None for s in engine.sensors.values()))
        self.assertTrue(all(e['status'] == 'unavailable' and e['ended_at'] is None for e in engine.events))
        self.assertEqual(engine.inspect_sensors()['status'], 'unavailable')
        for observe, value in zip(observers, (1, 11, 99)): observe(value, 'valid', 2)
        self.assertEqual(len(engine.events), 3)
        self.assertTrue(all(e['status'] == 'active' for e in engine.events))

    def test_smoke_exact_limit_immediate_recovery_and_deduplication(self):
        engine = WarningEngine()
        for value in (0, 9, 10): engine.observe_smoke(value, 'normal', 0)
        self.assertFalse(engine.events)
        for t in range(1, 20): engine.observe_smoke(11 + t, str(t), t, pa7_mv=850, raw=1000)
        self.assertEqual(len(engine.events), 1)
        event = engine.events[0]
        self.assertEqual(event['evidence']['threshold'], 10)
        self.assertEqual(event['evidence']['unit'], '相对指数')
        engine.observe_smoke(10, 'restore', 21)
        self.assertEqual(event['status'], 'resolved')
        self.assertEqual(engine.sensors['smoke']['state'], 'normal')
        engine.observe_smoke(11, 'new', 22)
        self.assertEqual(len(engine.events), 2)

    def test_smoke_voltage_mapping_matches_existing_rounded_index(self):
        for mv, index in ((0, 0), (350, 0), (550, 10), (559, 10), (560, 11), (850, 25), (1500, 60), (2500, 100), (3600, 100)):
            self.assertEqual(smoke_index(mv), index)
        for mv in (None, True, -1, 3601, float('nan'), float('inf')): self.assertIsNone(smoke_index(mv))

    def test_distance_strict_boundary_hysteresis_and_three_seconds(self):
        engine = WarningEngine()
        engine.observe_distance(100, 'boundary', 0)
        self.assertFalse(engine.events)
        engine.observe_distance(99, 'near', 1)
        event = engine.events[0]
        for t, distance in enumerate((101, 99, 100, 105, 109, 100), 2): engine.observe_distance(distance, str(t), t)
        self.assertEqual(len(engine.events), 1)
        self.assertEqual(event['status'], 'active')
        for t in range(8, 11):
            engine.observe_distance(110, str(t), t)
            self.assertEqual(event['status'], 'active')
        engine.observe_distance(110, 'recovered', 11)
        self.assertEqual(event['status'], 'resolved')
        self.assertEqual((event['evidence']['current'], event['evidence']['trigger_value'], event['evidence']['threshold']), (11, 9.9, 10))
        engine.observe_distance(99, 'new-near', 12)
        self.assertEqual(len(engine.events), 2)

    def test_distance_invalid_gap_or_dip_interrupts_recovery(self):
        for interruption in (None, 49, 501, 109, 'gap'):
            with self.subTest(interruption=interruption):
                engine = WarningEngine()
                engine.observe_distance(50, 'near', 0)
                engine.observe_distance(110, 'safe', 1)
                if interruption != 'gap': engine.observe_distance(interruption, 'break', 2)
                for t in range(4, 7):
                    engine.observe_distance(110, str(t), t)
                    self.assertEqual(engine.events[0]['status'], 'active')
                engine.observe_distance(110, 'restored', 7)
                self.assertEqual(engine.events[0]['status'], 'resolved')

    def test_temperature_switch_rules_and_window_are_unchanged(self):
        engine = WarningEngine()
        for t in range(121): engine.observe(t, t, str(t), dict(master_temp=24 + t * .01, master_humidity=50, slave_temp=24, slave_humidity=50))
        before = copy.deepcopy((engine.series, engine.metrics, engine.rates))
        engine.observe_rain(1, 'rain', 120); engine.observe_smoke(11, 'smoke', 120); engine.observe_distance(99, 'near', 120)
        self.assertEqual((engine.series, engine.metrics, engine.rates), before)
        engine.set_enabled(True, 'enabled'); engine.set_enabled(False, 'disabled')
        self.assertEqual([e['status'] for e in engine.events], ['active'] * 3)
        self.assertEqual((engine.WINDOW, engine.CONFIRM, engine.RECOVER, engine.GAP, engine.rates), (120, 30, 60, 10, {'temp': .4, 'humidity': 1}))
        engine.stop('disconnect')
        self.assertTrue(all(e['status'] == 'stopped' for e in engine.events))

    def test_sensor_history_limit_and_snapshot_isolation(self):
        engine = WarningEngine()
        for t in range(220):
            engine.observe_rain(1, str(t), t); engine.observe_rain(0, str(t), t)
        self.assertEqual(len(engine.events), 200)
        snapshot = engine.snapshot(); snapshot['sensors']['rain']['state'] = 'abnormal'
        self.assertEqual(engine.sensors['rain']['state'], 'normal')


class SensorControllerTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.controller = Controller(TelemetryDatabase(':memory:'), clock=self.clock, serial_factory=FakeSerial, start_readers=False)
        self.controller.connect('FAKE')
    def tearDown(self): self.controller.close()
    def sample(self, payload):
        self.controller.read_once()
        flow = self.controller.telemetry_pending[0]
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, payload))
        return flow

    def test_accepted_telemetry_creates_all_warnings_without_control_or_network(self):
        with patch('socket.socket', side_effect=AssertionError('No network')):
            flow = self.sample(packet(1, 560, 99))
            self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, packet(1, 560, 99)))
            self.assertEqual(len(self.controller.warning_engine.events), 3)
            self.assertEqual(self.controller.sample_id, 1)
            self.assertFalse(self.controller.warning_engine.enabled)
            self.assertTrue(all(f[3] == P.MSG_READ_TELEMETRY for f in self.controller.serial_port.writes))
            self.assertEqual(self.controller._analysis_tokens, {})

    def test_offline_slave_does_not_hide_master_rain_and_old_frames_are_unknown(self):
        self.sample(packet(1, 850, 99, online=False))
        state = self.controller.snapshot()['warnings']['sensors']
        self.assertEqual(state['rain']['state'], 'abnormal')
        self.assertEqual(state['smoke']['state'], 'unavailable')
        self.assertEqual(state['distance']['state'], 'unavailable')
        self.sample(telemetry_payload(flags=0x07))
        self.assertTrue(all(s['state'] == 'unavailable' for s in self.controller.snapshot()['warnings']['sensors'].values()))
        self.assertEqual(self.controller.warning_engine.events[0]['status'], 'unavailable')

    def test_missing_sensor_sentinels_and_expiry_never_resolve(self):
        self.sample(packet(1, 850, 99))
        self.clock.advance(2); self.controller.tick()
        events = {e['kind']: e for e in self.controller.warning_engine.events}
        self.assertEqual(events['smoke']['status'], 'unavailable')
        self.assertEqual(events['distance']['status'], 'unavailable')
        self.sample(packet(255, None, None))
        self.assertTrue(all(e['status'] == 'unavailable' for e in events.values()))
        self.assertTrue(all(s['current'] is None for s in self.controller.snapshot()['warnings']['sensors'].values()))

    def test_timeout_disconnect_reconnect_and_recovery_preserve_history(self):
        self.sample(packet(1, 850, 99))
        self.controller.read_once(); self.clock.advance(5); self.controller.tick()
        self.assertTrue(all(e['status'] == 'unavailable' for e in self.controller.warning_engine.events if e['kind'] != 'communication'))
        self.controller.disconnect()
        self.assertTrue(all(e['status'] == 'stopped' for e in self.controller.warning_engine.events))
        self.controller.connect('FAKE'); self.sample(packet(0, 550, 110))
        self.assertTrue(all(s['state'] == 'normal' for s in self.controller.snapshot()['warnings']['sensors'].values()))
        self.assertEqual(len(self.controller.warning_engine.events), 4)

    def test_manual_sensor_evidence_ai_and_archives_are_frozen_and_backwards_compatible(self):
        transport = FakeOpener()
        self.controller.explanations.close()
        self.controller.explanations = ExplanationWorker(client=ChatAPIClient(transport))
        self.controller.save_ai_config('deepseek', 'https://api.deepseek.com', 'deepseek-flash', 'api', FAKE_KEY, False)
        with patch('socket.socket', side_effect=AssertionError('No network')):
            self.sample(packet(1, 560, 99))
            writes = list(self.controller.serial_port.writes)
            self.controller.monitor_trends(); self.controller.monitor_trends()
            item = self.controller.explanations.results.get(timeout=2)
            self.controller.explanations.results.put(item); self.controller._drain_explanations()
            report = copy.deepcopy(self.controller.manual_trend)
            self.assertEqual(len(transport.requests), 1)
            evidence = json.loads(json.loads(transport.requests[0][0].data)['messages'][1]['content'])['event']['evidence']
            self.assertEqual(evidence['sensor_review']['status'], 'abnormal')
            self.assertEqual(len(evidence['event_ids']), 3)
            self.assertEqual(self.controller.serial_port.writes, writes)
            self.sample(packet(0, 550, 110))
            self.assertEqual(self.controller.manual_trend['sensor_review'], report['sensor_review'])
        path = tempfile.mkdtemp(prefix='sensor-archive-', dir=Path(__file__).parents[1] / 'build/ui-preview')
        store = FileTrendArchive(path)
        report['sensor_review']['readings']['smoke']['api_key'] = 'must-not-save'
        saved = store.save(report)
        self.assertEqual(FileTrendArchive(path).load()[0], saved)
        self.assertNotIn('must-not-save', json.dumps(saved))
        self.assertNotIn(FAKE_KEY, json.dumps(saved))
        legacy = copy.deepcopy(report); legacy.pop('sensor_review')
        self.assertNotIn('sensor_review', archive_record(legacy))

    def test_new_event_types_support_read_delete_and_simulated_analysis(self):
        self.sample(packet(1, 850, 99))
        self.controller.ai_mode = 'simulation'
        for event in list(self.controller.warning_engine.events):
            self.assertTrue(simulated_explanation(event)['summary'])
            self.controller.mark_warning_read(event['id'])
            self.assertEqual(event['status'], 'active')
            self.controller.analyze_warning(event['id'])
            item = self.controller.explanations.results.get(timeout=2)
            self.controller.explanations.results.put(item); self.controller._drain_explanations()
            self.assertEqual(event['analysis']['status'], 'complete')
            self.controller.delete_warning(event['id'])
        self.assertFalse(self.controller.warning_engine.events)
        self.clock.advance(1); self.sample(packet(1, 850, 99))
        self.assertEqual(len(self.controller.warning_engine.events), 3)
