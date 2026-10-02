import importlib.util
import pathlib
import struct
import unittest
from types import MethodType, SimpleNamespace
from unittest.mock import Mock, patch


MODULE_PATH = pathlib.Path(__file__).parents[1] / "main.py"
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


class FanControlTests(unittest.TestCase):
    def setUp(self):
        self.app = SimpleNamespace(
            duty_vars={channel: ValueStub(25.6) for channel in (1, 2)},
            fan_status_vars={channel: ValueStub("未发送") for channel in (1, 2)},
            fan_pending={},
            telemetry_pending=None,
            _normalize_fan_duty=UPPER.MonitorApp._normalize_fan_duty,
            _next_flow=Mock(side_effect=range(1, 100)),
            _send=Mock(return_value=True),
            _append_log=Mock(),
        )

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
        for payload in (invalid, master_bme_payload(flags=0), master_bme_payload(flags=2)):
            values = UPPER.LoRaProtocol.decode_telemetry(payload)
            for key in ("master_bme_temperature_c", "master_bme_humidity_pct", "master_bme_pressure_pa"):
                self.assertIsNone(values[key])
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
        self.app = SimpleNamespace(
            serial_port=Mock(), fan_pending={}, telemetry_pending=None,
            next_telemetry_poll_at=100.0,
            TELEMETRY_POLL_INTERVAL_S=1.0, TELEMETRY_TIMEOUT_S=5.0,
            FIELD_LABELS=UPPER.MonitorApp.FIELD_LABELS,
            value_vars={name: ValueStub("old") for name, _, _ in UPPER.MonitorApp.FIELD_LABELS},
            slave_link_var=ValueStub("old"),
            fan_status_vars={channel: ValueStub("ready") for channel in (1, 2)},
            _next_flow=Mock(side_effect=range(1, 100)), _send=Mock(return_value=True),
            _append_log=Mock(), database=Mock(), stop_event=Mock(), connect_button=Mock(),
        )
        for name in ("_request_telemetry", "_poll_telemetry", "_clear_telemetry", "_handle_frame", "_disconnect"):
            setattr(self.app, name, MethodType(getattr(UPPER.MonitorApp, name), self.app))

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
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload()))
        self.assertEqual(self.app.value_vars["master_bme_pressure_pa"].get(), "101325 Pa")
        self.assertEqual(self.app.value_vars["slave_bme_pressure_pa"].get(), "100000 Pa")
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：在线")
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload(flags=3)))
        self.assertEqual(self.app.value_vars["master_bme_pressure_pa"].get(), "101325 Pa")
        self.assertEqual(self.app.value_vars["slave_bme_pressure_pa"].get(), "--")
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：离线")
        self.app._handle_frame(telemetry_frame(payload=dual_bme_payload()))
        self.assertEqual(self.app.value_vars["slave_bme_temperature_c"].get(), "-12.3 ℃")
        self.app._clear_telemetry()
        self.assertEqual(self.app.slave_link_var.get(), "从机链路：未知")

    def test_sensor_failure_does_not_mean_link_offline(self):
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
        for flags in (0, 1, 2, 4, 5, 6, 8, 11, 0xFF):
            values = UPPER.LoRaProtocol.decode_telemetry(dual_bme_payload(flags=flags))
            self.assertIsNone(values["slave_online"])
            self.assertIsNone(values["slave_bme_temperature_c"])
            self.assertIsNone(values["slave_bme_pressure_pa"])
            if flags != 1:
                self.assertIsNone(values["master_bme_temperature_c"])

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


if __name__ == "__main__":
    unittest.main()
