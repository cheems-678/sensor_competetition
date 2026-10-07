"""Frozen verification contract, without launching a desktop or physical port."""
import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

from desktop_check import PackagingCheck


def sample_snapshot():
    return {"connected": True, "telemetry": {
        "sounds": {f"sound_p2p_{i}": value for i, value in enumerate(PackagingCheck.SAMPLE_SOUNDS, 1)},
        "mq2": {"valid": True}, "ultrasonic": {"valid": True, "distance_mm": 250}}}


class PackagingCheckTests(unittest.TestCase):
    def test_ready_uses_all_five_and_valid_zero_not_legacy_rms(self):
        snapshot = sample_snapshot()
        self.assertTrue(PackagingCheck.sample_ready(snapshot))
        snapshot["telemetry"]["sounds"]["sound_rms_1"] = "--"
        self.assertTrue(PackagingCheck.sample_ready(snapshot))
        for channel in range(1, 6):
            missing = copy.deepcopy(snapshot)
            missing["telemetry"]["sounds"][f"sound_p2p_{channel}"] = "--"
            self.assertFalse(PackagingCheck.sample_ready(missing))
        snapshot["connected"] = False
        self.assertFalse(PackagingCheck.sample_ready(snapshot))

    def test_old_two_rms_cannot_pass(self):
        snapshot = sample_snapshot()
        snapshot["telemetry"]["sounds"] = {"sound_rms_1": "0", "sound_rms_2": "186"}
        self.assertFalse(PackagingCheck.sample_ready(snapshot))

    def test_other_sensors_must_still_be_valid(self):
        for key in ("mq2", "ultrasonic"):
            snapshot = sample_snapshot()
            snapshot["telemetry"][key]["valid"] = False
            self.assertFalse(PackagingCheck.sample_ready(snapshot))
        snapshot = sample_snapshot()
        snapshot["telemetry"]["ultrasonic"]["distance_mm"] = 0
        self.assertFalse(PackagingCheck.sample_ready(snapshot))

    def test_sample_dom_and_memory_storage_save_success(self):
        with tempfile.TemporaryDirectory() as directory:
            check = PackagingCheck(str(Path(directory) / "result.json"), True)
            check.ready.set()
            view = {"visible": True, "title": "主机声音 · MAX4466",
                    "labels": check.SAMPLE_LABELS, "values": check.SAMPLE_SOUNDS, "units": ["ADC计数"] * 5}
            window = Mock()
            window.evaluate_js.side_effect = [None, view]
            check.observe_and_close(window)
            window.destroy.assert_called_once()
            self.assertIsNone(check.error)
            self.assertTrue(check.info["five_channel_dom_verified"])
            check.api_calls = 1
            check.closed = True
            check.inserts = 1
            check.info["demo"] = False
            self.assertTrue(check.save("edgechromium"))
            result = json.loads(check.output.read_text(encoding="utf-8"))
            self.assertFalse(result["physical_serial_opened"])
            self.assertEqual(result["database_path"], ":memory:")

    def test_dom_failure_is_reported_and_window_closed(self):
        check = PackagingCheck("unused.json", True)
        check.ready.set()
        window = Mock()
        window.evaluate_js.side_effect = RuntimeError("missing acoustic page")
        check.observe_and_close(window)
        self.assertEqual(check.error, "missing acoustic page")
        window.destroy.assert_called_once()

    def test_probe_rejects_disk_database_or_physical_serial(self):
        check = PackagingCheck("unused.json", False)
        with self.assertRaises(AssertionError):
            check.database_factory("sensor_data.db")
        with self.assertRaises(AssertionError):
            check.serial_factory(port="COM32")
