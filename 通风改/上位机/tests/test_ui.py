"""Real Tk layout regressions, isolated from serial ports and on-disk storage."""

import importlib.util
import math
import pathlib
import unittest
from contextlib import contextmanager
from types import SimpleNamespace
from tkinter import font as tkfont
from unittest.mock import Mock, patch


MODULE_PATH = pathlib.Path(__file__).parents[1] / "legacy_tk.py"
SPEC = importlib.util.spec_from_file_location("upper_ui", MODULE_PATH)
UPPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPPER)


def descendants(widget):
    for child in widget.winfo_children():
        yield child
        yield from descendants(child)


@contextmanager
def isolated_app(scale=1.0):
    configure_ui = UPPER.MonitorApp._configure_ui

    def configure_with_scaling(app):
        app.tk.call("tk", "scaling", (96 / 72) * scale)
        configure_ui(app)

    serial_stub = SimpleNamespace(
        Serial=Mock(side_effect=AssertionError("UI tests must not open serial ports")))
    with patch.object(UPPER, "TelemetryDatabase") as database, \
            patch.object(UPPER, "serial", serial_stub), \
            patch.object(UPPER.MonitorApp, "_configure_ui", configure_with_scaling), \
            patch.object(UPPER.MonitorApp, "_refresh_ports"), \
            patch.object(UPPER.MonitorApp, "after"), \
            patch.object(UPPER.MonitorApp, "_send") as send:
        app = UPPER.MonitorApp()
        try:
            app.update()
            yield app
            serial_stub.Serial.assert_not_called()
            send.assert_not_called()
            database.return_value.insert.assert_not_called()
        finally:
            app.destroy()


class MonitorLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            root = UPPER.tk.Tk()
            root.withdraw()
            root.destroy()
        except UPPER.tk.TclError as exc:
            raise unittest.SkipTest(f"Tk display is unavailable: {exc}")

    def assert_dashboard_fits_horizontally(self, app):
        content = app.dashboard_content
        left = content.winfo_rootx()
        right = left + content.winfo_width()
        for widget in descendants(content):
            if not widget.winfo_ismapped():
                continue
            with self.subTest(widget=str(widget), widget_class=widget.winfo_class()):
                self.assertGreaterEqual(widget.winfo_rootx(), left)
                self.assertLessEqual(widget.winfo_rootx() + widget.winfo_width(), right)
                if isinstance(widget, (UPPER.ttk.Label, UPPER.ttk.Button,
                                       UPPER.ttk.Spinbox)):
                    self.assertGreaterEqual(widget.winfo_width(), widget.winfo_reqwidth())
                    self.assertGreaterEqual(widget.winfo_height(), widget.winfo_reqheight())
                if isinstance(widget, UPPER.ttk.Label):
                    wrap = int(widget.cget("wraplength") or 0)
                    if wrap:
                        text_variable = str(widget.cget("textvariable"))
                        text = (str(app.tk.globalgetvar(text_variable)) if text_variable
                                else str(widget.cget("text")))
                        font_spec = (widget.cget("font") or UPPER.ttk.Style(app).lookup(
                            widget.cget("style") or "TLabel", "font"))
                        font = tkfont.Font(root=app, font=font_spec)
                        minimum_lines = sum(max(1, math.ceil(font.measure(line) / wrap))
                                            for line in text.split("\n"))
                        self.assertGreaterEqual(widget.winfo_height(),
                                                minimum_lines * font.metrics("linespace"))

    def test_default_and_minimum_windows_at_three_font_scales(self):
        for scale in (1.0, 1.25, 1.5):
            with self.subTest(scale=scale), isolated_app(scale) as app:
                self.assertAlmostEqual(app._ui_scale, scale, delta=0.02)
                for width, height in ((1120, 800), (880, 640)):
                    with self.subTest(size=(width, height)):
                        app.geometry(f"{width}x{height}")
                        app.update()
                        self.assert_dashboard_fits_horizontally(app)
                        self.assertEqual(app.data_panel.grid_info()["column"], 0)
                        wide_enough = app.dashboard_canvas.winfo_width() >= 1040 * app._ui_scale
                        self.assertEqual(app.control_panel.grid_info()["column"], int(wide_enough))
                        self.assertEqual(app.control_panel.grid_info()["row"], 0 if wide_enough else 1)
                        self.assertTrue(app.log.winfo_ismapped())
                        self.assertTrue(app.telemetry_button.winfo_ismapped())
                        if app.dashboard_content.winfo_height() > app.dashboard_canvas.winfo_height():
                            app.dashboard_canvas.yview_moveto(1.0)
                            app.update()
                            self.assertGreaterEqual(app.dashboard_canvas.yview()[1], 0.999)
                            self.assertLessEqual(
                                max(widget.winfo_rooty() + widget.winfo_height()
                                    for widget in descendants(app.dashboard_content)),
                                app.dashboard_canvas.winfo_rooty() + app.dashboard_canvas.winfo_height())
                            app.dashboard_canvas.yview_moveto(0.0)
                            app.update()
                        log_font = tkfont.Font(root=app, font=app.log.cget("font"))
                        log_padding = 2 * app.log.winfo_pixels(app.log.cget("pady"))
                        self.assertGreaterEqual(app.log.winfo_height(),
                                                2 * log_font.metrics("linespace") + log_padding)
                        self.assertLessEqual(app.log.winfo_rooty() + app.log.winfo_height(),
                                             app.winfo_rooty() + app.winfo_height())
                app.dashboard_canvas.yview_moveto(1.0)
                app.update()
                self.assertGreater(app.dashboard_canvas.yview()[0], 0.0)
                self.assertGreaterEqual(app.dashboard_canvas.yview()[1], 0.999)
                self.assertLessEqual(
                    max(widget.winfo_rooty() + widget.winfo_height()
                        for widget in descendants(app.dashboard_content)),
                    app.dashboard_canvas.winfo_rooty() + app.dashboard_canvas.winfo_height())

    def test_placeholder_zero_long_values_and_status_text_fit(self):
        for scale in (1.0, 1.25, 1.5):
            with self.subTest(scale=scale), isolated_app(scale) as app:
                app.geometry("880x640")
                for name, _, unit in app.FIELD_LABELS:
                    value = "4294967294" if "pressure" in name else "-3276.7" if "temperature" in name else "6553.4"
                    app.value_vars[name].set(f"{value} {unit}")
                app.sound_vars["sound_rms_1"].set("0")
                app.sound_vars["sound_rms_2"].set("4294967294")
                app.rain_var.set("有雨")
                app.window_status_var.set("开窗启动PWM已确认（完成/停止状态未知）")
                app.slave_link_var.set("从机链路：未知（旧布局）")
                for variable in app.fan_status_vars.values():
                    variable.set("100% 确认超时")
                app.update()
                self.assert_dashboard_fits_horizontally(app)
                for variable in (*app.value_vars.values(), *app.sound_vars.values(),
                                 app.rain_var, app.window_status_var, app.slave_link_var):
                    labels = [widget for widget in descendants(app.dashboard_content)
                              if isinstance(widget, UPPER.ttk.Label)
                              and str(widget.cget("textvariable")) == str(variable)]
                    self.assertEqual(len(labels), 1)
                self.assertEqual(app.sound_vars["sound_rms_1"].get(), "0")
                app._disconnect()
                app.update()
                self.assertTrue(all(variable.get() == "--" for variable in app.value_vars.values()))
                self.assertTrue(all(variable.get() == "--" for variable in app.sound_vars.values()))
                self.assertEqual(app.rain_var.get(), "--")
                self.assert_dashboard_fits_horizontally(app)

    def test_control_callbacks_keep_channels_actions_and_release_behavior(self):
        with isolated_app() as app:
            app._set_fan = Mock()
            app._set_window = Mock()
            scales = [widget for widget in descendants(app.control_panel)
                      if isinstance(widget, UPPER.ttk.Scale)]
            self.assertEqual(len(scales), 4)
            for channel in app.FAN_PINS:
                slider = next(widget for widget in scales
                              if str(widget.cget("variable")) == str(app.duty_vars[channel]))
                app.tk.call(slider.cget("command"), "67.7")
                self.assertEqual(app.duty_vars[channel].get(), 68)
                app._set_fan.assert_not_called()
                slider.focus_force()
                app.update()
                for event in ("<ButtonRelease-1>", "<KeyRelease-Left>", "<KeyRelease-Right>"):
                    slider.event_generate(event)
                    app.update()
                    app._set_fan.assert_called_once_with(channel)
                    app._set_fan.reset_mock()
                app.fan_send_buttons[channel].invoke()
                app._set_fan.assert_called_once_with(channel)
                app._set_fan.reset_mock()
            self.assertTrue(all(button.instate(("disabled",))
                                for button in app.window_buttons.values()))
            for action, button in app.window_buttons.items():
                button.state(("!disabled",))
                button.invoke()
                app._set_window.assert_called_once_with(action)
                app._set_window.reset_mock()

    def test_log_stays_read_only_and_scrolls_to_newest_frame(self):
        with isolated_app() as app:
            for number in range(100):
                app._append_log(f"RX frame {number}")
            app.update()
            self.assertEqual(str(app.log.cget("state")), UPPER.tk.DISABLED)
            self.assertIn("RX frame 99", app.log.get("1.0", UPPER.tk.END))
            self.assertGreater(app.log.yview()[0], 0.0)
            self.assertIsNotNone(app.log.dlineinfo("100.0"))
            # Exercise the Text's actual callback to identify the linked scrollbar.
            callback = str(app.log.cget("yscrollcommand"))
            self.assertTrue(callback)
            app.tk.call(callback, 0.25, 0.5)
            matching = [widget for widget in descendants(app)
                        if isinstance(widget, UPPER.ttk.Scrollbar)
                        and widget.get() == (0.25, 0.5)]
            self.assertEqual(len(matching), 1)
            app.log.yview_moveto(0)
            app.update()
            self.assertLess(app.log.yview()[0], 0.01)

    def test_dashboard_wheel_does_not_also_scroll_while_editing_fan_spinbox(self):
        with isolated_app() as app:
            app.geometry("880x640")
            app.update()
            canvas = app.dashboard_canvas
            label = next(widget for widget in descendants(app.data_panel)
                         if isinstance(widget, UPPER.ttk.Label))
            label.event_generate("<MouseWheel>", delta=-120)
            app.update()
            self.assertGreater(canvas.yview()[0], 0)
            spinner = next(widget for widget in descendants(app.control_panel)
                           if isinstance(widget, UPPER.ttk.Spinbox)
                           and str(widget.cget("textvariable")) == str(app.duty_vars[1]))
            app.duty_vars[1].set(50)
            position = canvas.yview()
            spinner.event_generate("<MouseWheel>", delta=-120)
            app.update()
            self.assertEqual(app.duty_vars[1].get(), 49)
            self.assertEqual(canvas.yview(), position)


if __name__ == "__main__":
    unittest.main()
