"""Archive/restart, deletion isolation and shared measurement warning boundaries."""
import copy
import json
from pathlib import Path
import queue
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parents[1]))
from backend.controller import Controller
from backend.storage import TelemetryDatabase
from backend.trend_archive import FileTrendArchive, MemoryTrendArchive
from backend.warning_engine import WarningEngine
from backend.warning_service import simulated_explanation
from test_controller import Clock, FakeSerial, ack


class PendingWorker:
    def __init__(self):
        self.results = queue.Queue()
        self.cancelled = []
    def submit(self, *args): return True
    def cancel(self, tokens): self.cancelled.extend(tokens)
    def close(self): pass


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.store = MemoryTrendArchive()
        self.controller = Controller(TelemetryDatabase(':memory:'), demo=True, clock=self.clock,
                                     serial_factory=FakeSerial, start_readers=False, archive_store=self.store)
        self.controller.explanations.close()
        self.controller.explanations = PendingWorker()
        self.controller.set_warning_scenario('normal')
        self.controller.monitor_trends()
        token = self.controller._manual_token
        self.controller.explanations.results.put((token, dict(summary='平稳', possible_causes=['环境稳定'],
                                                suggested_checks=['持续观察'], limitations='模拟数据'), None))
        self.controller._drain_explanations()
    def tearDown(self): self.controller.close()

    def test_archive_is_frozen_and_duplicate_click_is_merged(self):
        report = self.controller.manual_trend
        self.controller.archive_manual_trend(report['id'])
        self.controller.archive_manual_trend(report['id'])
        self.assertEqual(len(self.store.records), 1)
        report['analysis']['result']['summary'] = 'later'
        self.assertEqual(self.store.load()[0]['analysis']['result']['summary'], '平稳')
        self.controller.delete_manual_trend(report['id'])
        self.assertIsNone(self.controller.manual_trend)
        self.assertEqual(len(self.controller.archives), 1)

    def test_persistent_restart_and_allowlisted_content(self):
        evidence_dir = Path(__file__).parents[1] / 'build/ui-preview'
        evidence_dir.mkdir(parents=True, exist_ok=True)
        path = tempfile.mkdtemp(prefix='archive-test-', dir=evidence_dir)
        store = FileTrendArchive(path)
        report = copy.deepcopy(self.controller.manual_trend)
        report['api_key'] = 'must-not-save'
        report['analysis']['api_key'] = 'must-not-save'
        report['analysis']['result']['api_key'] = 'must-not-save'
        report['channels']['master_temp']['api_key'] = 'must-not-save'
        saved = store.save(report)
        restarted = Controller(TelemetryDatabase(':memory:'), archive_store=FileTrendArchive(path))
        try:
            self.assertEqual(restarted.snapshot()['warnings']['archives'][0], saved)
            payload = next(Path(path).glob('*.json')).read_text(encoding='utf-8')
            self.assertNotIn('must-not-save', payload)
            self.assertFalse(list(Path(path).glob('*.pending')))
        finally: restarted.close()

    def test_write_failure_keeps_report_unsaved(self):
        with patch.object(self.store, 'save', side_effect=OSError('disk failed')):
            with self.assertRaises(OSError): self.controller.archive_manual_trend(self.controller.manual_trend['id'])
        self.assertNotIn('archive_id', self.controller.manual_trend)
        self.assertEqual(self.controller.archives, [])

    def test_corrupt_archive_is_visible_as_error_and_never_overwritten(self):
        path = Path(tempfile.mkdtemp(prefix='archive-invalid-', dir=Path(__file__).parents[1] / 'build/ui-preview'))
        filename = path / ('a' * 32 + '.json')
        filename.write_text(json.dumps(dict(archive_id='a' * 32, analysis=[], channels={})), encoding='utf-8')
        controller = Controller(TelemetryDatabase(':memory:'), archive_store=FileTrendArchive(path))
        try:
            self.assertTrue(controller.snapshot()['warnings']['archive_error'])
            self.assertTrue(filename.exists())
        finally: controller.close()

    def test_wrong_id_or_pending_report_cannot_archive(self):
        with self.assertRaises(ValueError): self.controller.delete_manual_trend(999)
        self.controller.manual_trend['analysis']['status'] = 'pending'
        with self.assertRaises(ValueError): self.controller.archive_manual_trend(self.controller.manual_trend['id'])

    def test_deleted_manual_task_cannot_restore_result(self):
        self.controller.monitor_trends()
        token = self.controller._manual_token
        self.controller.delete_manual_trend(self.controller.manual_trend['id'])
        self.controller.explanations.results.put((token, {'summary': 'late'}, None))
        self.controller._drain_explanations()
        self.assertIsNone(self.controller.manual_trend)
        self.assertIn(token, self.controller.explanations.cancelled)

    def test_deleted_event_analysis_is_cancelled_and_retrigger_is_possible(self):
        self.controller.warning_engine.observe_distance(50, 'now', 0)
        event_id = self.controller.warning_engine.events[-1]['id']
        self.controller.analyze_warning(event_id)
        token = self.controller._analysis_tokens[event_id]
        self.controller.delete_warning(event_id)
        self.controller.warning_engine.observe_distance(50, 'again', 1)
        replacement = self.controller.warning_engine.events[-1]
        self.assertGreater(replacement['id'], event_id)
        self.controller.explanations.results.put((token, {'summary': 'late'}, None))
        self.controller._drain_explanations()
        self.assertEqual(replacement['analysis']['status'], 'idle')
        self.assertIn(token, self.controller.explanations.cancelled)


class StateTests(unittest.TestCase):
    def test_distance_boundary_unknown_recovery_and_auto_switch(self):
        engine = WarningEngine()
        engine.observe_distance(100, '0', 0); self.assertEqual(engine.events, [])
        engine.observe_distance(50, '1', 1); event = engine.events[0]
        engine.set_enabled(True, '2'); engine.set_enabled(False, '3')
        self.assertEqual(event['status'], 'active')
        engine.observe_distance(49, '4', 4); self.assertEqual(event['status'], 'unavailable')
        engine.observe_distance(50, '5', 5); self.assertEqual(event['status'], 'active')
        for second in range(6, 10): engine.observe_distance(110, str(second), second)
        self.assertEqual(event['status'], 'resolved')
        engine.observe_distance(None, '10', 10); self.assertEqual(event['status'], 'resolved')
        self.assertIn('10 cm', simulated_explanation(event)['summary'])

    def test_fan_confirmed_pwm_is_independent_of_pending_and_disconnect(self):
        clock = Clock()
        controller = Controller(TelemetryDatabase(':memory:'), clock=clock, serial_factory=FakeSerial, start_readers=False)
        try:
            controller.connect('FAKE')
            controller.set_fan(1, 100); controller._handle_frame(ack(controller.flow_id))
            self.assertEqual(controller.snapshot()['fans'][0]['confirmed_duty'], 100)
            controller.set_fan(1, 80)
            self.assertEqual(controller.snapshot()['fans'][0]['confirmed_duty'], 100)
            clock.advance(8); controller.tick()
            self.assertIsNone(controller.snapshot()['fans'][0]['confirmed_duty'])
            controller.set_fan(2, 90); controller._handle_frame(ack(controller.flow_id))
            controller.disconnect()
            self.assertTrue(all(f['confirmed_duty'] is None for f in controller.snapshot()['fans']))
        finally: controller.close()
