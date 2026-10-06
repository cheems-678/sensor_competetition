"""Read-only, forward-only telemetry replay and labelled synthetic evaluation."""
from __future__ import annotations
import argparse
import json
import math
import sqlite3
from contextlib import closing
from datetime import datetime
from pathlib import Path
from .protocol import LoRaProtocol
from .warning_engine import WarningEngine

SCENARIOS = ("normal", "warming", "humidity", "spike", "missing", "timeout", "recovery", "reconnect")


def scenario_points(name, duration=1050):
    if name not in SCENARIOS:
        raise ValueError("未知演示场景")
    for second in range(duration + 1):
        rising = min(second, 600)
        temp = 24 + (rising * 0.01 if name in ("warming", "recovery", "reconnect") else math.sin(second / 15) * 0.03)
        humidity = 50 + (rising / 30 if name == "humidity" else math.sin(second / 20) * 0.05)
        if name == "spike" and second == 450:
            temp += 12
        readings = {"master_temp": temp, "master_humidity": humidity,
                    "slave_temp": 24.2, "slave_humidity": 51.3}
        if name == "missing" and 350 <= second < 370:
            readings["master_temp"] = None
        action = "sample"
        if name == "timeout" and 350 <= second < 370:
            action = "timeout" if second == 350 else "gap"
        if name == "reconnect" and second == 400:
            action = "reconnect"
        yield second, action, readings


def run_scenario(engine, name, stamp=lambda t: f"模拟 +{t}s", offset=0, duration=None):
    lengths = {"normal": 360, "warming": 400, "humidity": 400, "spike": 600,
               "missing": 380, "timeout": 370, "recovery": 1050, "reconnect": 410}
    duration = lengths[name] if duration is None else duration
    engine.reset_window()
    engine.connected = True
    for sample_id, (second, action, readings) in enumerate(scenario_points(name, duration), 1):
        now = offset + second
        engine.expire(now)
        if action == "reconnect":
            engine.stop(stamp(second))
            engine.connected = True
        elif action == "timeout":
            engine.communication_timeout(stamp(second), now)
        elif action == "sample":
            engine.observe(sample_id, now, stamp(second), readings)
    return engine.snapshot()


def evaluate_synthetic():
    reports = []
    for name in ("normal", "warming", "humidity", "spike", "missing", "recovery"):
        engine = WarningEngine(enabled=True)
        baseline = []
        above = {"master_temp": False, "master_humidity": False}
        for second, action, readings in scenario_points(name):
            engine.expire(second)
            if action != "sample":
                continue
            engine.observe(second + 1, second, f"+{second}s", readings)
            for key, limit in (("master_temp", 30), ("master_humidity", 70)):
                value = readings[key]
                if value is None:
                    continue
                current = value >= limit
                if current and not above[key]:
                    baseline.append(dict(key=key, at=second))
                above[key] = current
        truth = []
        if name in ("warming", "recovery"):
            truth = [{"key": "master_temp", "start": 0, "end": 1050}]
        elif name == "humidity":
            truth = [{"key": "master_humidity", "start": 0, "end": 1050}]
        detected = [{"key": event["key"], "at": event["occurred_seconds"]} for event in engine.events]
        def metrics(events):
            matches = lambda ev, label: ev["key"] == label["key"] and label["start"] <= ev["at"] <= label["end"]
            missed = sum(not any(matches(ev, label) for ev in events) for label in truth)
            return dict(false_events=sum(not any(matches(ev, label) for label in truth) for ev in events),
                        missed_events=missed, labelled_events=len(truth),
                        miss_rate=missed / len(truth) if truth else None)
        lead = []
        for label in truth:
            times = [[ev["at"] for ev in events if ev["key"] == label["key"]
                      and label["start"] <= ev["at"] <= label["end"]] for events in (detected, baseline)]
            if all(times):
                lead.append(min(times[1]) - min(times[0]))
        reports.append(dict(scenario=name, duration_seconds=1050, trend=metrics(detected),
                            fixed=metrics(baseline), lead_seconds=lead,
                            trend_triggers=detected, fixed_triggers=baseline))
    return {"source": "labelled_synthetic", "limitations": "仅合成测试，不能视为现场准确率",
            "fixed_thresholds": {"temperature_c": 30, "humidity_pct": 70}, "scenarios": reports}


def replay_database(path):
    source = Path(path).resolve(strict=True)
    engine = WarningEngine(enabled=True)
    invalid = 0
    origin = None
    previous = -1
    with closing(sqlite3.connect(source.as_uri() + "?mode=ro", uri=True)) as connection:
        for sample_id, (timestamp, raw) in enumerate(connection.execute(
                "SELECT timestamp, raw_frame FROM telemetry_v4 ORDER BY id"), 1):
            try:
                seconds = datetime.fromisoformat(timestamp).timestamp()
                packet = LoRaProtocol.parse_packet(bytes.fromhex(raw))
                if packet["type"] != LoRaProtocol.MSG_TELEMETRY:
                    raise ValueError("非遥测帧")
                values = LoRaProtocol.decode_telemetry(packet["data"])
                if origin is None:
                    origin = seconds
                now = seconds - origin
                if now < previous:
                    raise ValueError("时间倒退")
                previous = now
                readings = {f"{side}_{kind}": values[f"{side}_bme_{'temperature_c' if kind == 'temp' else 'humidity_pct'}"]
                            for side in ("master", "slave") for kind in ("temp", "humidity")}
                engine.observe(sample_id, now, timestamp, readings)
            except (ValueError, TypeError, KeyError):
                invalid += 1
    return dict(source="unlabelled_history", skipped_rows=invalid, events=engine.snapshot()["events"],
                limitations="无真实异常标注，仅报告触发记录；不能计算现场误报、漏报或准确率。")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--synthetic", action="store_true")
    group.add_argument("--database")
    args = parser.parse_args()
    print(json.dumps(evaluate_synthetic() if args.synthetic else replay_database(args.database), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
