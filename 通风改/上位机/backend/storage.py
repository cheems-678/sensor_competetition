"""Existing telemetry_v4 storage; column meanings and SQL are unchanged."""

from __future__ import annotations

import sqlite3
from datetime import datetime


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

