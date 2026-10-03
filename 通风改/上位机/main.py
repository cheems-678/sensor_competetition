"""单组 LoRa 通风监控上位机（协议 v4）。"""

from __future__ import annotations

import queue
import math
import sqlite3
import struct
import threading
import time
import tkinter as tk
from datetime import datetime
from tkinter import messagebox, ttk

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # 测试协议时不强制依赖 pyserial
    serial = None


class LoRaProtocol:
    HEAD = b"\xAA\x55"
    VERSION = 0x04
    MIN_FRAME_SIZE = 13
    MAX_PAYLOAD_SIZE = 128

    ROLE_CONTROL = 0x01
    ROLE_MASTER = 0x02
    ROLE_SLAVE = 0x03

    MSG_READ_TELEMETRY = 0x01
    MSG_TELEMETRY = 0x02
    MSG_SET_FAN_SPEED = 0x10
    MSG_SET_WINDOW = 0x11
    MSG_ACK = 0x20
    MSG_ERROR = 0x7E

    LEGACY_TELEMETRY_SIZE = 18
    TELEMETRY_SIZE = 26
    FLAG_MASTER_BME = 0x01
    FLAG_DUAL_BME = 0x02
    FLAG_SLAVE_ONLINE = 0x04
    FLAG_ACOUSTIC_EXTENSION = 0x08
    SINGLE_GROUP = 1
    FAN_CHANNELS = (1, 2)
    WINDOW_SERVO = 1
    WINDOW_ACTIONS = (0, 1)

    TEMPERATURE_INVALID = -32768
    UINT16_INVALID = 0xFFFF
    UINT32_INVALID = 0xFFFFFFFF
    RAIN_INVALID = 0xFF

    TYPE_NAMES = {
        MSG_READ_TELEMETRY: "READ_TELEMETRY",
        MSG_TELEMETRY: "TELEMETRY",
        MSG_SET_FAN_SPEED: "SET_FAN_SPEED",
        MSG_SET_WINDOW: "SET_WINDOW",
        MSG_ACK: "ACK",
        MSG_ERROR: "ERROR",
    }

    @staticmethod
    def crc16(data: bytes) -> int:
        crc = 0xFFFF
        for value in data:
            crc ^= value
            for _ in range(8):
                crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
        return crc & 0xFFFF

    @classmethod
    def build_packet(
        cls,
        msg_type: int,
        sender_role: int,
        sender_group: int,
        recv_role: int,
        recv_group: int,
        flow_id: int,
        data: bytes = b"",
    ) -> bytes:
        if len(data) > cls.MAX_PAYLOAD_SIZE:
            raise ValueError("payload too long")
        if not 0 <= flow_id <= 0xFFFF:
            raise ValueError("flow_id must be 0..65535")
        header = struct.pack(
            "<BBBBBBHB",
            cls.VERSION,
            msg_type,
            sender_role,
            sender_group,
            recv_role,
            recv_group,
            flow_id,
            len(data),
        )
        body = header + data
        return cls.HEAD + body + struct.pack("<H", cls.crc16(body))

    @classmethod
    def _validate(cls, packet: dict) -> None:
        msg_type = packet["type"]
        payload = packet["data"]
        source = (packet["sender_role"], packet["sender_group"])
        target = (packet["recv_role"], packet["recv_group"])

        if msg_type == cls.MSG_READ_TELEMETRY:
            valid = len(payload) == 1 and payload[0] <= 1
            direction = source == (cls.ROLE_CONTROL, 0) and target == (
                cls.ROLE_MASTER,
                cls.SINGLE_GROUP,
            )
        elif msg_type == cls.MSG_TELEMETRY:
            cls._validate_telemetry_payload(payload)
            valid = True
            direction = source == (cls.ROLE_MASTER, cls.SINGLE_GROUP) and target == (
                cls.ROLE_CONTROL,
                0,
            )
        elif msg_type == cls.MSG_SET_FAN_SPEED:
            valid = (
                len(payload) == 2
                and payload[0] in cls.FAN_CHANNELS
                and payload[1] <= 100
            )
            direction = source == (cls.ROLE_CONTROL, 0) and target == (
                cls.ROLE_MASTER,
                cls.SINGLE_GROUP,
            )
        elif msg_type == cls.MSG_SET_WINDOW:
            valid = (len(payload) == 2 and payload[0] == cls.WINDOW_SERVO
                     and payload[1] in cls.WINDOW_ACTIONS)
            direction = source == (cls.ROLE_CONTROL, 0) and target == (
                cls.ROLE_MASTER,
                cls.SINGLE_GROUP,
            )
        elif msg_type == cls.MSG_ACK:
            valid = len(payload) == 2
            if valid and payload[0] == cls.MSG_SET_WINDOW:
                valid = payload[1] in (0, 1, 2, 3)
            direction = source == (cls.ROLE_MASTER, cls.SINGLE_GROUP) and target == (
                cls.ROLE_CONTROL,
                0,
            )
        elif msg_type == cls.MSG_ERROR:
            valid = 1 <= len(payload) <= 16
            direction = True
        else:
            raise ValueError(f"unsupported message type 0x{msg_type:02X}")
        if not valid:
            raise ValueError("invalid payload")
        if not direction:
            raise ValueError("invalid direction or group")

    @classmethod
    def parse_packet(cls, frame: bytes, strict: bool = True) -> dict:
        if len(frame) < cls.MIN_FRAME_SIZE:
            raise ValueError("frame too short")
        if frame[:2] != cls.HEAD:
            raise ValueError("invalid frame header")
        if frame[2] != cls.VERSION:
            raise ValueError(f"unsupported protocol version {frame[2]}")
        payload_length = frame[10]
        expected_length = cls.MIN_FRAME_SIZE + payload_length
        if len(frame) != expected_length:
            raise ValueError("frame length mismatch")
        expected_crc = struct.unpack_from("<H", frame, len(frame) - 2)[0]
        actual_crc = cls.crc16(frame[2:-2])
        if expected_crc != actual_crc:
            raise ValueError("CRC mismatch")
        packet = {
            "version": frame[2],
            "type": frame[3],
            "sender_role": frame[4],
            "sender_group": frame[5],
            "recv_role": frame[6],
            "recv_group": frame[7],
            "flow_id": struct.unpack_from("<H", frame, 8)[0],
            "data": bytes(frame[11:-2]),
        }
        if strict:
            cls._validate(packet)
        return packet

    @classmethod
    def cmd_read_telemetry(cls, flow_id: int, force_resample: bool = False) -> bytes:
        return cls.build_packet(
            cls.MSG_READ_TELEMETRY,
            cls.ROLE_CONTROL,
            0,
            cls.ROLE_MASTER,
            cls.SINGLE_GROUP,
            flow_id,
            bytes((1 if force_resample else 0,)),
        )

    @classmethod
    def cmd_set_fan_speed(cls, flow_id: int, channel: int, duty: int) -> bytes:
        if channel not in cls.FAN_CHANNELS:
            raise ValueError("fan channel must be 1 or 2")
        if not 0 <= duty <= 100:
            raise ValueError("fan duty must be 0..100")
        return cls.build_packet(
            cls.MSG_SET_FAN_SPEED,
            cls.ROLE_CONTROL,
            0,
            cls.ROLE_MASTER,
            cls.SINGLE_GROUP,
            flow_id,
            bytes((channel, duty)),
        )

    @classmethod
    def cmd_set_window(cls, flow_id: int, action: int) -> bytes:
        if type(action) is not int or action not in cls.WINDOW_ACTIONS:
            raise ValueError("window action must be 0 (close) or 1 (open)")
        return cls.build_packet(
            cls.MSG_SET_WINDOW,
            cls.ROLE_CONTROL,
            0,
            cls.ROLE_MASTER,
            cls.SINGLE_GROUP,
            flow_id,
            bytes((cls.WINDOW_SERVO, action)),
        )

    @classmethod
    def placeholder_payload(cls) -> bytes:
        """Return the historical 18-byte placeholder layout, not the extension."""
        return struct.pack(
            "<BhHIHHBhH",
            0,
            cls.TEMPERATURE_INVALID,
            cls.UINT16_INVALID,
            cls.UINT32_INVALID,
            cls.UINT16_INVALID,
            cls.UINT16_INVALID,
            cls.RAIN_INVALID,
            cls.TEMPERATURE_INVALID,
            cls.UINT16_INVALID,
        )

    @classmethod
    def _validate_telemetry_payload(cls, payload: bytes) -> None:
        if len(payload) == cls.LEGACY_TELEMETRY_SIZE:
            valid = payload[0] in (0x00, 0x01, 0x03, 0x07)
        elif len(payload) == cls.TELEMETRY_SIZE:
            valid = payload[0] in (0x0B, 0x0F)
        else:
            raise ValueError("invalid telemetry payload length")
        if not valid:
            raise ValueError("invalid telemetry payload layout")

    @classmethod
    def decode_telemetry(cls, payload: bytes) -> dict:
        cls._validate_telemetry_payload(payload)
        extended = len(payload) == cls.TELEMETRY_SIZE
        # Decode the historical wire slots first; replace them for dual BME.
        values = struct.unpack("<BhHIHHBhH", payload[:cls.LEGACY_TELEMETRY_SIZE])

        def temperature(value: int):
            return None if value == cls.TEMPERATURE_INVALID else value / 10.0

        def humidity(value: int):
            return None if value == cls.UINT16_INVALID else value / 10.0

        def u16(value: int):
            return None if value == cls.UINT16_INVALID else value

        decoded = {
            "flags": values[0],
            # Legacy database names identify wire slots, not sensor ownership.
            "slave_temperature_c": temperature(values[1]),
            "slave_humidity_pct": humidity(values[2]),
            "slave_pressure_pa": None if values[3] == cls.UINT32_INVALID else values[3],
            "sound_rms_1": u16(values[4]),
            "sound_rms_2": u16(values[5]),
            "rain_state": None if values[6] == cls.RAIN_INVALID else values[6],
            "master_temperature_c": temperature(values[7]),
            "master_humidity_pct": humidity(values[8]),
        }
        dual_bme = decoded["flags"] in (0x03, 0x07, 0x0B, 0x0F)
        master_bme = decoded["flags"] == cls.FLAG_MASTER_BME or dual_bme
        decoded.update(
            master_bme_temperature_c=decoded["slave_temperature_c"] if master_bme else None,
            master_bme_humidity_pct=decoded["slave_humidity_pct"] if master_bme else None,
            master_bme_pressure_pa=decoded["slave_pressure_pa"] if master_bme else None,
            slave_bme_temperature_c=None,
            slave_bme_humidity_pct=None,
            slave_bme_pressure_pa=None,
            slave_online=None,
        )
        if dual_bme:
            slave_t, slave_h, slave_p = struct.unpack_from("<hHI", payload, 9)
            online = bool(decoded["flags"] & cls.FLAG_SLAVE_ONLINE)
            decoded.update(
                slave_online=online,
                slave_bme_temperature_c=temperature(slave_t) if online else None,
                slave_bme_humidity_pct=humidity(slave_h) if online else None,
                slave_bme_pressure_pa=slave_p if online and slave_p != cls.UINT32_INVALID else None,
                sound_rms_1=None, sound_rms_2=None, rain_state=None,
                master_temperature_c=None, master_humidity_pct=None,
            )
            if extended and online:
                sound_left, sound_right = struct.unpack_from("<II", payload, 18)
                decoded.update(
                    sound_rms_1=None if sound_left == cls.UINT32_INVALID else sound_left,
                    sound_rms_2=None if sound_right == cls.UINT32_INVALID else sound_right,
                )
        return decoded


class FrameStreamParser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self.buffer.extend(data)
        frames = []
        while True:
            start = self.buffer.find(LoRaProtocol.HEAD)
            if start < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1:] == b"\xAA" else b""
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 11:
                break
            frame_length = LoRaProtocol.MIN_FRAME_SIZE + self.buffer[10]
            if frame_length > LoRaProtocol.MIN_FRAME_SIZE + LoRaProtocol.MAX_PAYLOAD_SIZE:
                del self.buffer[0]
                continue
            if len(self.buffer) < frame_length:
                break
            frames.append(bytes(self.buffer[:frame_length]))
            del self.buffer[:frame_length]
        return frames


class TelemetryDatabase:
    # Existing columns store wire slots; flags and raw_frame retain provenance.
    def __init__(self, path: str = "sensor_data.db"):
        self.connection = sqlite3.connect(path, check_same_thread=False)
        self.connection.execute(
            """
            CREATE TABLE IF NOT EXISTS telemetry_v4 (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                flow_id INTEGER NOT NULL,
                flags INTEGER NOT NULL,
                slave_temperature_c REAL,
                slave_humidity_pct REAL,
                slave_pressure_pa INTEGER,
                sound_rms_1 INTEGER,
                sound_rms_2 INTEGER,
                rain_state INTEGER,
                master_temperature_c REAL,
                master_humidity_pct REAL,
                raw_frame TEXT NOT NULL
            )
            """
        )
        self.connection.commit()

    def insert(self, flow_id: int, values: dict, raw_frame: bytes) -> None:
        columns = (
            "slave_temperature_c",
            "slave_humidity_pct",
            "slave_pressure_pa",
            "sound_rms_1",
            "sound_rms_2",
            "rain_state",
            "master_temperature_c",
            "master_humidity_pct",
        )
        self.connection.execute(
            """
            INSERT INTO telemetry_v4
            (timestamp, flow_id, flags, slave_temperature_c, slave_humidity_pct,
             slave_pressure_pa, sound_rms_1, sound_rms_2, rain_state,
             master_temperature_c, master_humidity_pct, raw_frame)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                datetime.now().isoformat(timespec="seconds"),
                flow_id,
                values["flags"],
                *(values[name] for name in columns),
                raw_frame.hex(" ").upper(),
            ),
        )
        self.connection.commit()

    def close(self) -> None:
        self.connection.close()


class SerialEvent:
    def __init__(self, kind: str, value: object):
        self.kind = kind
        self.value = value


class MonitorApp(tk.Tk):
    FAN_PINS = {1: "PB1", 2: "PB8"}
    FAN_ACK_TIMEOUT_S = 8.0
    WINDOW_ACK_TIMEOUT_S = 8.0
    TELEMETRY_POLL_INTERVAL_S = 1.0
    TELEMETRY_TIMEOUT_S = 5.0
    FIELD_LABELS = (
        ("master_bme_temperature_c", "主机 BME280 温度", "℃"),
        ("slave_bme_temperature_c", "从机 BME280 温度", "℃"),
        ("master_bme_humidity_pct", "主机 BME280 湿度", "%RH"),
        ("slave_bme_humidity_pct", "从机 BME280 湿度", "%RH"),
        ("master_bme_pressure_pa", "主机 BME280 绝对气压", "Pa"),
        ("slave_bme_pressure_pa", "从机 BME280 绝对气压", "Pa"),
    )
    SOUND_LABELS = (
        ("sound_rms_1", "声音1 / 左声道（SEL 接 GND）"),
        ("sound_rms_2", "声音2 / 右声道（SEL 接 3V3）"),
    )

    def __init__(self):
        super().__init__()
        self.title("LoRa 通风监控 v4 — 主从 BME280 / 双声学 / 窗户测试")
        self.geometry("850x850")
        self.serial_port = None
        self.reader_thread = None
        self.stop_event = threading.Event()
        self.events: queue.Queue[SerialEvent] = queue.Queue()
        self.parser = FrameStreamParser()
        self.database = TelemetryDatabase()
        self.flow_id = 0
        self.value_vars = {name: tk.StringVar(value="--") for name, _, _ in self.FIELD_LABELS}
        self.sound_vars = {name: tk.StringVar(value="--") for name, _ in self.SOUND_LABELS}
        self.slave_link_var = tk.StringVar(value="从机链路：未知")
        self.last_telemetry_at = None
        self.duty_vars = {channel: tk.DoubleVar(value=0) for channel in self.FAN_PINS}
        self.fan_status_vars = {channel: tk.StringVar(value="未发送") for channel in self.FAN_PINS}
        self.fan_pending = {}
        self.telemetry_pending = None
        self.window_queued_action = None
        self.window_pending = None
        self.window_status_var = tk.StringVar(value="未连接，位置未知")
        self.window_buttons = {}
        self.fan_send_buttons = {}
        self.next_telemetry_poll_at = 0.0
        self._build_ui()
        self._refresh_ports()
        self.after(50, self._drain_events)
        self.protocol("WM_DELETE_WINDOW", self._close)

    def _build_ui(self):
        connection = ttk.LabelFrame(self, text="串口连接")
        connection.pack(fill=tk.X, padx=12, pady=8)
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(connection, textvariable=self.port_var, width=20)
        self.port_box.pack(side=tk.LEFT, padx=6, pady=8)
        ttk.Button(connection, text="刷新", command=self._refresh_ports).pack(side=tk.LEFT)
        self.connect_button = ttk.Button(connection, text="连接", command=self._toggle_connection)
        self.connect_button.pack(side=tk.LEFT, padx=6)
        self.telemetry_button = ttk.Button(connection, text="读取单帧", command=self._request_telemetry)
        self.telemetry_button.pack(side=tk.RIGHT, padx=6)

        telemetry = ttk.LabelFrame(self, text="单组遥测（占位值显示为 --）")
        telemetry.pack(fill=tk.X, padx=12, pady=8)
        for row, (name, label, unit) in enumerate(self.FIELD_LABELS):
            ttk.Label(telemetry, text=label, width=24).grid(row=row // 2, column=(row % 2) * 2, sticky="w", padx=8, pady=6)
            ttk.Label(telemetry, textvariable=self.value_vars[name], width=18).grid(row=row // 2, column=(row % 2) * 2 + 1, sticky="w")
            if unit:
                self.value_vars[name].set(f"-- {unit}")

        ttk.Label(telemetry, textvariable=self.slave_link_var).grid(
            row=3, column=0, columnspan=4, sticky="w", padx=8, pady=6)

        acoustic = ttk.LabelFrame(self, text="从机声学 — 最近1秒短窗 RMS 最大值")
        acoustic.pack(fill=tk.X, padx=12, pady=8)
        for column, (name, label) in enumerate(self.SOUND_LABELS):
            ttk.Label(acoustic, text=label).grid(row=0, column=column * 2, sticky="w", padx=8, pady=6)
            ttk.Label(acoustic, textvariable=self.sound_vars[name], width=14,
                      font=("TkDefaultFont", 13)).grid(row=0, column=column * 2 + 1, sticky="w", padx=8)
        ttk.Label(acoustic, text="单位：18位 PCM 计数，不是分贝；无有效数据显示 --").grid(
            row=1, column=0, columnspan=4, sticky="w", padx=8, pady=6)

        fans = ttk.LabelFrame(self, text="两路风机 PWM（0–100%）")
        fans.pack(fill=tk.X, padx=12, pady=8)
        for row, (channel, pin) in enumerate(self.FAN_PINS.items()):
            variable = self.duty_vars[channel]
            ttk.Label(fans, text=f"风机 {channel} / {pin}").grid(row=row, column=0, padx=8, pady=5)
            slider = ttk.Scale(fans, from_=0, to=100, variable=variable,
                               orient=tk.HORIZONTAL, length=300,
                               command=lambda value, ch=channel: self._preview_fan(ch, value))
            slider.grid(row=row, column=1)
            slider.bind("<ButtonRelease-1>", lambda event, ch=channel: self._set_fan(ch))
            slider.bind("<KeyRelease-Left>", lambda event, ch=channel: self._set_fan(ch))
            slider.bind("<KeyRelease-Right>", lambda event, ch=channel: self._set_fan(ch))
            ttk.Spinbox(fans, from_=0, to=100, textvariable=variable, width=6).grid(row=row, column=2, padx=8)
            button = ttk.Button(fans, text="发送", command=lambda ch=channel: self._set_fan(ch))
            button.grid(row=row, column=3, padx=8)
            self.fan_send_buttons[channel] = button
            ttk.Label(fans, textvariable=self.fan_status_vars[channel], width=20).grid(row=row, column=4, padx=6)

        window = ttk.LabelFrame(self, text="从机 PB8 / SG90 360°连续旋转空载测试")
        window.pack(fill=tk.X, padx=12, pady=8)
        for column, (action, label) in enumerate(((1, "打开窗户"), (0, "关闭窗户"))):
            button = ttk.Button(window, text=label, command=lambda value=action: self._set_window(value))
            button.grid(row=0, column=column, padx=8, pady=8)
            self.window_buttons[action] = button
        ttk.Label(window, textvariable=self.window_status_var, wraplength=460).grid(
            row=0, column=2, sticky="w", padx=8)
        ttk.Label(window, text="开窗 1700 μs / 关窗 1300 μs，各运行 300 ms 后设置 1500 μs 停止脉宽（需空载校准）",
                  wraplength=760).grid(
            row=1, column=0, columnspan=3, sticky="w", padx=8, pady=6)
        ttk.Label(window, text="ACK 仅确认动作启动 PWM，不表示动作完成、自动停止成功或机械到位",
                  wraplength=760).grid(
            row=2, column=0, columnspan=3, sticky="w", padx=8, pady=6)
        self._sync_window_buttons()

        log_frame = ttk.LabelFrame(self, text="帧日志")
        log_frame.pack(fill=tk.BOTH, expand=True, padx=12, pady=8)
        self.log = tk.Text(log_frame, height=12, state=tk.DISABLED)
        self.log.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

    def _next_flow(self) -> int:
        self.flow_id = (self.flow_id + 1) & 0xFFFF
        return self.flow_id

    def _refresh_ports(self):
        ports = [] if serial is None else [port.device for port in serial.tools.list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _toggle_connection(self):
        if self.serial_port is not None:
            self._disconnect()
            return
        if serial is None:
            messagebox.showerror("缺少依赖", "请安装 pyserial")
            return
        try:
            self.serial_port = serial.Serial(self.port_var.get(), 115200,
                                             timeout=0.1, write_timeout=0.5)
        except Exception as exc:
            messagebox.showerror("连接失败", str(exc))
            return
        self.stop_event.clear()
        self.parser = FrameStreamParser()
        self.telemetry_pending = None
        self.window_queued_action = None
        self.window_pending = None
        self.window_status_var.set("已连接，尚未发送，位置未知")
        self.next_telemetry_poll_at = time.monotonic() + self.TELEMETRY_POLL_INTERVAL_S
        self.reader_thread = threading.Thread(target=self._reader, daemon=True)
        self.reader_thread.start()
        self.connect_button.config(text="断开")
        self._sync_window_buttons()
        self._append_log(f"已连接 {self.port_var.get()}")

    def _disconnect(self):
        if self.window_pending is not None:
            self.window_status_var.set("已断开，执行结果未知")
        elif self.window_queued_action is not None:
            self.window_status_var.set("已断开，待发动作已取消")
        elif self.serial_port is not None:
            self.window_status_var.set("已断开，位置未知")
        self.window_queued_action = None
        self.window_pending = None
        self.fan_pending.clear()
        self.telemetry_pending = None
        self.next_telemetry_poll_at = 0.0
        self._clear_telemetry()
        for status in self.fan_status_vars.values():
            status.set("已断开，状态未知")
        self.stop_event.set()
        port, self.serial_port = self.serial_port, None
        if port is not None:
            port.close()
        self.connect_button.config(text="连接")
        self._sync_window_buttons()

    def _reader(self):
        while not self.stop_event.is_set() and self.serial_port is not None:
            try:
                chunk = self.serial_port.read(self.serial_port.in_waiting or 1)
                for frame in self.parser.feed(chunk):
                    self.events.put(SerialEvent("frame", frame))
            except Exception as exc:
                self.events.put(SerialEvent("error", str(exc)))
                break

    def _send(self, frame: bytes):
        if self.serial_port is None:
            messagebox.showwarning("未连接", "请先连接控制室串口")
            return False
        try:
            if self.serial_port.write(frame) != len(frame):
                raise IOError("串口未发送完整帧")
        except Exception as exc:
            self._append_log(f"发送失败: {exc}")
            self._disconnect()
            return False
        self._append_log("TX " + frame.hex(" ").upper())
        return True

    def _clear_telemetry(self):
        for value in self.value_vars.values():
            value.set("--")
        for value in self.sound_vars.values():
            value.set("--")
        self.slave_link_var.set("从机链路：未知")
        self.last_telemetry_at = None

    def _request_telemetry(self, force_resample: bool = True):
        if self._window_busy():
            self._append_log("窗户命令等待完成，暂停新增遥测请求")
            return
        if self.telemetry_pending is not None:
            self._append_log("遥测请求等待应答，暂不重复发送")
            return
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_read_telemetry(flow, force_resample)):
            now = time.monotonic()
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
        # Close is action 0; truthiness must not drop a queued close command.
        return self.window_queued_action is not None or self.window_pending is not None

    def _sync_window_buttons(self):
        busy = self._window_busy()
        for button in self.window_buttons.values():
            button.state(["!disabled" if self.serial_port is not None and not busy else "disabled"])
        for button in (*self.fan_send_buttons.values(), self.telemetry_button):
            button.state(["disabled" if busy else "!disabled"])

    @staticmethod
    def _window_action_text(action: int) -> str:
        return "开窗" if action == 1 else "关窗"

    def _set_window(self, action: int):
        if type(action) is not int or action not in LoRaProtocol.WINDOW_ACTIONS:
            raise ValueError("window action must be 0 or 1")
        if self.serial_port is None:
            self.window_status_var.set("未连接，未发送")
            self._sync_window_buttons()
            return
        if self._window_busy():
            self._append_log("窗户命令等待完成，不重复发送")
            return
        self.window_queued_action = action
        self.window_status_var.set(f"{self._window_action_text(action)}等待前序请求")
        self._sync_window_buttons()
        self._service_window(time.monotonic())

    def _finish_window(self, text: str, now: float):
        self.window_pending = None
        self.window_status_var.set(text)
        self.next_telemetry_poll_at = now + self.TELEMETRY_POLL_INTERVAL_S
        self._sync_window_buttons()

    def _expire_window(self, now: float) -> bool:
        if self.window_pending is None or now - self.window_pending[2] < self.WINDOW_ACK_TIMEOUT_S:
            return False
        flow, action, _ = self.window_pending
        self._finish_window(f"{self._window_action_text(action)}确认超时，结果未知", now)
        self._append_log(f"窗户确认超时 flow={flow}；不自动重试")
        return True

    def _service_window(self, now: float):
        self._expire_window(now)
        if (self.serial_port is None or self.window_pending is not None
                or self.window_queued_action is None or self.telemetry_pending is not None
                or self.fan_pending):
            return
        action, self.window_queued_action = self.window_queued_action, None
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_set_window(flow, action)):
            # Start the deadline only after the complete UART write, not at button click.
            self.window_pending = (flow, action, time.monotonic())
            self.window_status_var.set(f"{self._window_action_text(action)}等待确认")
        else:
            self.window_status_var.set(f"{self._window_action_text(action)}发送失败，结果未知")
        self._sync_window_buttons()

    @staticmethod
    def _normalize_fan_duty(value) -> int:
        number = float(value)
        if not math.isfinite(number) or not 0 <= number <= 100:
            raise ValueError("占空比必须在 0–100 之间")
        return int(number + 0.5)

    def _preview_fan(self, channel: int, value):
        duty = self._normalize_fan_duty(value)
        self.duty_vars[channel].set(duty)

    def _set_fan(self, channel: int):
        if self._window_busy():
            self._append_log("窗户命令等待完成，暂停新增风机请求")
            return
        if channel not in LoRaProtocol.FAN_CHANNELS:
            messagebox.showwarning("通道已停用", "仅支持风机1/PB1、风机2/PB8")
            return
        try:
            duty = self._normalize_fan_duty(self.duty_vars[channel].get())
        except (ValueError, TypeError, tk.TclError):
            messagebox.showwarning("输入错误", "请输入 0–100 的占空比")
            return
        self.duty_vars[channel].set(duty)
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_set_fan_speed(flow, channel, duty)):
            # Newer settings supersede older confirmations for this channel.
            self.fan_pending = {key: item for key, item in self.fan_pending.items()
                                if item[0] != channel}
            self.fan_pending[flow] = (channel, duty, time.monotonic())
            self.fan_status_vars[channel].set(f"等待确认 {duty}%")

    def _drain_events(self):
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            if event.kind == "frame":
                self._handle_frame(event.value)
            else:
                self._append_log("串口错误: " + str(event.value))
                self._disconnect()
        for flow, (channel, duty, sent_at) in list(self.fan_pending.items()):
            if time.monotonic() - sent_at >= self.FAN_ACK_TIMEOUT_S:
                del self.fan_pending[flow]
                self.fan_status_vars[channel].set(f"{duty}% 确认超时")
        now = time.monotonic()
        self._poll_telemetry(now)
        self._service_window(now)
        self.after(50, self._drain_events)

    def _handle_frame(self, frame: bytes):
        self._append_log("RX " + frame.hex(" ").upper())
        try:
            packet = LoRaProtocol.parse_packet(frame)
        except ValueError as exc:
            self._append_log("丢弃: " + str(exc))
            return
        if packet["type"] == LoRaProtocol.MSG_TELEMETRY:
            if self.telemetry_pending is None or self.telemetry_pending[0] != packet["flow_id"]:
                return
            now = time.monotonic()
            if now - self.telemetry_pending[1] >= self.TELEMETRY_TIMEOUT_S:
                # The event drain runs before polling: enforce the deadline here too.
                self.telemetry_pending = None
                self.next_telemetry_poll_at = now + self.TELEMETRY_POLL_INTERVAL_S
                self._clear_telemetry()
                return
            self.telemetry_pending = None
            values = LoRaProtocol.decode_telemetry(packet["data"])
            online = values["slave_online"]
            self.slave_link_var.set("从机链路：" + (
                "未知（旧布局）" if online is None else "在线" if online else "离线"))
            self.database.insert(packet["flow_id"], values, frame)
            self.last_telemetry_at = now
            for name, _, unit in self.FIELD_LABELS:
                value = values[name]
                self.value_vars[name].set("--" if value is None else f"{value:g}{(' ' + unit) if unit else ''}")
            # Only the 26-byte layout carries current stereo acoustic statistics.
            for name, _ in self.SOUND_LABELS:
                value = values[name] if len(packet["data"]) == LoRaProtocol.TELEMETRY_SIZE else None
                self.sound_vars[name].set("--" if value is None else str(value))
        elif packet["type"] == LoRaProtocol.MSG_ACK:
            self._append_log(f"ACK command=0x{packet['data'][0]:02X} status={packet['data'][1]}")
            if packet["data"][0] == LoRaProtocol.MSG_SET_WINDOW:
                if self.window_pending is None or self.window_pending[0] != packet["flow_id"]:
                    return
                now = time.monotonic()
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
                    text = f"已确认 {duty}%" if packet["data"][1] == 0 else "主机拒绝执行"
                    self.fan_status_vars[channel].set(text)
        elif packet["type"] == LoRaProtocol.MSG_ERROR:
            self._append_log(f"ERROR code={packet['data'][0]}")
            if self.window_pending is not None and self.window_pending[0] == packet["flow_id"]:
                now = time.monotonic()
                if not self._expire_window(now):
                    action = self._window_action_text(self.window_pending[1])
                    self._finish_window(f"{action}错误 code={packet['data'][0]}，结果未知", now)
            if self.telemetry_pending is not None and self.telemetry_pending[0] == packet["flow_id"]:
                self.telemetry_pending = None
                self.next_telemetry_poll_at = time.monotonic() + self.TELEMETRY_POLL_INTERVAL_S
                self._clear_telemetry()
            pending = self.fan_pending.pop(packet["flow_id"], None)
            if pending is not None:
                self.fan_status_vars[pending[0]].set(f"失败 code={packet['data'][0]}")

    def _append_log(self, text: str):
        self.log.config(state=tk.NORMAL)
        self.log.insert(tk.END, f"{time.strftime('%H:%M:%S')} {text}\n")
        self.log.see(tk.END)
        self.log.config(state=tk.DISABLED)

    def _close(self):
        self._disconnect()
        self.database.close()
        self.destroy()


if __name__ == "__main__":
    MonitorApp().mainloop()
