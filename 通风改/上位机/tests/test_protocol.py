import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).parents[1] / "main.py"
SPEC = importlib.util.spec_from_file_location("upper_v4", MODULE_PATH)
UPPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPPER)


class ProtocolV4Tests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
