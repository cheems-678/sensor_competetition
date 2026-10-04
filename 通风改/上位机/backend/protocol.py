"""LoRa v4 wire protocol, preserved from the legacy desktop."""

from __future__ import annotations

import struct


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

