import importlib.util
import pathlib
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch


MODULE_PATH = pathlib.Path(__file__).parents[1] / "main.py"
SPEC = importlib.util.spec_from_file_location("upper_v4", MODULE_PATH)
UPPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPPER)


class ProtocolV4Tests(unittest.TestCase):
    def test_all_fan_channels_and_duty_boundaries(self):
        for channel in range(1, 5):
            for duty in (0, 1, 25, 50, 75, 99, 100):
                packet = UPPER.LoRaProtocol.parse_packet(
                    UPPER.LoRaProtocol.cmd_set_fan_speed(5, channel, duty))
                self.assertEqual(packet["data"], bytes((channel, duty)))
        for channel, duty in ((0, 50), (5, 50), (1, -1), (1, 101)):
            with self.assertRaises(ValueError):
                UPPER.LoRaProtocol.cmd_set_fan_speed(1, channel, duty)

    def test_read_telemetry_frame_layout(self):
        frame = UPPER.LoRaProtocol.cmd_read_telemetry(0x1234, True)
        self.assertEqual(frame[:11], bytes.fromhex("AA 55 04 01 01 00 02 01 34 12 01"))
        self.assertEqual(frame[11], 1)
        self.assertEqual(len(frame), 14)
        parsed = UPPER.LoRaProtocol.parse_packet(frame)
        self.assertEqual(parsed["flow_id"], 0x1234)

    def test_fan_command_frame_layout(self):
        frame = UPPER.LoRaProtocol.cmd_set_fan_speed(7, 4, 75)
        self.assertEqual(frame[3], UPPER.LoRaProtocol.MSG_SET_FAN_SPEED)
        self.assertEqual(frame[8:10], b"\x07\x00")
        self.assertEqual(frame[10:13], b"\x02\x04\x4B")
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
            duty_vars=[ValueStub(25.6) for _ in range(4)],
            fan_status_vars=[ValueStub("未发送") for _ in range(4)],
            fan_pending={},
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
        UPPER.MonitorApp._set_fan(self.app, 3)
        frame = self.app._send.call_args.args[0]
        self.assertEqual(UPPER.LoRaProtocol.parse_packet(frame)["data"], bytes((3, 26)))
        self.assertEqual(self.app.fan_status_vars[2].get(), "等待确认 26%")
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x10\x00")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertEqual(self.app.fan_status_vars[2].get(), "已确认 26%")
        self.assertFalse(self.app.fan_pending)

    def test_older_ack_cannot_confirm_new_setting(self):
        UPPER.MonitorApp._set_fan(self.app, 1)
        self.app.duty_vars[0].set(70)
        UPPER.MonitorApp._set_fan(self.app, 1)
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x10\x00")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertEqual(self.app.fan_status_vars[0].get(), "等待确认 70%")
        self.assertEqual(list(self.app.fan_pending), [2])

    def test_invalid_input_and_disconnected_send(self):
        self.app.duty_vars[0].set(101)
        with patch.object(UPPER.messagebox, "showwarning"):
            UPPER.MonitorApp._set_fan(self.app, 1)
        self.app._send.assert_not_called()
        self.app.duty_vars[0].set(50)
        self.app._send.return_value = False
        UPPER.MonitorApp._set_fan(self.app, 1)
        self.assertFalse(self.app.fan_pending)

    def test_wrong_command_ack_and_error(self):
        UPPER.MonitorApp._set_fan(self.app, 4)
        ack = UPPER.LoRaProtocol.build_packet(0x20, 2, 1, 1, 0, 1, b"\x01\x00")
        UPPER.MonitorApp._handle_frame(self.app, ack)
        self.assertIn(1, self.app.fan_pending)
        error = UPPER.LoRaProtocol.build_packet(0x7E, 1, 0, 2, 1, 1, b"\x0A")
        UPPER.MonitorApp._handle_frame(self.app, error)
        self.assertEqual(self.app.fan_status_vars[3].get(), "失败 code=10")
        self.assertFalse(self.app.fan_pending)


if __name__ == "__main__":
    unittest.main()
