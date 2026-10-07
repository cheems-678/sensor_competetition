"""Five master analog microphones; no ports, disk DB, or real commands."""
import struct
import unittest
import test_controller as fixtures
from test_controller import response, telemetry_payload
from backend.protocol import LoRaProtocol as P, FrameStreamParser
from legacy_tk import LoRaProtocol as TkP


def payload(flags=0x77, sounds=(0, 100, 200, 300, 4095), age=50):
    return struct.pack("<BhHIhHIB14H", flags, 236, 478, 101325,
                       242, 513, 100982, 0, *sounds, age,
                       1241, 1000, 2000, 60, 50, 52, 300, 60)


class Max4466ProtocolTests(unittest.TestCase):
    def test_five_order_zero_bme_rain_mq_distance_and_legacy_parity(self):
        for flags in (0x73, 0x77):
            p = payload(flags)
            values = P.decode_telemetry(p)
            self.assertEqual(values, TkP.decode_telemetry(p))
            self.assertEqual([values[f"sound_p2p_{i}"] for i in range(1, 6)], [0, 100, 200, 300, 4095])
            self.assertEqual(values["max4466_age_ms"], 50)
            self.assertIsNone(values["sound_rms_1"])
            self.assertEqual(values["master_bme_pressure_pa"], 101325)
            self.assertEqual(values["rain_source"], "master")
            self.assertEqual(values["mq2"]["valid"], flags == 0x77)
            self.assertEqual(values["ultrasonic"]["valid"], flags == 0x77)
        values = P.decode_telemetry(telemetry_payload())
        self.assertTrue(all(values[f"sound_p2p_{i}"] is None for i in range(1, 6)))

    def test_all_flags_partial_sentinels_bounds_and_age(self):
        for flags in range(256):
            for protocol in (P, TkP):
                if flags in (0x73, 0x77):
                    protocol.decode_telemetry(payload(flags))
                else:
                    with self.assertRaises(ValueError): protocol.decode_telemetry(payload(flags))
        for sounds, age in (((65535,)*5, 65535), ((0,)*5, 0), ((4095,)*5, 299)):
            P.decode_telemetry(payload(sounds=sounds, age=age))
        for sounds, age in (((65535,)*5, 0), ((0,)*5, 65535), ((4096, 0, 0, 0, 0), 0), ((0,)*5, 300)):
            with self.assertRaises(ValueError): P.decode_telemetry(payload(sounds=sounds, age=age))

    def test_complete_frame_crc_and_every_split(self):
        frame = response(P.MSG_TELEMETRY, 42, payload())
        self.assertEqual(len(frame), 59)
        for split in range(1, len(frame)):
            parser = FrameStreamParser()
            self.assertEqual(parser.feed(frame[:split]), [])
            self.assertEqual(parser.feed(frame[split:]), [frame])
        broken = bytearray(frame); broken[37] ^= 1
        with self.assertRaises(ValueError): P.parse_packet(bytes(broken))


class Max4466ControllerTests(unittest.TestCase):
    setUp = fixtures.ControllerTests.setUp
    tearDown = fixtures.ControllerTests.tearDown
    connect = fixtures.ControllerTests.connect
    sample = fixtures.ControllerTests.sample

    def test_latest_five_decreases_zero_offline_old_frame_and_db_isolation(self):
        self.connect()
        for sounds in ((4000,)*5, (20,)*5, (0,)*5):
            self.sample(payload(0x73, sounds))
            self.assertEqual([self.controller.sounds[f"sound_p2p_{i}"] for i in range(1, 6)], list(map(str, sounds)))
        rows = self.database.connection.execute("SELECT sound_rms_1,sound_rms_2,raw_frame FROM telemetry_v4").fetchall()
        self.assertTrue(all(a is None and b is None and len(raw.split()) == 59 for a, b, raw in rows))
        self.sample(telemetry_payload())
        self.assertTrue(all(self.controller.sounds[f"sound_p2p_{i}"] == "--" for i in range(1, 6)))

    def test_wrong_flow_timeout_and_disconnect_cannot_restore_stale(self):
        self.connect(); flow = self.sample(payload())
        self.controller.read_once(); current = self.controller.telemetry_pending[0]
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, payload(sounds=(4000,)*5)))
        self.assertEqual(self.controller.sounds["sound_p2p_1"], "0")
        self.clock.advance(5); self.controller._handle_frame(response(P.MSG_TELEMETRY, current, payload()))
        self.assertTrue(all(v == "--" for v in self.controller.sounds.values()))
        self.sample(payload()); self.controller.disconnect()
        self.assertTrue(all(v == "--" for v in self.controller.sounds.values()))
