"""Hardware-free regression tests for the new controller and desktop bridge."""

import ast
import pathlib
import struct
import sys
import threading
import time
import unittest
from datetime import datetime
from unittest.mock import Mock, patch

UPPER_DIR = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(UPPER_DIR))

from backend.controller import Controller, SerialEvent
from backend.demo import DemoSerial
from backend.protocol import FrameStreamParser, LoRaProtocol as P
from backend.service import ControllerService, DesktopAPI
from backend.storage import TelemetryDatabase


class Clock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now

    def advance(self, amount):
        self.now += amount


class FakeSerial:
    def __init__(self, port="MOCK", baudrate=115200, *, timeout=0.1, write_timeout=0.5):
        self.port, self.closed = port, False
        self.writes = []
        self.stop = threading.Event()
        self.on_write = None

    @property
    def in_waiting(self):
        return 0

    def read(self, count):
        self.stop.wait(0.01)
        return b""

    def write(self, frame):
        self.writes.append(frame)
        return self.on_write(frame) if self.on_write else len(frame)

    def close(self):
        self.closed = True
        self.stop.set()


def response(msg_type, flow, payload):
    return P.build_packet(msg_type, 2, 1, 1, 0, flow, payload)


def ack(flow, command=P.MSG_SET_FAN_SPEED, status=0):
    return response(P.MSG_ACK, flow, bytes((command, status)))


def telemetry_payload(flags=0x0F, left=0, right=9873, slave_temp=242):
    payload = struct.pack("<BhHIhHIBII", flags, 236, 478, 101325,
                          slave_temp, 513, 100982, 0xFF, left, right)
    return payload[:18] if flags in (3, 7) else payload


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.ports = []
        def factory(*args, **kwargs):
            port = FakeSerial(*args, **kwargs)
            self.ports.append(port)
            return port
        self.database = TelemetryDatabase(":memory:")
        self.controller = Controller(self.database, serial_factory=factory,
                                     port_provider=lambda: ["MOCK1", "MOCK2"], clock=self.clock,
                                     wall_clock=lambda: datetime(2026, 10, 4, 12, 34, 56),
                                     start_readers=False)

    def tearDown(self):
        self.controller.close()

    def connect(self):
        self.controller.connect("MOCK1")
        return self.ports[-1]

    def sample(self, payload=None, flow=None):
        self.controller.read_once()
        current = self.controller.telemetry_pending[0]
        self.controller._handle_frame(response(P.MSG_TELEMETRY, current if flow is None else flow,
                                              telemetry_payload() if payload is None else payload))
        return current

    def stored_rows(self):
        return self.database.connection.execute("SELECT COUNT(*) FROM telemetry_v4").fetchone()[0]

    def test_initial_snapshot_and_disconnected_button_rules(self):
        s = self.controller.snapshot()
        self.assertFalse(s["connected"])
        self.assertTrue(s["controls"]["read_enabled"])
        self.assertTrue(s["controls"]["fan_enabled"])
        self.assertFalse(s["controls"]["window_enabled"])
        self.assertEqual(s["window"]["status"], "未连接，位置未知")
        self.assertEqual([(f["channel"], f["pin"], f["duty"]) for f in s["fans"]],
                         [(1, "PB1", 0), (2, "PB8", 0)])
        self.assertTrue(all(v == "--" for v in s["telemetry"]["values"].values()))

    def test_refresh_only_selects_first_if_no_existing_port(self):
        self.controller.refresh_ports()
        self.assertEqual(self.controller.port, "MOCK1")
        self.controller.port = "COM_CUSTOM"
        self.controller.refresh_ports()
        self.assertEqual(self.controller.port, "COM_CUSTOM")

    def test_connection_does_not_send_and_waits_one_second_to_poll(self):
        port = self.connect()
        self.assertEqual(port.writes, [])
        self.clock.advance(0.999)
        self.controller.tick()
        self.assertEqual(port.writes, [])
        self.clock.advance(0.001)
        self.controller.tick()
        self.assertEqual(P.parse_packet(port.writes[0])["data"], b"\0")
        self.assertEqual(self.controller.telemetry_pending, (1, 101.0))

    def test_flow_rollover_keeps_uint16_wire_identifier(self):
        port = self.connect()
        self.controller.flow_id = 65535
        self.controller.set_fan(1, 0)
        self.assertEqual(P.parse_packet(port.writes[-1])["flow_id"], 0)

    def test_connection_failure_and_missing_dependency_are_notices(self):
        self.controller.serial_factory = Mock(side_effect=OSError("busy"))
        self.controller.connect("MOCK1")
        self.assertEqual(self.controller.notice["title"], "连接失败")
        self.assertIsNone(self.controller.serial_port)
        self.controller.serial_factory = None
        self.controller.connect("MOCK1")
        self.assertEqual(self.controller.notice["title"], "缺少依赖")

    def test_fan_commands_round_and_retain_channel_mapping(self):
        port = self.connect()
        for channel, value, expected in ((1, "25.6", 26), (2, 100, 100), (1, 0, 0)):
            self.controller.set_fan(channel, value)
            self.assertEqual(P.parse_packet(port.writes[-1])["data"], bytes((channel, expected)))
            self.assertEqual(self.controller.fan_status[channel], f"等待确认 {expected}%")

    def test_invalid_duty_or_channel_does_not_write_or_create_pending(self):
        port = self.connect()
        for invalid in ("", "abc", "NaN", "inf", -1, 101, None):
            self.controller.set_fan(1, invalid)
            self.assertEqual(self.controller.notice["title"], "输入错误")
        for channel in (0, 3, 4, 5):
            self.controller.set_fan(channel, 50)
            self.assertEqual(self.controller.notice["title"], "通道已停用")
        self.assertEqual(port.writes, [])
        self.assertEqual(self.controller.fan_pending, {})

    def test_disconnected_fan_and_read_warn_without_replay(self):
        self.controller.set_fan(1, 75)
        self.controller.read_once()
        self.assertEqual(self.controller.notice["title"], "未连接")
        self.assertFalse(self.controller.fan_pending)
        self.assertIsNone(self.controller.telemetry_pending)
        port = self.connect()
        self.assertEqual(port.writes, [])

    def test_matching_fan_ack_only_and_rejection(self):
        self.connect()
        self.controller.set_fan(2, 75)
        flow = self.controller.flow_id
        self.controller._handle_frame(ack(flow + 1))
        self.controller._handle_frame(ack(flow, P.MSG_SET_WINDOW))
        self.assertEqual(self.controller.fan_status[2], "等待确认 75%")
        self.controller._handle_frame(ack(flow, status=1))
        self.assertEqual(self.controller.fan_status[2], "主机拒绝执行")
        self.assertFalse(self.controller.fan_pending)

    def test_new_fan_command_supersedes_only_its_channel(self):
        self.connect()
        self.controller.set_fan(1, 10)
        older = self.controller.flow_id
        self.controller.set_fan(2, 20)
        other = self.controller.flow_id
        self.controller.set_fan(1, 30)
        newer = self.controller.flow_id
        self.assertEqual(set(self.controller.fan_pending), {other, newer})
        self.controller._handle_frame(ack(older))
        self.assertEqual(self.controller.fan_status[1], "等待确认 30%")
        self.controller._handle_frame(ack(newer))
        self.assertEqual(self.controller.fan_status[1], "已确认 30%")

    def test_fan_timeout_no_retry_or_late_restore_after_tick(self):
        port = self.connect()
        self.controller.set_fan(1, 10)
        flow = self.controller.flow_id
        self.clock.advance(8)
        self.controller.tick()
        self.assertEqual(self.controller.fan_status[1], "10% 确认超时")
        self.assertEqual(len([f for f in port.writes if f[3] == P.MSG_SET_FAN_SPEED]), 1)
        self.controller._handle_frame(ack(flow))
        self.assertEqual(self.controller.fan_status[1], "10% 确认超时")

    def test_fan_event_before_timeout_scan_matches_legacy_order(self):
        self.connect()
        self.controller.set_fan(1, 10)
        self.clock.advance(8)
        self.controller.events.put(SerialEvent(self.controller.connection_generation, "frame",
                                               ack(self.controller.flow_id)))
        self.controller.tick()
        self.assertEqual(self.controller.fan_status[1], "已确认 10%")

    def test_matching_fan_error_does_not_affect_other_channel(self):
        self.connect()
        self.controller.set_fan(1, 40)
        flow = self.controller.flow_id
        self.controller.set_fan(2, 50)
        self.controller._handle_frame(response(P.MSG_ERROR, flow, b"\x05"))
        self.assertEqual(self.controller.fan_status[1], "失败 code=5")
        self.assertEqual(self.controller.fan_status[2], "等待确认 50%")

    def test_manual_read_forces_resample_and_is_allowed_during_fan_pending(self):
        port = self.connect()
        self.controller.set_fan(1, 10)
        self.clock.advance(1)
        self.controller.tick()
        self.assertEqual(len(port.writes), 1)
        self.controller.read_once()
        self.assertEqual(P.parse_packet(port.writes[-1])["data"], b"\x01")
        self.controller.read_once()
        self.assertEqual(len(port.writes), 2)

    def test_matching_telemetry_updates_six_fields_and_sound_zero(self):
        self.connect()
        self.sample()
        s = self.controller.snapshot()["telemetry"]
        self.assertEqual(s["values"], {"master_temp": "23.6 ℃", "slave_temp": "24.2 ℃",
                                       "master_humidity": "47.8 %RH", "slave_humidity": "51.3 %RH",
                                       "master_pressure": "101325 Pa", "slave_pressure": "100982 Pa"})
        self.assertEqual(s["sounds"], {"sound_rms_1": "0", "sound_rms_2": "9873"})
        self.assertEqual(s["updated_at"], "12:34:56")
        self.assertEqual(s["slave_link"], "从机链路：在线")
        self.assertEqual(self.stored_rows(), 1)

    def test_short_window_replaces_prior_high_value_including_zero(self):
        self.connect()
        for left, right in ((4294967294, 131071), (1, 2), (0, 0)):
            self.sample(telemetry_payload(left=left, right=right))
            self.assertEqual(self.controller.sounds, {"sound_rms_1": str(left), "sound_rms_2": str(right)})

    def test_offline_and_sensor_fault_are_independent(self):
        self.connect()
        self.sample(telemetry_payload(slave_temp=P.TEMPERATURE_INVALID))
        self.assertEqual(self.controller.values["slave_temp"], "--")
        self.assertEqual(self.controller.slave_link, "从机链路：在线")
        self.sample(telemetry_payload(flags=0x0B))
        self.assertEqual(self.controller.slave_link, "从机链路：离线")
        self.assertEqual(self.controller.values["master_temp"], "23.6 ℃")
        self.assertEqual(self.controller.values["slave_pressure"], "--")
        self.assertTrue(all(v == "--" for v in self.controller.sounds.values()))

    def test_legacy_sound_slots_never_display_as_current_stereo(self):
        self.connect()
        self.sample(telemetry_payload(flags=7))
        self.assertEqual(self.controller.values["slave_temp"], "24.2 ℃")
        self.assertTrue(all(v == "--" for v in self.controller.sounds.values()))
        self.sample(P.placeholder_payload())
        self.assertEqual(self.controller.slave_link, "从机链路：未知（旧布局）")
        self.assertTrue(all(v == "--" for v in self.controller.values.values()))

    def test_sound_sentinel_and_zero_are_distinct(self):
        self.connect()
        self.sample(telemetry_payload(left=P.UINT32_INVALID, right=0))
        self.assertEqual(self.controller.sounds, {"sound_rms_1": "--", "sound_rms_2": "0"})

    def test_wrong_flow_invalid_and_duplicate_frames_only_log(self):
        self.connect()
        self.controller.read_once()
        flow = self.controller.flow_id
        wrong = response(P.MSG_TELEMETRY, flow + 1, telemetry_payload())
        self.controller._handle_frame(wrong)
        damaged = bytearray(response(P.MSG_TELEMETRY, flow, telemetry_payload()))
        damaged[-1] ^= 1
        self.controller._handle_frame(bytes(damaged))
        self.assertIsNotNone(self.controller.telemetry_pending)
        self.assertEqual(self.stored_rows(), 0)
        valid = response(P.MSG_TELEMETRY, flow, telemetry_payload())
        self.controller._handle_frame(valid)
        self.controller._handle_frame(valid)
        self.assertEqual(self.stored_rows(), 1)
        self.assertTrue(any("丢弃: CRC mismatch" in row["text"] for row in self.controller.logs))

    def test_telemetry_response_before_deadline_is_accepted(self):
        self.connect()
        self.controller.read_once()
        flow = self.controller.flow_id
        self.clock.advance(4.999)
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, telemetry_payload()))
        self.assertEqual(self.stored_rows(), 1)

    def test_telemetry_response_at_deadline_clears_stale_and_backs_off(self):
        port = self.connect()
        self.sample()
        self.controller.read_once()
        flow = self.controller.flow_id
        self.clock.advance(5)
        self.controller._handle_frame(response(P.MSG_TELEMETRY, flow, telemetry_payload()))
        self.assertEqual(self.stored_rows(), 1)
        self.assertIsNone(self.controller.updated_at)
        self.assertEqual(self.controller.values["master_temp"], "--")
        self.assertEqual(self.controller.sounds["sound_rms_1"], "--")
        count = len(port.writes)
        self.controller.tick()
        self.assertEqual(len(port.writes), count)
        self.clock.advance(1)
        self.controller.tick()
        self.assertEqual(len(port.writes), count + 1)

    def test_telemetry_timeout_and_matching_error_clear_stale_data(self):
        self.connect()
        self.sample()
        self.controller.read_once()
        self.clock.advance(5)
        self.controller.tick()
        self.assertIsNone(self.controller.updated_at)
        self.assertIn("遥测应答超时", self.controller.logs[-1]["text"])
        self.sample()
        self.controller.read_once()
        self.controller._handle_frame(response(P.MSG_ERROR, self.controller.flow_id, b"\x07"))
        self.assertIsNone(self.controller.telemetry_pending)
        self.assertEqual(self.controller.sounds["sound_rms_2"], "--")

    def test_window_immediate_actions_and_exact_controls(self):
        port = self.connect()
        self.controller.set_window(1)
        self.assertEqual(P.parse_packet(port.writes[0])["data"], b"\x01\x01")
        self.assertEqual(self.controller.window_status, "开窗等待确认")
        controls = self.controller.snapshot()["controls"]
        self.assertFalse(any(controls.values()))
        self.controller.set_fan(1, 20)
        self.controller.read_once()
        self.controller.set_window(0)
        self.assertEqual(len(port.writes), 1)

    def test_window_queued_close_zero_waits_for_telemetry_and_both_fans(self):
        port = self.connect()
        self.controller.read_once()
        telemetry_flow = self.controller.flow_id
        self.controller.set_fan(1, 10)
        first_fan = self.controller.flow_id
        self.controller.set_fan(2, 20)
        second_fan = self.controller.flow_id
        self.controller.set_window(0)
        self.assertTrue(self.controller._window_busy())
        self.assertEqual(self.controller.window_queued_action, 0)
        self.controller._handle_frame(ack(first_fan))
        self.controller._handle_frame(ack(second_fan))
        self.controller.tick()
        self.assertEqual(len(port.writes), 3)
        self.controller._handle_frame(response(P.MSG_TELEMETRY, telemetry_flow, telemetry_payload()))
        self.controller.tick()
        self.assertEqual(P.parse_packet(port.writes[-1])["data"], b"\x01\x00")
        self.assertEqual(len(port.writes), 4)

    def test_queued_window_wait_does_not_start_ack_deadline(self):
        port = self.connect()
        self.controller.set_fan(1, 10)
        self.controller.set_window(0)
        self.clock.advance(8)
        self.controller.tick()
        self.assertEqual(self.controller.window_pending[2], self.clock.now)
        self.assertEqual(self.controller.window_status, "关窗等待确认")
        self.assertEqual(len([f for f in port.writes if f[3] == P.MSG_SET_WINDOW]), 1)

    def test_window_ack_statuses_and_no_claim_of_mechanical_completion(self):
        self.connect()
        expected = {0: "开窗启动PWM已确认（完成/停止状态未知）", 1: "开窗失败：舵机驱动故障",
                    2: "开窗确认超时，结果未知", 3: "开窗未执行：主机忙"}
        for status in range(4):
            self.controller.set_window(1)
            self.controller._handle_frame(ack(self.controller.flow_id, P.MSG_SET_WINDOW, status))
            self.assertEqual(self.controller.window_status, expected[status])
            self.assertFalse(self.controller._window_busy())
            self.assertTrue(self.controller.snapshot()["controls"]["window_enabled"])
            self.assertEqual(self.controller.next_telemetry_poll_at, self.clock.now + 1)

    def test_window_invalid_ack_flow_command_crc_status_and_direction(self):
        self.connect()
        self.controller.set_window(1)
        flow = self.controller.flow_id
        frames = [ack(flow + 1, P.MSG_SET_WINDOW), ack(flow, P.MSG_SET_FAN_SPEED),
                  ack(flow, P.MSG_SET_WINDOW, 4), P.build_packet(P.MSG_ACK, 3, 1, 1, 0, flow, b"\x11\0")]
        broken = bytearray(ack(flow, P.MSG_SET_WINDOW)); broken[-1] ^= 1
        frames.append(bytes(broken))
        for frame in frames:
            self.controller._handle_frame(frame)
            self.assertIsNotNone(self.controller.window_pending)
            self.assertEqual(self.controller.window_status, "开窗等待确认")

    def test_window_deadline_response_and_duplicate_cannot_restore_success(self):
        port = self.connect()
        self.controller.set_window(0)
        flow = self.controller.flow_id
        self.clock.advance(8)
        self.controller._handle_frame(ack(flow, P.MSG_SET_WINDOW))
        self.assertEqual(self.controller.window_status, "关窗确认超时，结果未知")
        self.controller._handle_frame(ack(flow, P.MSG_SET_WINDOW))
        self.controller.tick()
        self.assertEqual(len(port.writes), 1)
        self.assertEqual(self.controller.window_status, "关窗确认超时，结果未知")

    def test_window_before_deadline_ack_and_duplicate_only_logs(self):
        self.connect()
        self.controller.set_window(1)
        flow = self.controller.flow_id
        self.clock.advance(7.999)
        self.controller._handle_frame(ack(flow, P.MSG_SET_WINDOW))
        text = self.controller.window_status
        self.clock.advance(1)
        self.controller._handle_frame(ack(flow, P.MSG_SET_WINDOW, 1))
        self.assertEqual(self.controller.window_status, text)

    def test_window_tick_timeout_and_error_no_automatic_retry(self):
        port = self.connect()
        self.controller.set_window(1)
        flow = self.controller.flow_id
        self.controller._handle_frame(response(P.MSG_ERROR, flow + 1, b"\x09"))
        self.assertIsNotNone(self.controller.window_pending)
        self.controller._handle_frame(response(P.MSG_ERROR, flow, b"\x09"))
        self.assertEqual(self.controller.window_status, "开窗错误 code=9，结果未知")
        self.controller.set_window(0)
        self.clock.advance(8)
        self.controller.tick()
        self.assertEqual(self.controller.window_status, "关窗确认超时，结果未知")
        self.assertEqual(len([f for f in port.writes if f[3] == P.MSG_SET_WINDOW]), 2)

    def test_window_invalid_actions_and_disconnected_action_have_no_pending(self):
        for value in (True, False, 1.0, "1", -1, 2):
            with self.assertRaises(ValueError):
                self.controller.set_window(value)
        self.controller.set_window(0)
        self.assertEqual(self.controller.window_status, "未连接，未发送")
        self.assertFalse(self.controller._window_busy())

    def test_complete_write_time_starts_each_deadline_after_write(self):
        port = self.connect()
        def delayed(frame):
            self.clock.advance(2)
            return len(frame)
        port.on_write = delayed
        self.controller.set_fan(1, 50)
        self.assertEqual(self.controller.fan_pending[self.controller.flow_id][2], 102)
        self.controller._handle_frame(ack(self.controller.flow_id))
        self.controller.read_once()
        self.assertEqual(self.controller.telemetry_pending[1], 104)
        self.controller._handle_frame(response(P.MSG_ERROR, self.controller.flow_id, b"\x01"))
        self.controller.set_window(1)
        self.assertEqual(self.controller.window_pending[2], 106)

    def test_partial_write_disconnects_window_and_cannot_replay(self):
        port = self.connect()
        port.on_write = lambda frame: len(frame) - 1
        self.controller.set_window(1)
        self.assertIsNone(self.controller.serial_port)
        self.assertFalse(self.controller._window_busy())
        self.assertEqual(self.controller.window_status, "开窗发送失败，结果未知")
        self.assertTrue(port.closed)
        self.assertEqual(self.connect().writes, [])

    def test_disconnect_cancels_queued_pending_and_clears_data(self):
        self.connect()
        self.sample()
        self.controller.set_fan(1, 10)
        self.controller.set_window(0)
        self.controller.disconnect()
        self.assertEqual(self.controller.window_status, "已断开，待发动作已取消")
        self.assertEqual(self.controller.fan_pending, {})
        self.assertEqual(self.controller.values["master_temp"], "--")
        self.assertIsNone(self.controller.updated_at)
        port = self.connect()
        self.assertEqual(port.writes, [])
        self.controller.set_window(1)
        self.controller.disconnect()
        self.assertEqual(self.controller.window_status, "已断开，执行结果未知")

    def test_stale_reader_event_cannot_disconnect_or_restore_new_connection(self):
        self.connect()
        generation = self.controller.connection_generation
        self.controller.disconnect()
        current = self.connect()
        self.controller.events.put(SerialEvent(generation, "error", "obsolete reader"))
        self.controller.events.put(SerialEvent(generation, "frame", ack(1)))
        self.controller.tick()
        self.assertIs(self.controller.serial_port, current)
        self.assertFalse(any("obsolete reader" in row["text"] for row in self.controller.logs))

    def test_current_reader_error_disconnects_and_close_sends_no_commands(self):
        port = self.connect()
        self.controller.set_window(0)
        self.controller.events.put(SerialEvent(self.controller.connection_generation, "error", "unplugged"))
        self.controller.tick()
        self.assertIsNone(self.controller.serial_port)
        self.assertEqual(self.controller.window_status, "已断开，执行结果未知")
        self.assertEqual(len(port.writes), 1)

    def test_log_incremental_cursor_and_snapshot_do_not_expose_mutable_state(self):
        self.connect()
        first = self.controller.snapshot()
        self.controller.set_fan(1, 10)
        incremental = self.controller.snapshot(first["last_log_id"])
        self.assertEqual(len(incremental["logs"]), 1)
        self.assertTrue(incremental["logs"][0]["text"].startswith("12:34:56 TX AA 55"))
        incremental["fans"][0]["status"] = "injected"
        incremental["telemetry"]["values"]["master_temp"] = "injected"
        self.assertNotEqual(self.controller.fan_status[1], "injected")
        self.assertNotEqual(self.controller.values["master_temp"], "injected")


class SourceParityTests(unittest.TestCase):
    def test_wire_and_sql_classes_match_preserved_legacy_ast(self):
        baseline = ast.parse((UPPER_DIR / "legacy_tk.py").read_text(encoding="utf-8"))
        legacy = {node.name: ast.dump(node) for node in baseline.body if isinstance(node, ast.ClassDef)}
        for path in ("protocol.py", "storage.py"):
            tree = ast.parse((UPPER_DIR / "backend" / path).read_text(encoding="utf-8"))
            for node in tree.body:
                if isinstance(node, ast.ClassDef):
                    self.assertEqual(ast.dump(node), legacy[node.name])


class ServiceTests(unittest.TestCase):
    def await_snapshot(self, service, predicate, timeout=2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            snapshot = service.get_snapshot()
            if predicate(snapshot):
                return snapshot
            time.sleep(0.01)
        self.fail("controller did not reach the expected state")

    def test_demo_never_scans_real_ports_or_opens_real_serial_and_uses_memory_db(self):
        db_paths = []
        def db_factory(path):
            db_paths.append(path)
            return TelemetryDatabase(path)
        with patch("backend.controller.serial", Mock()) as real_serial:
            service = ControllerService(demo=True, db_path="DO_NOT_CREATE.db", database_factory=db_factory)
            service.start()
            try:
                api = DesktopAPI(service)
                s = api.get_snapshot()
                self.assertTrue(s["demo"])
                self.assertFalse(s["connected"])
                self.assertEqual(s["ports"], ["DEMO"])
                self.assertEqual(db_paths, [":memory:"])
                self.assertEqual(api.connect("DEMO"), {"accepted": True})
                self.await_snapshot(service, lambda s: s["connected"])
                api.read_once()
                s = self.await_snapshot(service, lambda s: s["telemetry"]["updated_at"] is not None)
                self.assertEqual(s["telemetry"]["sounds"]["sound_rms_1"], "0")
                real_serial.Serial.assert_not_called()
                real_serial.tools.list_ports.comports.assert_not_called()
            finally:
                service.close()

    def test_database_lifecycle_and_writes_belong_to_single_worker(self):
        calls = []
        class OwnedDatabase:
            def __init__(self, path):
                calls.append(("create", threading.get_ident(), path))
            def insert(self, *args):
                calls.append(("insert", threading.get_ident()))
            def close(self):
                calls.append(("close", threading.get_ident()))
        service = ControllerService(demo=True, database_factory=OwnedDatabase)
        main_id = threading.get_ident()
        service.start()
        api = DesktopAPI(service)
        try:
            api.connect("DEMO")
            self.await_snapshot(service, lambda s: s["connected"])
            api.read_once()
            self.await_snapshot(service, lambda s: s["telemetry"]["updated_at"] is not None)
        finally:
            service.close()
        self.assertEqual([item[0] for item in calls], ["create", "insert", "close"])
        self.assertEqual(len({item[1] for item in calls}), 1)
        self.assertNotEqual(calls[0][1], main_id)

    def test_bridge_success_is_enqueue_and_not_a_device_ack(self):
        service = ControllerService(demo=True)
        service.start()
        api = DesktopAPI(service)
        try:
            self.assertEqual(api.set_fan(1, 50), {"accepted": True})
            s = self.await_snapshot(service, lambda s: s["notice"] is not None)
            self.assertEqual(s["notice"]["title"], "未连接")
            self.assertEqual(s["fans"][0]["status"], "未发送")
        finally:
            service.close()
        self.assertEqual(api.set_fan(1, 20), {"accepted": False})

    def test_concurrent_bridge_commands_are_serialized_and_snapshots_isolated(self):
        ports, threads = [], []
        class RecordingSerial(FakeSerial):
            def write(self, frame):
                threads.append(threading.get_ident())
                return super().write(frame)
        def factory(*args, **kwargs):
            port = RecordingSerial(*args, **kwargs); ports.append(port); return port
        service = ControllerService(db_path=":memory:", serial_factory=factory, port_provider=lambda: ["MOCK"])
        service.start()
        api = DesktopAPI(service)
        try:
            api.connect("MOCK")
            self.await_snapshot(service, lambda s: s["connected"])
            submitters = [threading.Thread(target=api.set_fan, args=((i % 2) + 1, i)) for i in range(12)]
            for thread in submitters: thread.start()
            for thread in submitters: thread.join()
            self.await_snapshot(service, lambda s: len([row for row in s["logs"] if "TX " in row["text"]]) == 12)
            self.assertEqual(len(set(threads)), 1)
            self.assertNotEqual(threads[0], threading.get_ident())
            flows = [P.parse_packet(frame)["flow_id"] for frame in ports[0].writes]
            self.assertEqual(flows, list(range(1, 13)))
            s = api.get_snapshot()
            s["logs"][0]["text"] = "injected"
            self.assertNotEqual(api.get_snapshot()["logs"][0]["text"], "injected")
        finally:
            service.close()

    def test_close_cancels_queued_action_and_never_writes_extra_uart(self):
        ports = []
        def factory(*args, **kwargs):
            port = FakeSerial(*args, **kwargs); ports.append(port); return port
        service = ControllerService(db_path=":memory:", serial_factory=factory, port_provider=lambda: ["MOCK"])
        service.start()
        api = DesktopAPI(service)
        api.connect("MOCK")
        self.await_snapshot(service, lambda s: s["connected"])
        api.set_fan(1, 50)
        self.await_snapshot(service, lambda s: s["fans"][0]["status"] == "等待确认 50%")
        api.set_window(0)
        self.await_snapshot(service, lambda s: s["window"]["status"] == "关窗等待前序请求")
        service.close()
        s = api.get_snapshot()
        self.assertFalse(s["window"]["busy"])
        self.assertEqual(s["window"]["status"], "已断开，待发动作已取消")
        self.assertEqual(len(ports[0].writes), 1)
        self.assertTrue(ports[0].closed)
        self.assertFalse(service._thread.is_alive())

    def test_incremental_logs_have_no_duplicates_and_all_logs_retained(self):
        service = ControllerService(demo=True)
        service.start()
        api = DesktopAPI(service)
        try:
            api.connect("DEMO")
            first = self.await_snapshot(service, lambda s: s["connected"])
            api.read_once()
            final = self.await_snapshot(service, lambda s: s["telemetry"]["updated_at"] is not None)
            incremental = api.get_snapshot(first["last_log_id"])
            self.assertTrue(all(row["id"] > first["last_log_id"] for row in incremental["logs"]))
            self.assertEqual(len(final["logs"]), final["last_log_id"])
            self.assertEqual(api.get_snapshot(final["last_log_id"])["logs"], [])
        finally:
            service.close()

    def test_api_public_surface_contains_only_expected_methods(self):
        api = DesktopAPI(Mock())
        self.assertEqual({name for name in dir(api) if not name.startswith("_")},
                         {"refresh_ports", "connect", "disconnect", "read_once", "set_fan", "set_window", "get_snapshot"})

    def test_close_during_command_does_not_run_a_final_tick(self):
        entered, release = threading.Event(), threading.Event()
        tick_calls = []
        class BlockingController(Controller):
            def read_once(self):
                entered.set()
                release.wait(2)
            def tick(self):
                tick_calls.append(threading.get_ident())
                super().tick()
        service = ControllerService(demo=True, controller_factory=BlockingController)
        service.start()
        DesktopAPI(service).read_once()
        self.assertTrue(entered.wait(1))
        before = len(tick_calls)
        closer = threading.Thread(target=service.close)
        closer.start()
        self.assertTrue(service._closing.wait(1))
        release.set()
        closer.join(2)
        self.assertFalse(closer.is_alive())
        self.assertEqual(len(tick_calls), before)


if __name__ == "__main__":
    unittest.main()
