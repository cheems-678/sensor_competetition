"""Desktop resource/sizing checks without GUI, database or serial access."""
import importlib.util
import io
import pathlib
import tempfile
import unittest
import urllib.request
from contextlib import redirect_stderr
from unittest.mock import patch

PATH = pathlib.Path(__file__).parents[1] / "main.py"
SPEC = importlib.util.spec_from_file_location("desktop_entry", PATH)
DESKTOP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DESKTOP)


class DesktopEntryTests(unittest.TestCase):
    def test_frozen_database_stays_next_to_exe(self):
        with patch.object(DESKTOP.sys, "frozen", True, create=True), \
                patch.object(DESKTOP.sys, "executable", str(PATH.parent / "portable" / "app.exe")):
            self.assertEqual(pathlib.Path(DESKTOP.database_path()), PATH.parent / "portable" / "sensor_data.db")

    def test_source_database_keeps_original_working_directory(self):
        with patch.object(DESKTOP.sys, "frozen", False, create=True):
            self.assertEqual(DESKTOP.database_path(), "sensor_data.db")

    def test_legacy_cannot_override_isolation_flags(self):
        for flags in (("--legacy", "--demo"), ("--legacy", "--check")):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                DESKTOP.run(flags)
            self.assertEqual(caught.exception.code, 2)

    def test_default_and_scaled_windows_fit_work_area(self):
        for scale in (1, 1.25, 1.5):
            width, height, minimum = DESKTOP.fit_window(1920, 1040, scale)
            self.assertLessEqual(width * scale, 1920)
            self.assertLessEqual(height * scale, 1040)
            self.assertGreaterEqual(width, minimum[0])
            self.assertGreaterEqual(height, minimum[1])
            self.assertEqual(minimum, (880, 640))
        self.assertEqual(DESKTOP.fit_window(1920, 1040), (1120, 800, (880, 640)))

    def test_small_screen_remains_accessible(self):
        width, height, minimum = DESKTOP.fit_window(1024, 768, 1.5)
        self.assertLessEqual(width * 1.5, 1024)
        self.assertLessEqual(height * 1.5, 768)
        self.assertEqual((width, height), minimum)

    def test_static_server_uses_loopback_and_desktop_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            pathlib.Path(directory, "index.html").write_text("offline", encoding="utf-8")
            server = DESKTOP.AssetServer(pathlib.Path(directory))
            try:
                url = server.start(demo=True)
                self.assertIn("127.0.0.1", url)
                self.assertIn("desktop=1&demo=1", url)
                with urllib.request.urlopen(url, timeout=2) as response:
                    self.assertEqual(response.read(), b"offline")
            finally:
                server.close()


if __name__ == "__main__":
    unittest.main()
