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
    MSG_ACK = 0x20
    MSG_ERROR = 0x7E

    TELEMETRY_SIZE = 18
    SINGLE_GROUP = 1

    TEMPERATURE_INVALID = -32768
    UINT16_INVALID = 0xFFFF
    UINT32_INVALID = 0xFFFFFFFF
    RAIN_INVALID = 0xFF

    TYPE_NAMES = {
        MSG_READ_TELEMETRY: "READ_TELEMETRY",
        MSG_TELEMETRY: "TELEMETRY",
        MSG_SET_FAN_SPEED: "SET_FAN_SPEED",
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
            valid = len(payload) == cls.TELEMETRY_SIZE
            direction = source == (cls.ROLE_MASTER, cls.SINGLE_GROUP) and target == (
                cls.ROLE_CONTROL,
                0,
            )
        elif msg_type == cls.MSG_SET_FAN_SPEED:
            valid = (
                len(payload) == 2
                and 1 <= payload[0] <= 4
                and payload[1] <= 100
            )
            direction = source == (cls.ROLE_CONTROL, 0) and target == (
                cls.ROLE_MASTER,
                cls.SINGLE_GROUP,
            )
        elif msg_type == cls.MSG_ACK:
            valid = len(payload) == 2
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
        if not 1 <= channel <= 4:
            raise ValueError("fan channel must be 1..4")
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
    def placeholder_payload(cls) -> bytes:
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
    def decode_telemetry(cls, payload: bytes) -> dict:
        if len(payload) != cls.TELEMETRY_SIZE:
            raise ValueError(f"telemetry payload must be {cls.TELEMETRY_SIZE} bytes")
        values = struct.unpack("<BhHIHHBhH", payload)

        def temperature(value: int):
            return None if value == cls.TEMPERATURE_INVALID else value / 10.0

        def humidity(value: int):
            return None if value == cls.UINT16_INVALID else value / 10.0

        def u16(value: int):
            return None if value == cls.UINT16_INVALID else value

        return {
            "flags": values[0],
            "slave_temperature_c": temperature(values[1]),
            "slave_humidity_pct": humidity(values[2]),
            "slave_pressure_pa": None if values[3] == cls.UINT32_INVALID else values[3],
            "sound_rms_1": u16(values[4]),
            "sound_rms_2": u16(values[5]),
            "rain_state": None if values[6] == cls.RAIN_INVALID else values[6],
            "master_temperature_c": temperature(values[7]),
            "master_humidity_pct": humidity(values[8]),
        }


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
    FAN_PINS = ("PA1", "PB1", "PB9", "PB8")
    FAN_ACK_TIMEOUT_S = 8.0
    FIELD_LABELS = (
        ("slave_temperature_c", "从机 BME 温度", "℃"),
        ("slave_humidity_pct", "从机 BME 湿度", "%RH"),
        ("slave_pressure_pa", "从机 BME 气压", "Pa"),
        ("sound_rms_1", "声学 RMS 1", ""),
        ("sound_rms_2", "声学 RMS 2", ""),
        ("rain_state", "雨滴状态", ""),
        ("master_temperature_c", "主机 DHT11 温度", "℃"),
        ("master_humidity_pct", "主机 DHT11 湿度", "%RH"),
    )

    def __init__(self):
        super().__init__()
        self.title("LoRa 通风监控 v4 — 单组")
        self.geometry("820x650")
        self.serial_port = None
        self.reader_thread = None
        self.stop_event = threading.Event()
        self.events: queue.Queue[SerialEvent] = queue.Queue()
        self.parser = FrameStreamParser()
        self.database = TelemetryDatabase()
        self.flow_id = 0
        self.value_vars = {name: tk.StringVar(value="--") for name, _, _ in self.FIELD_LABELS}
        self.duty_vars = [tk.DoubleVar(value=0) for _ in range(4)]
        self.fan_status_vars = [tk.StringVar(value="未发送") for _ in range(4)]
        self.fan_pending = {}
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
        ttk.Button(connection, text="读取单帧", command=self._request_telemetry).pack(side=tk.RIGHT, padx=6)

        telemetry = ttk.LabelFrame(self, text="单组遥测（占位值显示为 --）")
        telemetry.pack(fill=tk.X, padx=12, pady=8)
        for row, (name, label, unit) in enumerate(self.FIELD_LABELS):
            ttk.Label(telemetry, text=label, width=24).grid(row=row // 2, column=(row % 2) * 2, sticky="w", padx=8, pady=6)
            ttk.Label(telemetry, textvariable=self.value_vars[name], width=18).grid(row=row // 2, column=(row % 2) * 2 + 1, sticky="w")
            if unit:
                self.value_vars[name].set(f"-- {unit}")

        fans = ttk.LabelFrame(self, text="四路风机 PWM（0–100%）")
        fans.pack(fill=tk.X, padx=12, pady=8)
        for index, variable in enumerate(self.duty_vars, start=1):
            ttk.Label(fans, text=f"风机 {index} / {self.FAN_PINS[index - 1]}").grid(row=index - 1, column=0, padx=8, pady=5)
            slider = ttk.Scale(fans, from_=0, to=100, variable=variable,
                               orient=tk.HORIZONTAL, length=300,
                               command=lambda value, ch=index: self._preview_fan(ch, value))
            slider.grid(row=index - 1, column=1)
            slider.bind("<ButtonRelease-1>", lambda event, ch=index: self._set_fan(ch))
            slider.bind("<KeyRelease-Left>", lambda event, ch=index: self._set_fan(ch))
            slider.bind("<KeyRelease-Right>", lambda event, ch=index: self._set_fan(ch))
            ttk.Spinbox(fans, from_=0, to=100, textvariable=variable, width=6).grid(row=index - 1, column=2, padx=8)
            ttk.Button(fans, text="发送", command=lambda ch=index: self._set_fan(ch)).grid(row=index - 1, column=3, padx=8)
            ttk.Label(fans, textvariable=self.fan_status_vars[index - 1], width=20).grid(row=index - 1, column=4, padx=6)

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
        self.reader_thread = threading.Thread(target=self._reader, daemon=True)
        self.reader_thread.start()
        self.connect_button.config(text="断开")
        self._append_log(f"已连接 {self.port_var.get()}")

    def _disconnect(self):
        self.fan_pending.clear()
        for status in self.fan_status_vars:
            status.set("已断开，状态未知")
        self.stop_event.set()
        port, self.serial_port = self.serial_port, None
        if port is not None:
            port.close()
        self.connect_button.config(text="连接")

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

    def _request_telemetry(self):
        self._send(LoRaProtocol.cmd_read_telemetry(self._next_flow(), True))

    @staticmethod
    def _normalize_fan_duty(value) -> int:
        number = float(value)
        if not math.isfinite(number) or not 0 <= number <= 100:
            raise ValueError("占空比必须在 0–100 之间")
        return int(number + 0.5)

    def _preview_fan(self, channel: int, value):
        duty = self._normalize_fan_duty(value)
        self.duty_vars[channel - 1].set(duty)

    def _set_fan(self, channel: int):
        try:
            duty = self._normalize_fan_duty(self.duty_vars[channel - 1].get())
        except (ValueError, TypeError, tk.TclError):
            messagebox.showwarning("输入错误", "请输入 0–100 的占空比")
            return
        self.duty_vars[channel - 1].set(duty)
        flow = self._next_flow()
        if self._send(LoRaProtocol.cmd_set_fan_speed(flow, channel, duty)):
            # Newer settings supersede older confirmations for this channel.
            self.fan_pending = {key: item for key, item in self.fan_pending.items()
                                if item[0] != channel}
            self.fan_pending[flow] = (channel, duty, time.monotonic())
            self.fan_status_vars[channel - 1].set(f"等待确认 {duty}%")

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
                self.fan_status_vars[channel - 1].set(f"{duty}% 确认超时")
        self.after(50, self._drain_events)

    def _handle_frame(self, frame: bytes):
        self._append_log("RX " + frame.hex(" ").upper())
        try:
            packet = LoRaProtocol.parse_packet(frame)
        except ValueError as exc:
            self._append_log("丢弃: " + str(exc))
            return
        if packet["type"] == LoRaProtocol.MSG_TELEMETRY:
            values = LoRaProtocol.decode_telemetry(packet["data"])
            self.database.insert(packet["flow_id"], values, frame)
            for name, _, unit in self.FIELD_LABELS:
                value = values[name]
                self.value_vars[name].set("--" if value is None else f"{value:g}{(' ' + unit) if unit else ''}")
        elif packet["type"] == LoRaProtocol.MSG_ACK:
            self._append_log(f"ACK command=0x{packet['data'][0]:02X} status={packet['data'][1]}")
            if packet["data"][0] == LoRaProtocol.MSG_SET_FAN_SPEED:
                pending = self.fan_pending.pop(packet["flow_id"], None)
                if pending is not None:
                    channel, duty, _ = pending
                    text = f"已确认 {duty}%" if packet["data"][1] == 0 else "主机拒绝执行"
                    self.fan_status_vars[channel - 1].set(text)
        elif packet["type"] == LoRaProtocol.MSG_ERROR:
            self._append_log(f"ERROR code={packet['data'][0]}")
            pending = self.fan_pending.pop(packet["flow_id"], None)
            if pending is not None:
                self.fan_status_vars[pending[0] - 1].set(f"失败 code={packet['data'][0]}")

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
