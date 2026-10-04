import importlib.util
import pathlib
import queue
import struct
import unittest
from types import MethodType, SimpleNamespace
from unittest.mock import Mock, patch


MODULE_PATH = pathlib.Path(__file__).parents[1] / "legacy_tk.py"
SPEC = importlib.util.spec_from_file_location("upper_v4", MODULE_PATH)
UPPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPPER)


class ProtocolV4Tests(unittest.TestCase):
    def test_all_fan_channels_and_duty_boundaries(self):
        for channel in (1, 2):
            for duty in (0, 1, 25, 50, 75, 99, 100):
                packet = UPPER.LoRaProtocol.parse_packet(
                    UPPER.LoRaProtocol.cmd_set_fan_speed(5, channel, duty))
                self.assertEqual(packet["data"], bytes((channel, duty)))
        for channel, duty in ((0, 50), (3, 0), (3, 100), (4, 0), (4, 100), (5, 50), (1, -1), (1, 101)):
            with self.assertRaises(ValueError):
                UPPER.LoRaProtocol.cmd_set_fan_speed(1, channel, duty)

    def test_invalid_fan_is_rejected_when_parsing(self):
        for channel in (0, 3, 4, 5):
            frame = UPPER.LoRaProtocol.build_packet(0x10, 1, 0, 2, 1, 1, bytes((channel, 100)))
            with self.assertRaisesRegex(ValueError, "payload"):
                UPPER.LoRaProtocol.parse_packet(frame)

    def test_read_telemetry_frame_layout(self):
        frame = UPPER.LoRaProtocol.cmd_read_telemetry(0x1234, True)
        self.assertEqual(frame[:11], bytes.fromhex("AA 55 04 01 01 00 02 01 34 12 01"))
        self.assertEqual(frame[11], 1)
        self.assertEqual(len(frame), 14)
        parsed = UPPER.LoRaProtocol.parse_packet(frame)
        self.assertEqual(parsed["flow_id"], 0x1234)

    def test_fan_command_frame_layout(self):
        frame = UPPER.LoRaProtocol.cmd_set_fan_speed(7, 2, 75)
        self.assertEqual(frame[3], UPPER.LoRaProtocol.MSG_SET_FAN_SPEED)
        self.assertEqual(frame[8:10], b"\x07\x00")
        self.assertEqual(frame[10:13], b"\x02\x02\x4B")
        UPPER.LoRaProtocol.parse_packet(frame)

    def test_placeholder_payload_exact_bytes(self):
        payload = UPPER.LoRaProtocol.placeholder_payload()
        self.assertEqual(len(payload), 18)
        self.assertEqual(
            payload.hex(" ").upper(),
            "00 00 80 FF FF FF FF FF FF FF FF FF FF FF 00 80 FF FF",
        )
        decoded = UPPER.LoRaProtocol.decode_telemetry(payload)
        self.assertEqual(decoded["flags"], 0)
        for key, value in decoded.items():
            if key != "flags":
                self.assertIsNone(value, key)

    def test_master_telemetry_frame_is_31_bytes(self):
        frame = UPPER.LoRaProtocol.build_packet(
            UPPER.LoRaProtocol.MSG_TELEMETRY,
            UPPER.LoRaProtocol.ROLE_MASTER,
            1,
            UPPER.LoRaProtocol.ROLE_CONTROL,
            0,
            0xABCD,
            UPPER.LoRaProtocol.placeholder_payload(),
        )
        self.assertEqual(len(frame), 31)
        parsed = UPPER.LoRaProtocol.parse_packet(frame)
        self.assertEqual(parsed["data"], UPPER.LoRaProtocol.placeholder_payload())

    def test_stream_parser_handles_noise_and_split_frame(self):
        frame = UPPER.LoRaProtocol.cmd_read_telemetry(3)
        parser = UPPER.FrameStreamParser()
        self.assertEqual(parser.feed(b"noise" + frame[:8]), [])
        self.assertEqual(parser.feed(frame[8:]), [frame])

    def test_crc_error_is_rejected(self):
        frame = bytearray(UPPER.LoRaProtocol.cmd_read_telemetry(1))
        frame[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "CRC"):
            UPPER.LoRaProtocol.parse_packet(bytes(frame))


class ValueStub:
    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value

    def set(self, value):
        self.value = value


def bind_window_controls(app):
    app.window_queued_action = None
    app.window_pending = None
    app.window_status_var = ValueStub("未发送")
    app.window_buttons = {action: Mock() for action in (0, 1)}
    app.fan_send_buttons = {channel: Mock() for channel in (1, 2)}
    app.telemetry_button = Mock()
    app.WINDOW_ACK_TIMEOUT_S = UPPER.MonitorApp.WINDOW_ACK_TIMEOUT_S
    app._window_action_text = UPPER.MonitorApp._window_action_text
    for name in ("_window_busy", "_sync_window_buttons", "_set_window", "_finish_window",
                 "_expire_window", "_service_window"):
        setattr(app, name, MethodType(getattr(UPPER.MonitorApp, name), app))


class FanControlTests(unittest.TestCase):
    def setUp(self):
        self.app = SimpleNamespace(
            serial_port=Mock(),
            duty_vars={channel: ValueStub(25.6) for channel in (1, 2)},
            fan_status_vars={channel: ValueStub("未发送") for channel in (1, 2)},
            fan_pending={},
            telemetry_pending=None,
            _normalize_fan_duty=UPPER.MonitorApp._normalize_fan_duty,
            _next_flow=Mock(side_effect=range(1, 100)),
            _send=Mock(return_value=True),
            _append_log=Mock(),
        )
        bind_window_controls(self.app)

    def test_decimal_slider_rounding(self):
        self.assertEqual(self.app._normalize_fan_duty("25.6"), 26)
        self.assertEqual(self.app._normalize_fan_duty(0), 0)
        self.assertEqual(self.app._normalize_fan_duty(100), 100)
        for value in ("", "abc", float("nan"), float("inf"), -1, 101):
            with self.assertRaises(ValueError):
                self.app._normalize_fan_duty(value)

    def test_send_and_matching_ack(self):
        UPPER.MonitorApp._set_fan(self.app, 2)
        frame = self.app._send.call_args.args[0]
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(frame)["data"], bytes((2, 26)))
        self.assertEqual(self.app.fan_status_vars[2].get(), "等待确认 26%")
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x10\x00")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertEqual(self.app.fan_status_vars[2].get(), "已确认 26%")
        self.assertFalse(self.app.fan_pending)

    def test_active_fan_mapping_and_invalid_channel(self):
        self.assertEqual(UPPER.MonitorApp.FAN_PINS, {1: "PB1", 2: "PB8"})
        self.assertEqual(tuple(UPPER.MonitorApp.FAN_PINS), UPPER.LoRaProtocol.FAN_CHANNELS)
        for channel in (0, 3, 4, 5):
            with patch.object(UPPER.messagebox, "showwarning"):
                UPPER.MonitorApp._set_fan(self.app, channel)
        self.app._send.assert_not_called()
        self.assertFalse(self.app.fan_pending)

    def test_each_active_slider_keeps_its_channel_number(self):
        for channel in (1, 2):
            previous = {key: value.get() for key, value in self.app.duty_vars.items()}
            UPPER.MonitorApp._preview_fan(self.app, channel, "100")
            UPPER.MonitorApp._set_fan(self.app, channel)
            frame = self.app._send.call_args.args[0]
            self.assertEqual(UPPER.LoRaProtocol.parse_packet(frame)["data"], bytes((channel, 100)))
            for other in previous:
                if other != channel:
                    self.assertEqual(self.app.duty_vars[other].get(), previous[other])

    def test_rejected_ack_is_not_success(self):
        UPPER.MonitorApp._set_fan(self.app, 2)
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x10\x01")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertEqual(self.app.fan_status_vars[2].get(), "主机拒绝执行")
        self.assertFalse(self.app.fan_pending)

    def test_older_ack_cannot_confirm_new_setting(self):
        UPPER.MonitorApp._set_fan(self.app, 1)
        self.app.duty_vars[1].set(70)
        UPPER.MonitorApp._set_fan(self.app, 1)
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x10\x00")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertEqual(self.app.fan_status_vars[1].get(), "等待确认 70%")
        self.assertEqual(list(self.app.fan_pending), [2])

    def test_invalid_input_and_disconnected_send(self):
        self.app.duty_vars[1].set(101)
        with patch.object(UPPER.messagebox, "showwarning"):
            UPPER.MonitorApp._set_fan(self.app, 1)
        self.app._send.assert_not_called()
        self.app.duty_vars[1].set(50)
        self.app._send.return_value = False
        UPPER.MonitorApp._set_fan(self.app, 1)
        self.assertFalse(self.app.fan_pending)

    def test_wrong_command_ack_and_error(self):
        UPPER.MonitorApp._set_fan(self.app, 2)
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x01\x00")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertIn(1, self.app.fan_pending)
        error = UPPER.LoRaProtocol.build_packet(0x7E, 1, 0, 2, 1, 1, b"\x0A")
        UPPER.MonitorApp._handle_frame(self.app, error)
        self.assertEqual(self.app.fan_status_vars[2].get(), "失败 code=10")
        self.assertFalse(self.app.fan_pending)


def master_bme_payload(flags=1, temperature=-55, humidity=654, pressure=101325):
    return struct.pack("<BhHIHHBhH", flags, temperature, humidity, pressure,
                       0xFFFF, 0xFFFF, 0xFF, -32768, 0xFFFF)


def telemetry_frame(flow=1, payload=None):
    return UPPER.LoRaProtocol.build_packet(
        2, 2, 1, 1, 0, flow, master_bme_payload() if payload is None else payload)


class MasterBmeProtocolTests(unittest.TestCase):
    def test_exact_wire_bytes_and_negative_temperature(self):
        payload = master_bme_payload()
        self.assertEqual(payload[:9], bytes.fromhex("01 C9 FF 8E 02 CD 8B 01 00"))
        self.assertEqual(payload[9:], bytes.fromhex("FF FF FF FF FF 00 80 FF FF"))
        values = UPPER.LoRaProtocol.decode_telemetry(payload)
        self.assertEqual(values["master_bme_temperature_c"], -5.5)
        self.assertEqual(values["master_bme_humidity_pct"], 65.4)
        self.assertEqual(values["master_bme_pressure_pa"], 101325)
        frame = telemetry_frame(payload=payload)
        self.assertEqual(len(frame), 31)
        self.assertEqual(frame[10], 18)
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(frame)["data"], payload)

    def test_invalid_bme_and_legacy_source_are_not_master_measurements(self):
        invalid = master_bme_payload(temperature=-32768, humidity=0xFFFF, pressure=0xFFFFFFFF)
        for payload in (invalid, master_bme_payload(flags=0)):
            values = UPPER.LoRaProtocol.decode_telemetry(payload)
            for key in ("master_bme_temperature_c", "master_bme_humidity_pct", "master_bme_pressure_pa"):
                self.assertIsNone(values[key])
        with self.assertRaisesRegex(ValueError, "layout"):
            UPPER.LoRaProtocol.decode_telemetry(master_bme_payload(flags=2))
        legacy = UPPER.LoRaProtocol.decode_telemetry(master_bme_payload(flags=0))
        self.assertEqual(legacy["slave_temperature_c"], -5.5)

    def test_database_keeps_legacy_columns_and_source_flag(self):
        database = UPPER.TelemetryDatabase(":memory:")
        try:
            columns = [row[1] for row in database.connection.execute("PRAGMA table_info(telemetry_v4)")]
            self.assertEqual(columns, ["id", "timestamp", "flow_id", "flags", "slave_temperature_c",
                                      "slave_humidity_pct", "slave_pressure_pa", "sound_rms_1",
                                      "sound_rms_2", "rain_state", "master_temperature_c",
                                      "master_humidity_pct", "raw_frame"])
            for flags in (0, 1):
                frame = telemetry_frame(flow=flags, payload=master_bme_payload(flags=flags))
                values = UPPER.LoRaProtocol.decode_telemetry(UPPER.LoRaProtocol.parse_packet(frame)["data"])
                database.insert(flags, values, frame)
            rows = database.connection.execute(
                "SELECT flags, slave_temperature_c, slave_humidity_pct, slave_pressure_pa FROM telemetry_v4 ORDER BY id"
            ).fetchall()
            self.assertEqual(rows, [(0, -5.5, 65.4, 101325), (1, -5.5, 65.4, 101325)])
        finally:
            database.close()


class TelemetryPollingTests(unittest.TestCase):
    def setUp(self):
        clock_patch = patch.object(UPPER.time, "monotonic", return_value=100.0)
        clock_patch.start()
        self.addCleanup(clock_patch.stop)
        self.app = SimpleNamespace(
            serial_port=Mock(), fan_pending={}, telemetry_pending=None,
            next_telemetry_poll_at=100.0,
            TELEMETRY_POLL_INTERVAL_S=1.0, TELEMETRY_TIMEOUT_S=5.0,
            FIELD_LABELS=UPPER.MonitorApp.FIELD_LABELS,
            SOUND_LABELS=UPPER.MonitorApp.SOUND_LABELS,
            SOUND_SECTION_TITLE=UPPER.MonitorApp.SOUND_SECTION_TITLE,
            SOUND_HELP_TEXT=UPPER.MonitorApp.SOUND_HELP_TEXT,
            value_vars={name: ValueStub("old") for name, _, _ in UPPER.MonitorApp.FIELD_LABELS},
            sound_vars={name: ValueStub("old") for name, _ in UPPER.MonitorApp.SOUND_LABELS},
            slave_link_var=ValueStub("old"),
            last_telemetry_at=99.0,
            fan_status_vars={channel: ValueStub("ready") for channel in (1, 2)},
            _next_flow=Mock(side_effect=range(1, 100)), _send=Mock(return_value=True),
            _append_log=Mock(), database=Mock(), stop_event=Mock(), connect_button=Mock(),
        )
        for name in ("_request_telemetry", "_poll_telemetry", "_clear_telemetry", "_handle_frame", "_disconnect"):
            setattr(self.app, name, MethodType(getattr(UPPER.MonitorApp, name), self.app))
        bind_window_controls(self.app)

    def test_automatic_poll_is_cached_and_never_duplicates_pending_request(self):
        self.app._poll_telemetry(99.9)
        self.app._send.assert_not_called()
        with patch.object(UPPER.time, "monotonic", return_value=100.0):
            self.app._poll_telemetry(100.0)
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(self.app._send.call_args.args[0])["data"], b"\x00")
        self.assertEqual(self.app.telemetry_pending, (1, 100.0))
        self.assertEqual(self.app.next_telemetry_poll_at, 101.0)
        self.app._poll_telemetry(101.0)
        self.assertEqual(self.app._send.call_count, 1)
        self.app._handle_frame(telemetry_frame(flow=1))
        self.assertIsNone(self.app.telemetry_pending)
        self.assertEqual(self.app.value_vars["master_bme_pressure_pa"].get(), "101325 Pa")
        with patch.object(UPPER.time, "monotonic", return_value=101.0):
            self.app._poll_telemetry(101.0)
        self.assertEqual(self.app._send.call_count, 2)

    def test_fan_pending_and_disconnection_pause_polling(self):
        self.app.fan_pending[7] = (1, 50, 99.0)
        self.app._poll_telemetry(100.0)
        self.app._send.assert_not_called()
        self.app.fan_pending.clear()
        self.app.serial_port = None
        self.app._poll_telemetry(100.0)
        self.app._send.assert_not_called()

    def test_timeout_clears_stale_values_and_backs_off(self):
        self.app.telemetry_pending = (1, 100.0)
        self.app._poll_telemetry(104.9)
        self.assertIsNotNone(self.app.telemetry_pending)
        self.app._poll_telemetry(105.0)
        self.assertIsNone(self.app.telemetry_pending)
        self.assertEqual(self.app.next_telemetry_poll_at, 106.0)
        self.assertTrue(all(value.get() == "--" for value in self.app.value_vars.values()))
        self.app._poll_telemetry(105.9)
        self.app._send.assert_not_called()

    def test_manual_force_sample_and_failed_send(self):
        with patch.object(UPPER.time, "monotonic", return_value=100.0):
            self.app._request_telemetry()
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(self.app._send.call_args.args[0])["data"], b"\x01")
        self.app._request_telemetry()
        self.assertEqual(self.app._send.call_count, 1)
        self.app.telemetry_pending = None
        self.app._send.return_value = False
        self.app._request_telemetry()
        self.assertIsNone(self.app.telemetry_pending)

    def test_only_matching_valid_telemetry_or_error_completes_poll(self):
        self.app.telemetry_pending = (1, 100.0)
        self.app._handle_frame(telemetry_frame(flow=2))
        self.assertIsNotNone(self.app.telemetry_pending)
        bad = bytearray(telemetry_frame(flow=1))
        bad[-1] ^= 1
        self.app._handle_frame(bytes(bad))
        self.assertIsNotNone(self.app.telemetry_pending)
        self.app._handle_frame(UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x10\x00"))
        self.assertIsNotNone(self.app.telemetry_pending)
        with patch.object(UPPER.time, "monotonic", return_value=102.0):
            self.app._handle_frame(UPPER.LoRaProtocol.build_packet(0x7E, 1, 0, 2, 1, 1, b"\x0A"))
        self.assertIsNone(self.app.telemetry_pending)
        self.assertEqual(self.app.next_telemetry_poll_at, 103.0)
        self.assertTrue(all(value.get() == "--" for value in self.app.value_vars.values()))

    def test_legacy_data_is_not_shown_as_master_bme(self):
        self.app.telemetry_pending = (1, 100.0)
        self.app._handle_frame(telemetry_frame(payload=master_bme_payload(flags=0)))
        self.assertTrue(all(value.get() == "--" for value in self.app.value_vars.values()))
        self.app.database.insert.assert_called_once()

    def test_disconnect_clears_pending_data_and_never_starts_fans(self):
        port = self.app.serial_port
        self.app.telemetry_pending = (1, 100.0)
        self.app.fan_pending[2] = (1, 75, 100.0)
        self.app._disconnect()
        port.close.assert_called_once()
        self.assertIsNone(self.app.serial_port)
        self.assertIsNone(self.app.telemetry_pending)
        self.assertFalse(self.app.fan_pending)
        self.app._poll_telemetry(200.0)
        self.app._send.assert_not_called()
        self.assertTrue(all(value.get() == "--" for value in self.app.value_vars.values()))

    def test_dual_bme_disconnection_and_recovery_display(self):
        self.app.telemetry_pending = (1, 100.0)
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload()))
        self.assertEqual(self.app.value_vars["master_bme_pressure_pa"].get(), "101325 Pa")
        self.assertEqual(self.app.value_vars["slave_bme_pressure_pa"].get(), "100000 Pa")
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：在线")
        self.app.telemetry_pending = (1, 100.0)
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload(flags=3)))
        self.assertEqual(self.app.value_vars["master_bme_pressure_pa"].get(), "101325 Pa")
        self.assertEqual(self.app.value_vars["slave_bme_pressure_pa"].get(), "--")
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：离线")
        self.app.telemetry_pending = (1, 100.0)
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload()))
        self.assertEqual(self.app.value_vars["slave_bme_temperature_c"].get(), "-12.3 ℃")
        self.app._clear_telemetry()
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：未知")

    def test_sensor_failure_does_not_mean_link_offline(self):
        self.app.telemetry_pending = (1, 100.0)
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload(
            slave_temperature=-32768, slave_humidity=0xFFFF, slave_pressure=0xFFFFFFFF)))
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：在线")
        self.assertTrue(all(self.app.value_vars[name].get() == "--" for name, _, _
                            in self.app.FIELD_LABELS if name.startswith("slave_bme_")))


def dual_bme_payload(flags=7, slave_temperature=-123, slave_humidity=456, slave_pressure=100000):
    return struct.pack("<BhHIhHIB", flags, -55, 654, 101325,
                       slave_temperature, slave_humidity, slave_pressure, 0xFF)


class DualBmeProtocolTests(unittest.TestCase):
    def test_dual_exact_layout_little_endian_and_sources(self):
        payload = dual_bme_payload()
        self.assertEqual(payload.hex(" ").upper(),
                         "07 C9 FF 8E 02 CD 8B 01 00 85 FF C8 01 A0 86 01 00 FF")
        frame = telemetry_frame(payload=payload)
        self.assertEqual(len(frame), 31)
        decoded = UPPER.LoRaProtocol.decode_telemetry(UPPER.LoRaProtocol.parse_packet(frame)["data"])
        self.assertEqual([decoded[name] for name in (
            "master_bme_temperature_c", "master_bme_humidity_pct", "master_bme_pressure_pa",
            "slave_bme_temperature_c", "slave_bme_humidity_pct", "slave_bme_pressure_pa")],
            [-5.5, 65.4, 101325, -12.3, 45.6, 100000])
        self.assertTrue(decoded["slave_online"])
        for key in ("sound_rms_1", "sound_rms_2", "rain_state", "master_temperature_c", "master_humidity_pct"):
            self.assertIsNone(decoded[key], key)

    def test_offline_and_invalid_values_are_independent(self):
        values = UPPER.LoRaProtocol.decode_telemetry(dual_bme_payload(flags=3))
        self.assertFalse(values["slave_online"])
        self.assertEqual(values["master_bme_pressure_pa"], 101325)
        for key in ("slave_bme_temperature_c", "slave_bme_humidity_pct", "slave_bme_pressure_pa"):
            self.assertIsNone(values[key])
        values = UPPER.LoRaProtocol.decode_telemetry(dual_bme_payload(
            slave_temperature=-32768, slave_humidity=0xFFFF, slave_pressure=0xFFFFFFFF))
        self.assertTrue(values["slave_online"])
        for key in ("slave_bme_temperature_c", "slave_bme_humidity_pct", "slave_bme_pressure_pa"):
            self.assertIsNone(values[key])
        payload = bytearray(dual_bme_payload())
        payload[1:9] = bytes.fromhex("00 80 FF FF FF FF FF FF")
        values = UPPER.LoRaProtocol.decode_telemetry(bytes(payload))
        self.assertIsNone(values["master_bme_temperature_c"])
        self.assertIsNone(values["master_bme_humidity_pct"])
        self.assertIsNone(values["master_bme_pressure_pa"])
        self.assertEqual(values["slave_bme_pressure_pa"], 100000)

    def test_old_and_unknown_flags_do_not_claim_slave_bme(self):
        for flags in (0, 1):
            values = UPPER.LoRaProtocol.decode_telemetry(dual_bme_payload(flags=flags))
            self.assertIsNone(values["slave_online"])
            self.assertIsNone(values["slave_bme_temperature_c"])
            self.assertIsNone(values["slave_bme_pressure_pa"])
            if flags != 1:
                self.assertIsNone(values["master_bme_temperature_c"])
        for flags in (2, 4, 5, 6, 8, 11, 15, 0xFF):
            with self.assertRaisesRegex(ValueError, "layout"):
                UPPER.LoRaProtocol.decode_telemetry(dual_bme_payload(flags=flags))

    def test_database_retains_six_values_in_raw_frame_without_schema_change(self):
        database = UPPER.TelemetryDatabase(":memory:")
        try:
            frame = telemetry_frame(payload=dual_bme_payload())
            values = UPPER.LoRaProtocol.decode_telemetry(frame[11:-2])
            database.insert(5, values, frame)
            row = database.connection.execute(
                "SELECT flags, slave_temperature_c, slave_humidity_pct, slave_pressure_pa, "
                "sound_rms_1, sound_rms_2, rain_state, master_temperature_c, master_humidity_pct, raw_frame "
                "FROM telemetry_v4").fetchone()
            self.assertEqual(row[:-1], (7, -5.5, 65.4, 101325, None, None, None, None, None))
            restored = UPPER.LoRaProtocol.decode_telemetry(
                UPPER.LoRaProtocol.parse_packet(bytes.fromhex(row[-1]))["data"])
            self.assertEqual(restored, values)
            self.assertEqual(len(database.connection.execute("PRAGMA table_info(telemetry_v4)").fetchall()), 13)
        finally:
            database.close()


def acoustic_payload(flags=0x0F, left=123456, right=98765):
    return struct.pack("<BhHIhHIBII", flags, -55, 654, 101325,
                       -123, 456, 100000, 0xFF, left, right)


class AcousticProtocolTests(unittest.TestCase):
    def test_extended_exact_layout_and_crc(self):
        payload = acoustic_payload()
        self.assertEqual(len(payload), 26)
        self.assertEqual(payload[:18], dual_bme_payload(flags=0x0F))
        self.assertEqual(payload[18:], bytes.fromhex("40 E2 01 00 CD 81 01 00"))
        frame = telemetry_frame(flow=0x1234, payload=payload)
        self.assertEqual(len(frame), 39)
        self.assertEqual(frame[10], 0x1A)
        packet = UPPER.LoRaProtocol.parse_packet(frame)
        self.assertEqual(packet["flow_id"], 0x1234)
        self.assertEqual(packet["data"], payload)
        damaged = bytearray(frame)
        damaged[36] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            UPPER.LoRaProtocol.parse_packet(bytes(damaged))

    def test_sticky_split_and_legacy_frames(self):
        extended = telemetry_frame(payload=acoustic_payload())
        legacy = telemetry_frame(flow=2, payload=dual_bme_payload())
        parser = UPPER.FrameStreamParser()
        self.assertEqual(parser.feed(b"noise" + extended[:12]), [])
        self.assertEqual(parser.feed(extended[12:30]), [])
        self.assertEqual(parser.feed(extended[30:] + legacy + extended),
                         [extended, legacy, extended])
        for frame in (extended, legacy):
            UPPER.LoRaProtocol.parse_packet(frame)

    def test_stereo_uint32_zero_and_invalid_are_distinct(self):
        values = UPPER.LoRaProtocol.decode_telemetry(acoustic_payload(left=0, right=0xFFFFFFFE))
        self.assertEqual(values["sound_rms_1"], 0)
        self.assertEqual(values["sound_rms_2"], 0xFFFFFFFE)
        self.assertEqual(values["master_bme_temperature_c"], -5.5)
        self.assertEqual(values["slave_bme_temperature_c"], -12.3)
        self.assertEqual(values["slave_bme_pressure_pa"], 100000)
        self.assertIsNone(values["rain_state"])
        self.assertIsNone(values["master_temperature_c"])
        values = UPPER.LoRaProtocol.decode_telemetry(acoustic_payload(left=0xFFFFFFFF, right=77))
        self.assertIsNone(values["sound_rms_1"])
        self.assertEqual(values["sound_rms_2"], 77)
        values = UPPER.LoRaProtocol.decode_telemetry(acoustic_payload(flags=0x0B))
        self.assertFalse(values["slave_online"])
        self.assertIsNone(values["sound_rms_1"])
        self.assertIsNone(values["sound_rms_2"])
        self.assertIsNone(values["slave_bme_pressure_pa"])
        self.assertEqual(values["master_bme_pressure_pa"], 101325)

    def test_layout_guards_reject_wrong_length_flag_pair(self):
        for size in (0, 17, 19, 25, 27):
            with self.assertRaisesRegex(ValueError, "length"):
                UPPER.LoRaProtocol.decode_telemetry(b"\x0F" * size)
        for flags in (0, 1, 3, 7, 8, 0xFF):
            frame = telemetry_frame(payload=acoustic_payload(flags=flags))
            with self.assertRaisesRegex(ValueError, "layout"):
                UPPER.LoRaProtocol.parse_packet(frame)
        for flags in (8, 0x0B, 0x0F):
            frame = telemetry_frame(payload=dual_bme_payload(flags=flags))
            with self.assertRaisesRegex(ValueError, "layout"):
                UPPER.LoRaProtocol.parse_packet(frame)

    def test_database_reuses_integer_columns_without_migration(self):
        database = UPPER.TelemetryDatabase(":memory:")
        try:
            before = database.connection.execute("PRAGMA table_info(telemetry_v4)").fetchall()
            for flow, (left, right) in enumerate(((0, 0xFFFFFFFE), (0xFFFFFFFF, 98765))):
                frame = telemetry_frame(flow=flow, payload=acoustic_payload(left=left, right=right))
                values = UPPER.LoRaProtocol.decode_telemetry(frame[11:-2])
                database.insert(flow, values, frame)
            rows = database.connection.execute(
                "SELECT sound_rms_1, sound_rms_2, raw_frame FROM telemetry_v4 ORDER BY id"
            ).fetchall()
            self.assertEqual([row[:2] for row in rows], [(0, 0xFFFFFFFE), (None, 98765)])
            restored = UPPER.LoRaProtocol.decode_telemetry(
                UPPER.LoRaProtocol.parse_packet(bytes.fromhex(rows[1][2]))["data"])
            self.assertEqual(restored["slave_bme_pressure_pa"], 100000)
            self.assertIsNone(restored["sound_rms_1"])
            self.assertEqual(restored["sound_rms_2"], 98765)
            self.assertEqual(database.connection.execute("PRAGMA table_info(telemetry_v4)").fetchall(), before)
            self.assertEqual(len(before), 13)
        finally:
            database.close()


class AcousticDisplayTests(unittest.TestCase):
    setUp = TelemetryPollingTests.setUp

    def receive(self, payload, flow=1):
        self.app.telemetry_pending = (flow, 100.0)
        self.app._handle_frame(telemetry_frame(flow=flow, payload=payload))

    def test_acoustic_labels_describe_latest_window_and_query_interval(self):
        self.assertIn("最新短窗声音幅度", UPPER.MonitorApp.SOUND_SECTION_TITLE)
        self.assertIn("RMS", UPPER.MonitorApp.SOUND_SECTION_TITLE)
        self.assertIn("约63.7 ms完整采样窗", UPPER.MonitorApp.SOUND_HELP_TEXT)
        self.assertIn("界面约每秒查询一次", UPPER.MonitorApp.SOUND_HELP_TEXT)
        self.assertIn("PCM 计数，不是分贝", UPPER.MonitorApp.SOUND_HELP_TEXT)
        for text in (UPPER.MonitorApp.SOUND_SECTION_TITLE, UPPER.MonitorApp.SOUND_HELP_TEXT):
            self.assertNotIn("最大值", text)
            self.assertNotIn("峰值", text)
        self.assertEqual(UPPER.MonitorApp.TELEMETRY_POLL_INTERVAL_S, 1.0)

    def test_matching_short_windows_replace_high_values_with_low_and_zero(self):
        samples = ((123456, 98765), (77, 33), (0, 0))
        for flow, (left, right) in enumerate(samples, start=1):
            with self.subTest(flow=flow):
                with patch.object(UPPER.time, "monotonic", return_value=99.0 + flow):
                    self.receive(acoustic_payload(left=left, right=right), flow=flow)
                self.assertEqual(self.app.sound_vars["sound_rms_1"].get(), str(left))
                self.assertEqual(self.app.sound_vars["sound_rms_2"].get(), str(right))
                self.assertEqual(self.app.last_telemetry_at, 99.0 + flow)
                self.assertIsNone(self.app.telemetry_pending)
        saved = [(call.args[1]["sound_rms_1"], call.args[1]["sound_rms_2"])
                 for call in self.app.database.insert.call_args_list]
        self.assertEqual(saved, list(samples))

    def test_old_flow_and_timeout_cannot_restore_larger_short_window(self):
        larger = acoustic_payload(left=123456, right=98765)
        self.receive(larger, flow=1)
        self.receive(acoustic_payload(left=0, right=0), flow=2)
        self.app.telemetry_pending = (3, 100.0)
        self.app._handle_frame(telemetry_frame(flow=1, payload=larger))
        self.assertEqual(self.app.telemetry_pending, (3, 100.0))
        self.assertTrue(all(value.get() == "0" for value in self.app.sound_vars.values()))
        self.assertEqual(self.app.last_telemetry_at, 100.0)
        with patch.object(UPPER.time, "monotonic", return_value=105.0):
            self.app._poll_telemetry(105.0)
            self.app._handle_frame(telemetry_frame(flow=3, payload=larger))
        self.assertIsNone(self.app.telemetry_pending)
        self.assertIsNone(self.app.last_telemetry_at)
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.assertEqual(self.app.database.insert.call_count, 2)

    def test_integer_zero_sentinel_and_legacy_display(self):
        self.receive(acoustic_payload(left=0, right=123456))
        self.assertEqual(self.app.sound_vars["sound_rms_1"].get(), "0")
        self.assertEqual(self.app.sound_vars["sound_rms_2"].get(), "123456")
        self.receive(acoustic_payload(left=0xFFFFFFFF, right=0xFFFFFFFF))
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.receive(dual_bme_payload())
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.receive(master_bme_payload(flags=0))
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))

    def test_offline_clears_sound_without_clearing_master_bme(self):
        self.receive(acoustic_payload())
        self.receive(acoustic_payload(flags=0x0B))
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.assertEqual(self.app.value_vars["master_bme_pressure_pa"].get(), "101325 Pa")
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：离线")

    def test_wrong_flow_late_and_duplicate_frames_only_log(self):
        self.app.telemetry_pending = (10, 100.0)
        values_before = {key: value.get() for key, value in self.app.sound_vars.items()}
        self.app._handle_frame(telemetry_frame(flow=9, payload=acoustic_payload()))
        self.assertEqual(self.app.telemetry_pending, (10, 100.0))
        self.assertEqual({key: value.get() for key, value in self.app.sound_vars.items()}, values_before)
        self.assertEqual(self.app.last_telemetry_at, 99.0)
        self.app.database.insert.assert_not_called()
        with patch.object(UPPER.time, "monotonic", return_value=101.0):
            self.app._handle_frame(telemetry_frame(flow=10, payload=acoustic_payload()))
        self.assertEqual(self.app.last_telemetry_at, 101.0)
        self.assertIsNone(self.app.telemetry_pending)
        self.app._handle_frame(telemetry_frame(flow=10, payload=acoustic_payload(left=77)))
        self.assertEqual(self.app.last_telemetry_at, 101.0)
        self.assertEqual(self.app.sound_vars["sound_rms_1"].get(), "123456")
        self.app.database.insert.assert_called_once()
        self.assertEqual(self.app._append_log.call_count, 3)

    def test_matching_flow_at_deadline_cannot_restore_stale_values(self):
        self.app.telemetry_pending = (10, 100.0)
        with patch.object(UPPER.time, "monotonic", return_value=105.0):
            self.app._handle_frame(telemetry_frame(flow=10, payload=acoustic_payload()))
        self.assertIsNone(self.app.telemetry_pending)
        self.assertIsNone(self.app.last_telemetry_at)
        self.assertEqual(self.app.next_telemetry_poll_at, 106.0)
        self.assertTrue(all(value.get() == "--" for value in self.app.value_vars.values()))
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.app.database.insert.assert_not_called()
        self.assertEqual(self.app._append_log.call_count, 1)

    def test_matching_flow_before_deadline_is_accepted(self):
        self.app.telemetry_pending = (10, 100.0)
        with patch.object(UPPER.time, "monotonic", return_value=104.999):
            self.app._handle_frame(telemetry_frame(flow=10, payload=acoustic_payload()))
        self.assertIsNone(self.app.telemetry_pending)
        self.assertEqual(self.app.last_telemetry_at, 104.999)
        self.assertEqual(self.app.sound_vars["sound_rms_1"].get(), "123456")
        self.app.database.insert.assert_called_once()

    def test_timeout_disconnect_and_error_clear_sound_and_timestamp(self):
        self.receive(acoustic_payload())
        self.app.telemetry_pending = (2, 100.0)
        self.app._poll_telemetry(105.0)
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.assertIsNone(self.app.last_telemetry_at)
        self.app._handle_frame(telemetry_frame(flow=2, payload=acoustic_payload()))
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.receive(acoustic_payload())
        self.app.telemetry_pending = (2, 100.0)
        self.app._handle_frame(UPPER.LoRaProtocol.build_packet(0x7E, 1, 0, 2, 1, 2, b"\x0A"))
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.assertIsNone(self.app.last_telemetry_at)
        self.receive(acoustic_payload())
        self.app._disconnect()
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))
        self.assertIsNone(self.app.last_telemetry_at)
        self.app._send.assert_not_called()


class WindowProtocolTests(unittest.TestCase):
    def test_open_close_wire_layout_crc_and_flow(self):
        for action in (0, 1):
            frame = UPPER.LoRaProtocol.cmd_set_window(0x1234, action)
            self.assertEqual(frame[:13], bytes.fromhex("AA 55 04 11 01 00 02 01 34 12 02 01") + bytes((action,)))
            self.assertEqual(len(frame), 15)
            packet = UPPER.LoRaProtocol.parse_packet(frame)
            self.assertEqual(packet["data"], bytes((1, action)))
            self.assertEqual(packet["flow_id"], 0x1234)
            self.assertEqual(UPPER.LoRaProtocol.TYPE_NAMES[packet["type"]], "SET_WINDOW")
            self.assertEqual(struct.unpack_from("<H", frame, 13)[0],
                             UPPER.LoRaProtocol.crc16(frame[2:13]))

    def test_illegal_actions_payloads_and_directions(self):
        for action in (-1, 2, 255, "1", None, 1.0, True):
            with self.assertRaises(ValueError):
                UPPER.LoRaProtocol.cmd_set_window(1, action)
        for payload in (b"", b"\x01", b"\x01\x00\x00", b"\x00\x01", b"\x02\x01", b"\x01\x02"):
            frame = UPPER.LoRaProtocol.build_packet(0x11, 1, 0, 2, 1, 1, payload)
            with self.assertRaisesRegex(ValueError, "payload"):
                UPPER.LoRaProtocol.parse_packet(frame)
        for addresses in ((1, 1, 2, 1), (1, 0, 2, 2), (2, 1, 3, 1), (3, 1, 2, 1)):
            frame = UPPER.LoRaProtocol.build_packet(0x11, *addresses, 1, b"\x01\x01")
            with self.assertRaisesRegex(ValueError, "direction"):
                UPPER.LoRaProtocol.parse_packet(frame)

    def test_window_ack_only_accepts_master_and_status_zero_to_three(self):
        for status in (0, 1, 2, 3):
            UPPER.LoRaProtocol.parse_packet(
                UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, bytes((0x11, status))))
        for status in (4, 255):
            with self.assertRaisesRegex(ValueError, "payload"):
                UPPER.LoRaProtocol.parse_packet(
                    UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, bytes((0x11, status))))
        with self.assertRaisesRegex(ValueError, "direction"):
            UPPER.LoRaProtocol.parse_packet(
                UPPER.LoRaProtocol.build_packet(0x20, 3, 1, 2, 1, 1, b"\x11\x00"))


class WindowControlTests(unittest.TestCase):
    def setUp(self):
        TelemetryPollingTests.setUp(self)
        self.app.FAN_PINS = UPPER.MonitorApp.FAN_PINS
        self.app.FAN_ACK_TIMEOUT_S = UPPER.MonitorApp.FAN_ACK_TIMEOUT_S
        self.app.duty_vars = {channel: ValueStub(25) for channel in (1, 2)}
        self.app._normalize_fan_duty = UPPER.MonitorApp._normalize_fan_duty
        self.app.events = queue.Queue()
        self.app.after = Mock()
        for name in ("_set_fan", "_drain_events"):
            setattr(self.app, name, MethodType(getattr(UPPER.MonitorApp, name), self.app))

    def ack(self, flow=1, command=0x11, status=0):
        return UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, flow, bytes((command, status)))

    def test_immediate_open_sets_one_pending_and_disables_controls(self):
        self.app._set_window(1)
        self.assertEqual(self.app.window_pending, (1, 1, 100.0))
        self.assertIsNone(self.app.window_queued_action)
        packet = UPPER.LoRaProtocol.parse_packet(self.app._send.call_args.args[0])
        self.assertEqual(packet["data"], b"\x01\x01")
        self.assertEqual(self.app.window_status_var.get(), "开窗等待确认")
        for button in (*self.app.window_buttons.values(), *self.app.fan_send_buttons.values(), self.app.telemetry_button):
            button.state.assert_called_with(["disabled"])
        self.app._set_window(0)
        self.app._set_fan(1)
        self.app._request_telemetry()
        self.app._poll_telemetry(200.0)
        self.assertEqual(self.app._send.call_count, 1)

    def test_queued_close_zero_waits_for_both_preceding_requests(self):
        self.app.telemetry_pending = (42, 100.0)
        self.app.fan_pending[43] = (1, 25, 100.0)
        self.app._set_window(0)
        self.assertEqual(self.app.window_queued_action, 0)
        self.assertIsNone(self.app.window_pending)
        self.assertTrue(self.app._window_busy())
        self.app._set_window(1)
        self.app._set_fan(2)
        self.app._request_telemetry()
        self.app._poll_telemetry(101.0)
        self.app._send.assert_not_called()
        self.app._handle_frame(telemetry_frame(flow=42))
        self.app._service_window(100.0)
        self.app._send.assert_not_called()
        self.app._handle_frame(self.ack(flow=43, command=0x10))
        with patch.object(UPPER.time, "monotonic", return_value=102.0):
            self.app._service_window(102.0)
        self.assertEqual(self.app.window_pending, (1, 0, 102.0))
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(self.app._send.call_args.args[0])["data"], b"\x01\x00")

    def test_previous_request_timeout_still_runs_then_sends_window_once(self):
        self.app.telemetry_pending = (42, 100.0)
        self.app.fan_pending[43] = (1, 25, 97.0)
        self.app._set_window(1)
        with patch.object(UPPER.time, "monotonic", return_value=105.0):
            self.app._drain_events()
        self.assertIsNone(self.app.telemetry_pending)
        self.assertFalse(self.app.fan_pending)
        self.assertEqual(self.app.window_pending, (1, 1, 105.0))
        self.assertEqual(self.app._send.call_count, 1)
        self.assertTrue(all(value.get() == "--" for value in self.app.sound_vars.values()))

    def test_matching_ack_statuses_restore_buttons_and_resume_polling(self):
        for status, text in ((0, "启动PWM已确认（完成/停止状态未知）"), (1, "舵机驱动故障"), (2, "结果未知"), (3, "未执行：主机忙")):
            with self.subTest(status=status):
                self.app.window_pending = (20, 0, 100.0)
                self.app._handle_frame(self.ack(flow=20, status=status))
                self.assertIsNone(self.app.window_pending)
                self.assertIn("关窗", self.app.window_status_var.get())
                self.assertIn(text, self.app.window_status_var.get())
                self.assertEqual(self.app.next_telemetry_poll_at, 101.0)
                for button in self.app.window_buttons.values():
                    button.state.assert_called_with(["!disabled"])
        self.app._poll_telemetry(100.9)
        self.app._send.assert_not_called()
        self.app._poll_telemetry(101.0)
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(self.app._send.call_args.args[0])["type"], 0x01)
        self.app.database.insert.assert_not_called()

    def test_wrong_flow_command_direction_crc_and_status_do_not_confirm(self):
        self.app._set_window(1)
        bad_crc = bytearray(self.ack())
        bad_crc[-1] ^= 1
        frames = (self.ack(flow=2), self.ack(command=0x10), self.ack(status=4), bytes(bad_crc),
                  UPPER.LoRaProtocol.build_packet(0x20, 3, 1, 2, 1, 1, b"\x11\x00"))
        for frame in frames:
            self.app._handle_frame(frame)
            self.assertEqual(self.app.window_pending, (1, 1, 100.0))
            self.assertEqual(self.app.window_status_var.get(), "开窗等待确认")

    def test_matching_ack_at_deadline_and_duplicate_cannot_confirm(self):
        self.app._set_window(1)
        with patch.object(UPPER.time, "monotonic", return_value=108.0):
            self.app._handle_frame(self.ack())
        self.assertIsNone(self.app.window_pending)
        self.assertEqual(self.app.window_status_var.get(), "开窗确认超时，结果未知")
        self.app._handle_frame(self.ack())
        self.assertEqual(self.app.window_status_var.get(), "开窗确认超时，结果未知")
        self.assertEqual(self.app._send.call_count, 1)
        self.app._set_window(0)
        self.app._handle_frame(self.ack())
        self.assertEqual(self.app.window_pending[0], 2)
        self.assertEqual(self.app.window_status_var.get(), "关窗等待确认")

    def test_ack_before_deadline_and_ordinary_deadline_without_retry(self):
        self.app._set_window(1)
        with patch.object(UPPER.time, "monotonic", return_value=107.999):
            self.app._handle_frame(self.ack())
        self.assertIsNone(self.app.window_pending)
        self.assertEqual(self.app.window_status_var.get(), "开窗启动PWM已确认（完成/停止状态未知）")
        self.app._set_window(0)
        self.app._service_window(107.999)
        self.assertIsNotNone(self.app.window_pending)
        self.app._service_window(108.0)
        self.assertIsNone(self.app.window_pending)
        self.assertIn("结果未知", self.app.window_status_var.get())
        self.app._service_window(200.0)
        self.assertEqual(self.app._send.call_count, 2)

    def test_matching_error_completes_window_but_wrong_or_late_error_does_not(self):
        self.app._set_window(1)
        self.app._handle_frame(UPPER.LoRaProtocol.build_packet(0x7E, 2, 1, 1, 0, 2, b"\x0A"))
        self.assertIsNotNone(self.app.window_pending)
        self.app._handle_frame(UPPER.LoRaProtocol.build_packet(0x7E, 2, 1, 1, 0, 1, b"\x0A"))
        self.assertIsNone(self.app.window_pending)
        self.assertEqual(self.app.window_status_var.get(), "开窗错误 code=10，结果未知")
        self.app._set_window(0)
        with patch.object(UPPER.time, "monotonic", return_value=108.0):
            self.app._handle_frame(UPPER.LoRaProtocol.build_packet(0x7E, 2, 1, 1, 0, 2, b"\x0A"))
        self.assertEqual(self.app.window_status_var.get(), "关窗确认超时，结果未知")

    def test_disconnect_cancels_queued_and_pending_never_resends_on_reconnect(self):
        for queued in (True, False):
            with self.subTest(queued=queued):
                self.app.serial_port = Mock()
                self.app._send.reset_mock()
                self.app.telemetry_pending = (42, 100.0) if queued else None
                self.app._set_window(0)
                self.app._disconnect()
                self.assertIsNone(self.app.window_pending)
                self.assertIsNone(self.app.window_queued_action)
                self.assertIn("取消" if queued else "结果未知", self.app.window_status_var.get())
                disconnected_status = self.app.window_status_var.get()
                # Closing the UART can enqueue another reader error; keep the original result.
                self.app._disconnect()
                self.assertEqual(self.app.window_status_var.get(), disconnected_status)
                for button in self.app.window_buttons.values():
                    button.state.assert_called_with(["disabled"])
                self.app._send.reset_mock()
                self.app._service_window(200.0)
                self.app.serial_port = Mock()
                self.app._sync_window_buttons()
                self.app._service_window(201.0)
                self.app._send.assert_not_called()

    def test_disconnected_or_failed_write_keeps_no_replayable_action(self):
        self.app.serial_port = None
        self.app._set_window(1)
        self.app._send.assert_not_called()
        self.assertFalse(self.app._window_busy())
        for button in self.app.window_buttons.values():
            button.state.assert_called_with(["disabled"])
        self.app.serial_port = Mock()
        self.app._send.return_value = False
        self.app._set_window(0)
        self.assertFalse(self.app._window_busy())
        self.assertEqual(self.app.window_status_var.get(), "关窗发送失败，结果未知")
        self.app._service_window(200.0)
        self.assertEqual(self.app._send.call_count, 1)

    def test_actual_reconnection_entry_clears_old_action_without_sending(self):
        self.app.serial_port = None
        self.app.window_queued_action = 0
        self.app.window_pending = (41, 1, 90.0)
        self.app.port_var = ValueStub("TEST_PORT")
        self.app._reader = Mock()
        port = Mock()
        serial_stub = SimpleNamespace(Serial=Mock(return_value=port))
        with patch.object(UPPER, "serial", serial_stub), patch.object(UPPER.threading, "Thread") as thread:
            UPPER.MonitorApp._toggle_connection(self.app)
        self.assertIs(self.app.serial_port, port)
        self.assertIsNone(self.app.window_queued_action)
        self.assertIsNone(self.app.window_pending)
        self.assertEqual(self.app.window_status_var.get(), "已连接，尚未发送，位置未知")
        self.app._service_window(100.0)
        self.app._send.assert_not_called()
        thread.return_value.start.assert_called_once()
        for button in self.app.window_buttons.values():
            button.state.assert_called_with(["!disabled"])

    def test_duplicate_success_ack_only_logs_without_changing_confirmed_status(self):
        self.app._set_window(1)
        self.app._handle_frame(self.ack())
        confirmed = self.app.window_status_var.get()
        self.app._append_log.reset_mock()
        self.app._handle_frame(self.ack(status=1))
        self.assertEqual(self.app.window_status_var.get(), confirmed)
        self.assertIsNone(self.app.window_pending)
        self.assertEqual(self.app._append_log.call_count, 2)
        self.assertEqual(self.app._send.call_count, 1)

    def test_real_send_starts_deadline_after_complete_uart_write(self):
        self.app._send = MethodType(UPPER.MonitorApp._send, self.app)
        clock = Mock(return_value=100.0)
        def write(frame):
            clock.return_value = 100.5
            return len(frame)
        self.app.serial_port.write.side_effect = write
        with patch.object(UPPER.time, "monotonic", clock):
            self.app._set_window(1)
        self.assertEqual(self.app.window_pending, (1, 1, 100.5))
        self.assertEqual(self.app.serial_port.write.call_count, 1)
        self.app._service_window(108.499)
        self.assertIsNotNone(self.app.window_pending)
        self.app._service_window(108.5)
        self.assertIsNone(self.app.window_pending)

    def test_partial_write_disconnects_and_does_not_retry(self):
        self.app._send = MethodType(UPPER.MonitorApp._send, self.app)
        port = self.app.serial_port
        port.write.return_value = 1
        self.app._set_window(1)
        self.assertIsNone(self.app.serial_port)
        self.assertFalse(self.app._window_busy())
        self.assertIn("结果未知", self.app.window_status_var.get())
        self.app._service_window(200.0)
        self.assertEqual(port.write.call_count, 1)
        port.close.assert_called_once()

    def test_ui_build_has_two_exact_buttons_initially_disabled(self):
        self.app.serial_port = None
        self.app._refresh_ports = Mock()
        self.app._toggle_connection = Mock()
        self.app._preview_fan = Mock()
        self.app._ui_scale = 1.0
        for name in ("rowconfigure", "columnconfigure", "bind_class", "_layout_dashboard",
                     "_update_scroll_region", "_on_dashboard_mousewheel"):
            setattr(self.app, name, Mock())
        self.app.port_var = ValueStub("")
        created = []
        def widget(*args, **kwargs):
            result = Mock()
            result.options = kwargs
            result.bindtags.return_value = ()
            result.winfo_children.return_value = []
            created.append(result)
            return result
        with patch.multiple(UPPER.ttk, Frame=widget, LabelFrame=widget, Label=widget, Button=widget,
                            Combobox=widget, Scale=widget, Spinbox=widget, Scrollbar=widget), \
                patch.object(UPPER.tk, "StringVar", return_value=ValueStub("")), \
                patch.object(UPPER.tk, "Canvas", side_effect=widget), \
                patch.object(UPPER.tk, "Text", side_effect=widget):
            UPPER.MonitorApp._build_ui(self.app)
        self.assertEqual(self.app.window_buttons[1].options["text"], "打开窗户")
        self.assertEqual(self.app.window_buttons[0].options["text"], "关闭窗户")
        labels = [item.options for item in created if "text" in item.options]
        motion = next(item for item in labels if "各运行 300 ms" in item["text"])
        self.assertIn("1500 μs 停止脉宽（需空载校准）", motion["text"])
        self.assertLessEqual(motion["wraplength"], 760)
        disclaimer = next(item for item in labels if "ACK 仅确认动作启动 PWM" in item["text"])
        self.assertIn("不表示动作完成、自动停止成功或机械到位", disclaimer["text"])
        self.assertLessEqual(disclaimer["wraplength"], 760)
        for button in self.app.window_buttons.values():
            button.state.assert_called_with(["disabled"])
        self.app.window_buttons[0].options["command"]()
        self.assertFalse(self.app._window_busy())
        self.app._send.assert_not_called()


if __name__ == "__main__":
    unittest.main()
