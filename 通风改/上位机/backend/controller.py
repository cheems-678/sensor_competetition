"""UI-independent state machine. All mutation belongs to one controller thread."""

from __future__ import annotations

import math
import queue
import threading
import time
from datetime import datetime
from typing import Callable

from .protocol import FrameStreamParser, LoRaProtocol

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # Pure protocol/controller tests do not require pyserial.
    serial = None


class SerialEvent:
    def __init__(self, generation: int, kind: str, value: object, received_at: float | None = None):
        self.generation = generation
        self.kind = kind
        self.value = value
        self.received_at = received_at


class Controller:
    SERVO_PINS = {1: "PB8", 2: "PB9", 3: "PB10", 4: "PB11"}
    FAN_PINS = {1: "PB1", 2: "PB8", 3: "PA1", 4: "PB9"}
    FAN_ACK_TIMEOUT_S = 8.0
    WINDOW_ACK_TIMEOUT_S = 8.0
    TELEMETRY_POLL_INTERVAL_S = 1.0
    TELEMETRY_TIMEOUT_S = 5.0
    FIELD_LABELS = (
        ("master_temp", "master_bme_temperature_c", "℃"),
        ("slave_temp", "slave_bme_temperature_c", "℃"),
        ("master_humidity", "master_bme_humidity_pct", "%RH"),
        ("slave_humidity", "slave_bme_humidity_pct", "%RH"),
        ("master_pressure", "master_bme_pressure_pa", "Pa"),
        ("slave_pressure", "slave_bme_pressure_pa", "Pa"),
    )
    SOUND_KEYS = ("sound_rms_1", "sound_rms_2")

    def __init__(
        self,
        database,
        *,
        serial_factory: Callable | None = None,
        port_provider: Callable | None = None,
        clock: Callable = time.monotonic,
        wall_clock: Callable = datetime.now,
        demo: bool = False,
        start_readers: bool = True,
        closing: Callable | None = None,
    ):
        self.database = database
        self.serial_factory = serial_factory if serial_factory is not None else (
            serial.Serial if serial is not None else None)
        self.port_provider = port_provider if port_provider is not None else (
            (lambda: [port.device for port in serial.tools.list_ports.comports()])
            if serial is not None else (lambda: []))
        self.clock, self.wall_clock = clock, wall_clock
        self.demo, self.start_readers = demo, start_readers
        self._closing = closing if closing is not None else (lambda: False)
        self.serial_port = None
        self.reader_thread = None
        self.stop_event = threading.Event()
        self.events: queue.Queue[SerialEvent] = queue.Queue()
        self.connection_generation = 0
        self.flow_id = 0
        self.ports: list[str] = []
        self.port = ""
        self.values = {key: "--" for key, _, _ in self.FIELD_LABELS}
        self.sounds = {key: "--" for key in self.SOUND_KEYS}
        self.rain = {"state": None, "source": None}
        self.mq2 = self._empty_mq2()
        self._mq2_received_at = None
        self._mq2_source_age = None
        self.ultrasonic = self._empty_ultrasonic()
        self._ultrasonic_received_at = self._ultrasonic_source_age = None
        self.slave_link = "从机链路：未知"
        self.last_telemetry_at = None
        self.updated_at = None
        self.sample_id = 0
        self.duties = {channel: 0 for channel in self.FAN_PINS}
        self.fan_status = {channel: "未发送" for channel in self.FAN_PINS}
        self.fan_pending = {}
        self.telemetry_pending = None
        self.window_queued_action = None
        self.window_pending = None
        self.window_status = "未连接，位置未知"
        self.window_servo_id = 1
        self.window_statuses = {channel: self.window_status for channel in self.SERVO_PINS}
        self.next_telemetry_poll_at = 0.0
        self.logs: list[dict] = []
        self.notice = None
        self._notice_id = 0
        self.revision = 0

    def _changed(self):
        self.revision += 1

    def _append_log(self, text: str):
        self.logs.append({"id": len(self.logs) + 1,
                          "text": f"{self.wall_clock():%H:%M:%S} {text}"})
        self._changed()

    def _show_notice(self, title: str, message: str):
        self._notice_id += 1
        self.notice = {"id": self._notice_id, "title": title, "message": message}
        self._changed()

    def _next_flow(self) -> int:
        self.flow_id = (self.flow_id + 1) & 0xFFFF
        return self.flow_id

    def refresh_ports(self):
        self.ports = list(self.port_provider())
        if self.ports and not self.port:
            self.port = self.ports[0]
        self._changed()

    def connect(self, port: str):
        if self.serial_port is not None:
            return
        self.port = str(port)
        if self.serial_factory is None:
            self._show_notice("缺少依赖", "请安装 pyserial")
            return
        try:
            new_port = self.serial_factory(self.port, 115200, timeout=0.1, write_timeout=0.5)
        except Exception as exc:
            self._show_notice("连接失败", str(exc))
            return
        self.serial_port = new_port
        self.connection_generation += 1
        self.stop_event = threading.Event()
        parser = FrameStreamParser()
        self.telemetry_pending = None
        self.window_queued_action = None
        self.window_pending = None
        self.window_status = "已连接，尚未发送，位置未知"
        self.window_statuses = {channel: self.window_status for channel in self.SERVO_PINS}
        self.next_telemetry_poll_at = self.clock() + self.TELEMETRY_POLL_INTERVAL_S
        if self.start_readers:
            self.reader_thread = threading.Thread(
                target=self._reader,
                args=(new_port, parser, self.stop_event, self.connection_generation),
                name="LoRa serial reader", daemon=True)
            self.reader_thread.start()
        self._append_log(f"已连接 {self.port}")

    def disconnect(self):
        if self.window_pending is not None:
            self.window_status = "已断开，执行结果未知"
        elif self.window_queued_action is not None:
            self.window_status = "已断开，待发动作已取消"
        elif self.serial_port is not None:
            self.window_status = "已断开，位置未知"
        self.window_statuses = {channel: "已断开，位置未知" for channel in self.SERVO_PINS}
        self.window_statuses[self.window_servo_id] = self.window_status
        self.window_queued_action = None
        self.window_pending = None
        self.fan_pending.clear()
        self.telemetry_pending = None
        self.next_telemetry_poll_at = 0.0
        self._clear_telemetry()
        for channel in self.fan_status:
            self.fan_status[channel] = "已断开，状态未知"
        self.stop_event.set()
        self.connection_generation += 1
        port, self.serial_port = self.serial_port, None
        if port is not None:
            try:
                port.close()
            except Exception as exc:
                self._append_log("串口关闭失败: " + str(exc))
        reader, self.reader_thread = self.reader_thread, None
        if reader is not None and reader is not threading.current_thread():
            reader.join(timeout=0.3)
        self._changed()

    def _reader(self, port, parser: FrameStreamParser, stop: threading.Event, generation: int):
        # Capture all connection objects; an old reader must never consume a new port.
        while not stop.is_set():
            try:
                chunk = port.read(port.in_waiting or 1)
                if stop.is_set():
                    break
                for frame in parser.feed(chunk):
                    self.events.put(SerialEvent(generation, "frame", frame, self.clock()))
            except Exception as exc:
                if not stop.is_set():
                    self.events.put(SerialEvent(generation, "error", str(exc)))
                break

    def _send(self, frame: bytes):
        if self._closing():
            return False
        if self.serial_port is None:
            self._show_notice("未连接", "请先连接控制室串口")
            return False
        try:
            if self.serial_port.write(frame) != len(frame):
                raise IOError("串口未发送完整帧")
        except Exception as exc:
            self._append_log(f"发送失败: {exc}")
            self.disconnect()
            return False
        self._append_log("TX " + frame.hex(" ").upper())
        return True

    def _clear_telemetry(self):
        self.values = {key: "--" for key in self.values}
        self.sounds = {key: "--" for key in self.sounds}
        self.rain = {"state": None, "source": None}
        self.mq2 = self._empty_mq2()
        self._mq2_received_at = None
        self._mq2_source_age = None
        self.ultrasonic = self._empty_ultrasonic()
        self._ultrasonic_received_at = self._ultrasonic_source_age = None
        self.slave_link = "从机链路：未知"
        self.last_telemetry_at = None
        self.updated_at = None
        self._changed()

    @staticmethod
    def _empty_mq2():
        return dict(valid=False, raw=None, pa7_mv=None, ao_mv=None, age_ms=None)

    def _refresh_mq2(self, now: float):
        if not self.mq2["valid"]:
            return
        age = self._mq2_source_age + max(0, int((now - self._mq2_received_at) * 1000))
        if age >= LoRaProtocol.MQ2_MAX_AGE_MS:
            self.mq2 = self._empty_mq2()
            self._mq2_received_at = self._mq2_source_age = None
            self._changed()
        elif age != self.mq2["age_ms"]:
            self.mq2["age_ms"] = age
            self._changed()

    @staticmethod
    def _empty_ultrasonic():
        return dict(valid=False, distance_mm=None, raw_mm=None, pulse_us=None, age_ms=None)

    def _refresh_ultrasonic(self, now: float):
        if not self.ultrasonic["valid"]:
            return
        age = self._ultrasonic_source_age + max(0, int((now - self._ultrasonic_received_at) * 1000))
        if age >= LoRaProtocol.ULTRASONIC_MAX_AGE_MS:
            self.ultrasonic = self._empty_ultrasonic()
            self._ultrasonic_received_at = self._ultrasonic_source_age = None
            self._changed()
        elif age != self.ultrasonic["age_ms"]:
            self.ultrasonic["age_ms"] = age
            self._changed()

    def read_once(self):
        self._request_telemetry(True)

    def _request_telemetry(self, force_resample: bool = True):
        if self._window_busy():
            self._append_log("窗户命令等待完成，暂停新增遥测请求")
            return
        if self.telemetry_pending is not None:
            self._append_log("遥测请求等待应答，暂不重复发送")
            return
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_read_telemetry(flow, force_resample)):
            now = self.clock()
            self.telemetry_pending = (flow, now)
            self.next_telemetry_poll_at = now + self.TELEMETRY_POLL_INTERVAL_S

    def _poll_telemetry(self, now: float):
        if self.serial_port is None:
            return
        if self.telemetry_pending is not None:
            flow, sent_at = self.telemetry_pending
            if now - sent_at >= self.TELEMETRY_TIMEOUT_S:
                self.telemetry_pending = None
                self.next_telemetry_poll_at = now + self.TELEMETRY_POLL_INTERVAL_S
                self._clear_telemetry()
                self._append_log(f"遥测应答超时 flow={flow}")
            return
        if not self._window_busy() and not self.fan_pending and now >= self.next_telemetry_poll_at:
            self._request_telemetry(False)

    def _window_busy(self) -> bool:
        return self.window_queued_action is not None or self.window_pending is not None

    @staticmethod
    def _window_action_text(action: int) -> str:
        return "开窗" if action == 1 else "关窗"

    def set_window(self, action: int, servo_id: int = 1):
        if type(action) is not int or action not in LoRaProtocol.WINDOW_ACTIONS:
            raise ValueError("window action must be 0 or 1")
        if type(servo_id) is not int or servo_id not in self.SERVO_PINS:
            raise ValueError("servo id must be 1..4")
        if self.serial_port is None:
            self.window_status = "未连接，未发送"
            self.window_statuses[servo_id] = self.window_status
            self._changed()
            return
        if self._window_busy():
            self._append_log("窗户命令等待完成，不重复发送")
            return
        self.window_servo_id = servo_id
        self.window_queued_action = action
        self.window_status = f"{self._window_action_text(action)}等待前序请求"
        self.window_statuses[servo_id] = self.window_status
        self._changed()
        self._service_window(self.clock())

    def _finish_window(self, text: str, now: float):
        self.window_pending = None
        self.window_status = text
        self.window_statuses[self.window_servo_id] = text
        self.next_telemetry_poll_at = now + self.TELEMETRY_POLL_INTERVAL_S
        self._changed()

    def _expire_window(self, now: float) -> bool:
        if self.window_pending is None or now - self.window_pending[2] < self.WINDOW_ACK_TIMEOUT_S:
            return False
        flow, action, _ = self.window_pending
        self._finish_window(f"{self._window_action_text(action)}确认超时，结果未知", now)
        self._append_log(f"窗户确认超时 flow={flow}；不自动重试")
        return True

    def _service_window(self, now: float):
        if self._closing():
            return
        self._expire_window(now)
        if (self.serial_port is None or self.window_pending is not None
                or self.window_queued_action is None or self.telemetry_pending is not None
                or self.fan_pending):
            return
        action, self.window_queued_action = self.window_queued_action, None
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_set_window(flow, action, self.window_servo_id)):
            self.window_pending = (flow, action, self.clock())
            self.window_status = f"{self._window_action_text(action)}等待确认"
        else:
            self.window_status = f"{self._window_action_text(action)}发送失败，结果未知"
        self.window_statuses[self.window_servo_id] = self.window_status
        self._changed()

    @staticmethod
    def _normalize_fan_duty(value) -> int:
        number = float(value)
        if not math.isfinite(number) or not 0 <= number <= 100:
            raise ValueError("占空比必须在 0–100 之间")
        return int(number + 0.5)

    def set_fan(self, channel: int, value):
        if self._window_busy():
            self._append_log("窗户命令等待完成，暂停新增风机请求")
            return
        if channel not in LoRaProtocol.FAN_CHANNELS:
            self._show_notice("通道已停用", "仅支持风机1/PB1、风机2/PB8")
            return
        try:
            duty = self._normalize_fan_duty(value)
        except (ValueError, TypeError, OverflowError):
            self._show_notice("输入错误", "请输入 0–100 的占空比")
            return
        self.duties[channel] = duty
        self._changed()
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_set_fan_speed(flow, channel, duty)):
            self.fan_pending = {key: item for key, item in self.fan_pending.items()
                                if item[0] != channel}
            self.fan_pending[flow] = (channel, duty, self.clock())
            self.fan_status[channel] = f"等待确认 {duty}%"
            self._changed()

    def tick(self):
        if self._closing():
            return
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            if event.generation != self.connection_generation:
                continue
            if event.kind == "frame":
                self._handle_frame(event.value, event.received_at)
            else:
                self._append_log("串口错误: " + str(event.value))
                self.disconnect()
        for flow, (channel, duty, sent_at) in list(self.fan_pending.items()):
            if self.clock() - sent_at >= self.FAN_ACK_TIMEOUT_S:
                del self.fan_pending[flow]
                self.fan_status[channel] = f"{duty}% 确认超时"
                self._changed()
        now = self.clock()
        self._refresh_mq2(now)
        self._refresh_ultrasonic(now)
        self._poll_telemetry(now)
        self._service_window(now)

    def _handle_frame(self, frame: bytes, received_at: float | None = None):
        self._append_log("RX " + frame.hex(" ").upper())
        try:
            packet = LoRaProtocol.parse_packet(frame)
        except ValueError as exc:
            self._append_log("丢弃: " + str(exc))
            return
        if packet["type"] == LoRaProtocol.MSG_TELEMETRY:
            if self.telemetry_pending is None or self.telemetry_pending[0] != packet["flow_id"]:
                return
            now = self.clock()
            if now - self.telemetry_pending[1] >= self.TELEMETRY_TIMEOUT_S:
                self.telemetry_pending = None
                self.next_telemetry_poll_at = now + self.TELEMETRY_POLL_INTERVAL_S
                self._clear_telemetry()
                return
            self.telemetry_pending = None
            values = LoRaProtocol.decode_telemetry(packet["data"])
            self.mq2 = dict(values["mq2"])
            self._mq2_received_at = now if received_at is None else received_at
            self._mq2_source_age = self.mq2["age_ms"]
            self._refresh_mq2(now)
            self.ultrasonic = dict(values["ultrasonic"])
            self._ultrasonic_received_at = now if received_at is None else received_at
            self._ultrasonic_source_age = self.ultrasonic["age_ms"]
            self._refresh_ultrasonic(now)
            self.rain = {"state": values["rain_state"] if values["rain_source"] == "master" else None,
                         "source": values["rain_source"]}
            online = values["slave_online"]
            self.slave_link = "从机链路：" + (
                "未知（旧布局）" if online is None else "在线" if online else "离线")
            self.database.insert(packet["flow_id"], values, frame)
            self.last_telemetry_at = now
            self.updated_at = self.wall_clock().strftime("%H:%M:%S")
            self.sample_id += 1
            for key, name, unit in self.FIELD_LABELS:
                value = values[name]
                self.values[key] = "--" if value is None else f"{value:g} {unit}"
            for key in self.SOUND_KEYS:
                value = values[key] if len(packet["data"]) in (LoRaProtocol.TELEMETRY_SIZE, LoRaProtocol.MQ2_TELEMETRY_SIZE, LoRaProtocol.ULTRASONIC_TELEMETRY_SIZE) else None
                self.sounds[key] = "--" if value is None else str(value)
            self._changed()
        elif packet["type"] == LoRaProtocol.MSG_ACK:
            self._append_log(f"ACK command=0x{packet['data'][0]:02X} status={packet['data'][1]}")
            if packet["data"][0] == LoRaProtocol.MSG_SET_WINDOW:
                if self.window_pending is None or self.window_pending[0] != packet["flow_id"]:
                    return
                now = self.clock()
                if self._expire_window(now):
                    return
                action = self._window_action_text(self.window_pending[1])
                messages = {
                    0: f"{action}启动PWM已确认（完成/停止状态未知）",
                    1: f"{action}失败：舵机驱动故障",
                    2: f"{action}确认超时，结果未知",
                    3: f"{action}未执行：主机忙",
                }
                self._finish_window(messages[packet["data"][1]], now)
            elif packet["data"][0] == LoRaProtocol.MSG_SET_FAN_SPEED:
                pending = self.fan_pending.pop(packet["flow_id"], None)
                if pending is not None:
                    channel, duty, _ = pending
                    self.fan_status[channel] = (
                        f"已确认 {duty}%" if packet["data"][1] == 0 else "主机拒绝执行")
                    self._changed()
        elif packet["type"] == LoRaProtocol.MSG_ERROR:
            self._append_log(f"ERROR code={packet['data'][0]}")
            if self.window_pending is not None and self.window_pending[0] == packet["flow_id"]:
                now = self.clock()
                if not self._expire_window(now):
                    action = self._window_action_text(self.window_pending[1])
                    self._finish_window(f"{action}错误 code={packet['data'][0]}，结果未知", now)
            if self.telemetry_pending is not None and self.telemetry_pending[0] == packet["flow_id"]:
                self.telemetry_pending = None
                self.next_telemetry_poll_at = self.clock() + self.TELEMETRY_POLL_INTERVAL_S
                self._clear_telemetry()
            pending = self.fan_pending.pop(packet["flow_id"], None)
            if pending is not None:
                self.fan_status[pending[0]] = f"失败 code={packet['data'][0]}"
                self._changed()

    def snapshot(self, after_log_id: int = 0) -> dict:
        busy = self._window_busy()
        return {
            "revision": self.revision, "demo": self.demo,
            "connected": self.serial_port is not None, "port": self.port, "ports": list(self.ports),
            "controls": {"read_enabled": not busy, "fan_enabled": not busy,
                         "window_enabled": self.serial_port is not None and not busy},
            "telemetry": {"values": dict(self.values), "sounds": dict(self.sounds),
                          "rain": dict(self.rain),
                          "mq2": dict(self.mq2),
                          "ultrasonic": dict(self.ultrasonic),
                          "slave_link": self.slave_link, "updated_at": self.updated_at,
                          "sample_id": self.sample_id},
            "fans": [{"channel": ch, "pin": pin, "duty": self.duties[ch],
                      "status": self.fan_status[ch]} for ch, pin in self.FAN_PINS.items()],
            "window": {"busy": busy, "status": self.window_status},
            "windows": [{"channel": ch, "pin": pin, "status": self.window_statuses[ch]}
                        for ch, pin in self.SERVO_PINS.items()],
            "logs": [dict(item) for item in self.logs if item["id"] > after_log_id],
            "last_log_id": len(self.logs), "notice": dict(self.notice) if self.notice else None,
        }

    def close(self):
        self.disconnect()
        self.database.close()
