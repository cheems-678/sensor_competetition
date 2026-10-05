"""In-memory protocol simulator for explicitly requested previews only."""

from __future__ import annotations

import struct
import threading
import time

from .protocol import LoRaProtocol


class DemoSerial:
    def __init__(self, port="DEMO", baudrate=115200, *, timeout=0.1, write_timeout=0.5):
        if port != "DEMO":
            raise ValueError("演示模式仅提供 DEMO 模拟串口")
        self.port, self.timeout = port, timeout
        self.closed = False
        self.writes: list[bytes] = []
        self._condition = threading.Condition()
        self._responses: list[tuple[float, bytes]] = []
        self._buffer = bytearray()
        self._samples = 0

    def _collect(self):
        now = time.monotonic()
        due = [item for item in self._responses if item[0] <= now]
        self._responses = [item for item in self._responses if item[0] > now]
        for _, frame in due:
            self._buffer.extend(frame)

    @property
    def in_waiting(self):
        with self._condition:
            self._collect()
            return len(self._buffer)

    def read(self, count=1):
        deadline = time.monotonic() + self.timeout
        with self._condition:
            while not self.closed:
                self._collect()
                if self._buffer:
                    data = bytes(self._buffer[:count])
                    del self._buffer[:count]
                    return data
                left = deadline - time.monotonic()
                if left <= 0:
                    return b""
                if self._responses:
                    left = min(left, max(0.001, min(due for due, _ in self._responses) - time.monotonic()))
                self._condition.wait(left)
            return b""

    def write(self, frame: bytes):
        packet = LoRaProtocol.parse_packet(frame)
        with self._condition:
            if self.closed:
                raise IOError("模拟串口已关闭")
            self.writes.append(bytes(frame))
            if packet["type"] == LoRaProtocol.MSG_READ_TELEMETRY:
                self._samples += 1
                payload = struct.pack("<BhHIhHIBII4H", 0x1F, 236, 478, 101325,
                                      242, 513, 100982, 0xFF, 0, 9873,
                                      1241, 1000, 2000, 60)
                msg_type = LoRaProtocol.MSG_TELEMETRY
            elif packet["type"] in (LoRaProtocol.MSG_SET_FAN_SPEED, LoRaProtocol.MSG_SET_WINDOW):
                payload = bytes((packet["type"], 0))
                msg_type = LoRaProtocol.MSG_ACK
            else:
                raise ValueError("模拟串口收到不支持的命令")
            response = LoRaProtocol.build_packet(msg_type, 2, 1, 1, 0, packet["flow_id"], payload)
            self._responses.append((time.monotonic() + 0.25, response))
            self._condition.notify_all()
            return len(frame)

    def close(self):
        with self._condition:
            self.closed = True
            self._condition.notify_all()
