"""MQ-2 protocol and presentation regressions; only in-memory/mock transports."""
import struct
import unittest

import test_controller as fixtures
import test_protocol as legacy_tests
from backend.controller import SerialEvent
from backend.protocol import LoRaProtocol, FrameStreamParser
from backend.smoke import smoke_index


class SmokeIndexTests(unittest.TestCase):
    def test_site_reference_interpolation_and_invalid_values(self):
        for mv, expected in ((0, 0), (330, 0), (350, 0), (600, 13), (800, 23), (850, 25),
                             (900, 28), (1000, 33), (1175, 43), (1500, 60), (2000, 80),
                             (2100, 84), (2500, 100), (3300, 100)):
            self.assertEqual(smoke_index(mv), expected)
        for value in (None, '850', -1, 3601, float('nan'), float('inf')):
            self.assertIsNone(smoke_index(value))
        indices = [smoke_index(mv) for mv in range(3601)]
        self.assertEqual(indices, sorted(indices))


def payload(flags=0x1F, values=(1241, 1000, 2000, 60)):
    return fixtures.telemetry_payload(flags=flags) + struct.pack('<4H', *values)


class Mq2ProtocolTests(unittest.TestCase):
    def test_both_desktops_decode_zero_max_invalid_offline_and_old_frames(self):
        for protocol in (LoRaProtocol, legacy_tests.UPPER.LoRaProtocol):
            for values in ((0, 0, 0, 0), (4095, 3600, 7200, 1999), (65535,) * 4):
                with self.subTest(protocol=protocol, values=values):
                    decoded = protocol.decode_telemetry(payload(values=values))
                    self.assertEqual(decoded['mq2']['valid'], values[0] != 65535)
                    self.assertEqual(decoded['mq2']['raw'], None if values[0] == 65535 else values[0])
                    self.assertEqual(decoded['master_bme_temperature_c'], 23.6)
                    self.assertEqual(decoded['slave_bme_temperature_c'], 24.2)
                    self.assertEqual(decoded['sound_rms_2'], 9873)
            self.assertFalse(protocol.decode_telemetry(payload(flags=0x1B))['mq2']['valid'])
            self.assertFalse(protocol.decode_telemetry(fixtures.telemetry_payload())['mq2']['valid'])
            self.assertFalse(protocol.decode_telemetry(protocol.placeholder_payload())['mq2']['valid'])

    def test_layout_flags_bounds_and_partial_sentinels_are_rejected(self):
        for protocol in (LoRaProtocol, legacy_tests.UPPER.LoRaProtocol):
            for flags in range(256):
                with self.subTest(flags=flags, protocol=protocol):
                    if flags in (0x1B, 0x1F):
                        protocol.decode_telemetry(payload(flags=flags))
                    else:
                        with self.assertRaises(ValueError):
                            protocol.decode_telemetry(payload(flags=flags))
            for values in ((4096, 0, 0, 0), (0, 3601, 0, 0), (0, 0, 7201, 0),
                           (0, 0, 0, 2000), (65535, 0, 0, 0), (0, 0, 0, 65535)):
                with self.assertRaises(ValueError):
                    protocol.decode_telemetry(payload(values=values))
            with self.assertRaises(ValueError):
                protocol.decode_telemetry(payload()[:26])

    def test_47_byte_frame_crc_and_every_stream_split(self):
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, 42, payload())
        self.assertEqual(len(frame), 47)
        for split in range(1, len(frame)):
            parser = FrameStreamParser()
            self.assertEqual(parser.feed(frame[:split]), [])
            self.assertEqual(parser.feed(frame[split:]), [frame])
        damaged = bytearray(frame); damaged[37] ^= 1
        with self.assertRaises(ValueError):
            LoRaProtocol.parse_packet(bytes(damaged))


class Mq2ControllerTests(unittest.TestCase):
    def setUp(self):
        fixtures.ControllerTests.setUp(self)
        self.controller.connect('MOCK1')

    def tearDown(self):
        self.controller.close()

    def receive(self, data=None, received_at=None):
        self.controller.read_once()
        flow = self.controller.telemetry_pending[0]
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, flow, payload() if data is None else data)
        self.controller._handle_frame(frame, received_at)
        return frame

    def test_zero_independent_of_bme_and_full_raw_frame_is_saved(self):
        data = bytearray(payload(values=(0, 0, 0, 0)))
        data[1:9] = struct.pack('<hHI', -32768, 65535, 0xFFFFFFFF)
        data[9:17] = struct.pack('<hHI', -32768, 65535, 0xFFFFFFFF)
        frame = self.receive(bytes(data))
        snapshot = self.controller.snapshot()['telemetry']
        self.assertEqual(snapshot['values']['master_temp'], '--')
        self.assertTrue(snapshot['mq2']['valid'])
        self.assertEqual(snapshot['mq2']['raw'], 0)
        saved = self.database.connection.execute('SELECT raw_frame FROM telemetry_v4').fetchone()[0]
        self.assertEqual(bytes.fromhex(saved), frame)

    def test_local_age_expiry_does_not_clear_bme_or_refresh_on_wrong_flow(self):
        self.receive(payload(values=(1241, 1000, 2000, 1500)))
        self.clock.advance(0.25); self.controller.tick()
        self.assertGreaterEqual(self.controller.mq2['age_ms'], 1749)
        self.controller._handle_frame(fixtures.response(LoRaProtocol.MSG_TELEMETRY, 999, payload()))
        self.clock.advance(0.26); self.controller.tick()
        self.assertFalse(self.controller.mq2['valid'])
        self.assertEqual(self.controller.values['slave_temp'], '24.2 ℃')

    def test_reader_queue_delay_is_counted(self):
        self.controller.read_once(); flow = self.controller.telemetry_pending[0]
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, flow, payload(values=(0, 0, 0, 1900)))
        self.controller.events.put(SerialEvent(self.controller.connection_generation, 'frame', frame, self.clock()))
        self.clock.advance(0.11); self.controller.tick()
        self.assertFalse(self.controller.mq2['valid'])

    def test_old_offline_invalid_disconnect_and_timeout_clear_mq2(self):
        for data in (fixtures.telemetry_payload(), payload(flags=0x1B), payload(values=(65535,) * 4)):
            self.receive(); self.assertTrue(self.controller.mq2['valid'])
            self.receive(data); self.assertFalse(self.controller.mq2['valid'])
        self.receive(); self.controller.disconnect()
        self.assertFalse(self.controller.snapshot()['telemetry']['mq2']['valid'])
        self.controller.connect('MOCK1'); self.receive(); self.controller.read_once()
        self.clock.advance(5); self.controller.tick()
        self.assertFalse(self.controller.mq2['valid'])


class Mq2LegacyTests(unittest.TestCase):
    def setUp(self):
        self.fixture = legacy_tests.TelemetryPollingTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.app = self.fixture.app

    def test_tk_values_source_age_expiry_and_clear(self):
        self.app.telemetry_pending = (42, 99.0)
        frame = fixtures.response(LoRaProtocol.MSG_TELEMETRY, 42, payload(values=(0, 0, 0, 1500)))
        self.app._handle_frame(frame)
        self.assertEqual(self.app.mq2_vars['raw'].get(), '0 计数')
        self.assertEqual(self.app.mq2_vars['index'].get(), '0 / 100')
        self.app._refresh_mq2(100.5)
        self.assertTrue(all(item.get() == '--' for item in self.app.mq2_vars.values()))
        self.assertNotEqual(self.app.value_vars['master_bme_temperature_c'].get(), '--')
        self.app._clear_telemetry()
        self.assertFalse(self.app.mq2['valid'])
