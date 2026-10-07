"""Ultrasonic telemetry regressions with mock UART and memory SQLite."""
import struct
import unittest
from unittest.mock import patch
import test_controller as fixtures
import test_protocol as legacy_tests
from backend.protocol import LoRaProtocol, FrameStreamParser
from backend.controller import SerialEvent


def payload(values=(250, 252, 1469, 60), flags=0x3F, mq=(1241, 1000, 2000, 60)):
    return fixtures.telemetry_payload(flags=flags) + struct.pack('<8H', *mq, *values)


class UltrasonicProtocolTests(unittest.TestCase):
    def test_bounds_sentinels_compatibility_and_layout_matrix(self):
        for protocol in (LoRaProtocol, legacy_tests.UPPER.LoRaProtocol):
            for values in ((50, 50, 292, 0), (100, 100, 1, 0), (500, 500, 9999, 1999), (65535,) * 4):
                decoded = protocol.decode_telemetry(payload(values))
                self.assertEqual(decoded['ultrasonic']['valid'], values[0] != 65535)
                self.assertTrue(decoded['mq2']['valid'])
                self.assertEqual(decoded['slave_bme_temperature_c'], 24.2)
            for flags in range(256):
                if flags in (0x3B, 0x3F):
                    self.assertEqual(protocol.decode_telemetry(payload(flags=flags))['ultrasonic']['valid'], flags == 0x3F)
                else:
                    with self.assertRaises(ValueError): protocol.decode_telemetry(payload(flags=flags))
            for values in ((49, 250, 1000, 0), (501, 250, 1000, 0), (250, 49, 1000, 0),
                           (250, 501, 1000, 0), (250, 250, 0, 0), (250, 250, 10000, 0),
                           (250, 250, 1000, 2000), (65535, 250, 1000, 0), (250, 250, 1000, 65535)):
                with self.assertRaises(ValueError): protocol.decode_telemetry(payload(values))
            for old in (protocol.placeholder_payload(), fixtures.telemetry_payload(),
                        fixtures.telemetry_payload(flags=0x1F) + struct.pack('<4H', 0, 0, 0, 0)):
                self.assertFalse(protocol.decode_telemetry(old)['ultrasonic']['valid'])
            with self.assertRaises(ValueError): protocol.decode_telemetry(payload(mq=(4096, 0, 0, 0)))

    def test_crc_every_split_and_raw_frame(self):
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, 42, payload())
        self.assertEqual(len(frame), 55)
        for split in range(1, len(frame)):
            parser = FrameStreamParser()
            self.assertEqual(parser.feed(frame[:split]), [])
            self.assertEqual(parser.feed(frame[split:] + frame), [frame, frame])
        damaged = bytearray(frame); damaged[45] ^= 1
        with self.assertRaises(ValueError): LoRaProtocol.parse_packet(bytes(damaged))


class UltrasonicControllerTests(unittest.TestCase):
    def setUp(self):
        fixtures.ControllerTests.setUp(self)
        self.controller.connect('MOCK1')

    def tearDown(self): self.controller.close()

    def receive(self, data=None):
        self.controller.read_once()
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, self.controller.telemetry_pending[0], payload() if data is None else data)
        self.controller._handle_frame(frame)
        return frame

    def test_independent_values_storage_expiry_and_recovery(self):
        frame = self.receive(payload((100, 102, 595, 1500), mq=(65535,) * 4))
        reading = self.controller.snapshot()['telemetry']['ultrasonic']
        self.assertEqual(reading['distance_mm'], 100)
        self.assertFalse(self.controller.mq2['valid'])
        saved = self.database.connection.execute('SELECT raw_frame FROM telemetry_v4').fetchone()[0]
        self.assertEqual(bytes.fromhex(saved), frame)
        self.clock.advance(.51); self.controller.tick()
        self.assertFalse(self.controller.ultrasonic['valid'])
        self.assertEqual(self.controller.values['slave_temp'], '24.2 ℃')
        self.receive(); self.assertTrue(self.controller.ultrasonic['valid'])

    def test_reader_delay_wrong_flow_old_offline_fault_disconnect_timeout(self):
        self.receive(payload((250, 250, 1458, 1900)))
        self.controller._handle_frame(fixtures.response(LoRaProtocol.MSG_TELEMETRY, 999, payload()))
        self.clock.advance(.11); self.controller.tick()
        self.assertFalse(self.controller.ultrasonic['valid'])
        for data in (fixtures.telemetry_payload(), payload(flags=0x3B), payload((65535,) * 4)):
            self.receive(); self.receive(data); self.assertFalse(self.controller.ultrasonic['valid'])
        self.controller.read_once()
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, self.controller.telemetry_pending[0], payload((250, 250, 1458, 1900)))
        self.controller.events.put(SerialEvent(self.controller.connection_generation, 'frame', frame, self.clock()))
        self.clock.advance(.11); self.controller.tick()
        self.assertFalse(self.controller.ultrasonic['valid'])
        self.receive(); self.controller.disconnect()
        self.assertEqual(self.controller.snapshot()['telemetry']['ultrasonic'], self.controller._empty_ultrasonic())
        self.controller.connect('MOCK1'); self.receive(); self.controller.read_once()
        self.clock.advance(5); self.controller.tick(); self.assertFalse(self.controller.ultrasonic['valid'])


class UltrasonicLegacyTests(unittest.TestCase):
    def test_tk_units_expiry_and_clear(self):
        fixture = legacy_tests.TelemetryPollingTests(); fixture.setUp()
        self.addCleanup(fixture.doCleanups); app = fixture.app
        app.telemetry_pending = (42, 99.0)
        with patch.object(legacy_tests.UPPER.time, 'monotonic', return_value=100.0):
            app._handle_frame(fixtures.response(LoRaProtocol.MSG_TELEMETRY, 42, payload((250, 252, 1469, 1500))))
        self.assertEqual(app.ultrasonic_vars['distance_mm'].get(), '25.0 cm')
        self.assertEqual(app.ultrasonic_vars['raw_mm'].get(), '25.2 cm')
        app._refresh_ultrasonic(100.51)
        self.assertEqual(app.ultrasonic_vars['distance_mm'].get(), '--')
        app._clear_telemetry(); self.assertFalse(app.ultrasonic['valid'])
