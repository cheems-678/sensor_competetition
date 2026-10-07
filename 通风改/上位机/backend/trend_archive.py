"""Credential-free trend archives, owned by the controller worker."""
from __future__ import annotations

import copy
import json
import math
import os
import uuid
from pathlib import Path


def archive_record(report):
    if not isinstance(report, dict) or not isinstance(report.get("channels"), dict) or not isinstance(report.get("analysis"), dict):
        raise ValueError("档案结构无效")
    if type(report.get("id")) is not int or any(not isinstance(report.get(key), str) for key in ("checked_at", "status", "message", "data_source")):
        raise ValueError("档案字段无效")
    if report["status"] not in ("normal", "abnormal", "partial", "insufficient", "unavailable") or report["data_source"] not in ("demo", "telemetry"):
        raise ValueError("档案状态无效")
    if type(report.get("stale")) is not bool or not isinstance(report.get("event_ids"), list) or any(type(i) is not int for i in report["event_ids"]):
        raise ValueError("档案判定无效")
    for channel in report["channels"].values():
        if not isinstance(channel, dict) or channel.get("source") not in ("master", "slave") or channel.get("kind") not in ("temp", "humidity"):
            raise ValueError("档案通道无效")
        if channel.get("state") not in ("ready", "insufficient", "unavailable") or channel.get("direction") not in ("rising", "falling", "stable", "unknown"):
            raise ValueError("档案趋势无效")
        if any(type(channel.get(key)) not in (int, float) or not math.isfinite(channel[key]) for key in ("threshold", "span_seconds", "sample_count")):
            raise ValueError("档案数值无效")
        if any(channel.get(key) is not None and (type(channel[key]) not in (int, float) or not math.isfinite(channel[key])) for key in ("current", "start", "rate", "min", "max")):
            raise ValueError("档案数值无效")
    analysis = report["analysis"]
    if analysis.get("status") not in ("idle", "pending", "complete", "error", "stale"):
        raise ValueError("档案分析状态无效")
    if any(analysis.get(key) is not None and not isinstance(analysis[key], str) for key in ("provider", "model", "analyzed_at", "mode", "data_source")):
        raise ValueError("档案分析字段无效")
    result = analysis.get("result")
    if result is not None and (not isinstance(result, dict) or
            any(not isinstance(result[key], str) for key in ("summary", "limitations", "error") if key in result) or
            any(not isinstance(result[key], list) or any(not isinstance(item, str) for item in result[key]) for key in ("possible_causes", "suggested_checks") if key in result)):
        raise ValueError("档案分析结果无效")
    record = {key: copy.deepcopy(report[key]) for key in (
        "id", "checked_at", "status", "message", "channels", "event_ids", "stale", "data_source")}
    record["analysis"] = {key: copy.deepcopy(value) for key, value in report["analysis"].items()
                          if key in ("status", "result", "provider", "model", "mode", "data_source", "analyzed_at")}
    record["channels"] = {key: {field: copy.deepcopy(value) for field, value in channel.items()
                               if field in ("source", "kind", "state", "direction", "current", "start", "rate", "threshold",
                                            "span_seconds", "sample_count", "exceeded", "min", "max")}
                          for key, channel in report["channels"].items()
                          if key in ("master_temp", "master_humidity", "slave_temp", "slave_humidity")}
    if record["analysis"].get("result") is not None:
        record["analysis"]["result"] = {key: copy.deepcopy(value) for key, value in record["analysis"]["result"].items()
                                        if key in ("summary", "possible_causes", "suggested_checks", "limitations", "error")}
    record["archive_id"] = uuid.uuid4().hex
    if "sensor_review" in report:
        review = report["sensor_review"]
        if not isinstance(review, dict) or review.get("status") not in ("normal", "abnormal", "partial", "unavailable") or not isinstance(review.get("readings"), dict):
            raise ValueError("档案附加检查无效")
        readings = {}
        for kind, sensor in review["readings"].items():
            if kind not in ("rain", "smoke", "distance"):
                continue
            if not isinstance(sensor, dict) or sensor.get("state") not in ("normal", "abnormal", "unavailable"):
                raise ValueError("档案传感器状态无效")
            if any(not isinstance(sensor.get(key), str) for key in ("source", "kind", "unit", "message")) or sensor["source"] not in ("master", "slave") or sensor["kind"] != kind:
                raise ValueError("档案传感器字段无效")
            if any(sensor.get(key) is not None and (type(sensor[key]) not in (int, float) or not math.isfinite(sensor[key])) for key in ("current", "threshold")):
                raise ValueError("档案传感器数值无效")
            readings[kind] = {key: copy.deepcopy(sensor[key]) for key in ("source", "kind", "state", "current", "threshold", "unit", "message")}
            if "observed_at" in sensor:
                if not isinstance(sensor["observed_at"], str):
                    raise ValueError("档案传感器时间无效")
                readings[kind]["observed_at"] = sensor["observed_at"]
        ids = review.get("event_ids", [])
        if not isinstance(ids, list) or any(type(i) is not int for i in ids):
            raise ValueError("档案传感器事件无效")
        record["sensor_review"] = dict(status=review["status"], readings=readings, event_ids=list(ids))
    # Reports contain only local evidence and validated explanation fields; never config/context/keys.
    return record


class MemoryTrendArchive:
    persistent = False

    def __init__(self):
        self.records = []

    def load(self):
        return copy.deepcopy(self.records[-200:][::-1])

    def save(self, report):
        record = archive_record(report)
        self.records.append(record)
        self.records = self.records[-200:]
        return copy.deepcopy(record)


class FileTrendArchive:
    persistent = True

    def __init__(self, path=None):
        self.path = Path(path) if path is not None else Path(os.environ["LOCALAPPDATA"]) / "SensorVentilationArchives"

    def load(self):
        if not self.path.exists():
            return []
        paths = sorted((p for p in self.path.glob("*.json") if len(p.stem) == 32
                        and all(c in "0123456789abcdef" for c in p.stem)),
                       key=lambda p: p.stat().st_mtime_ns, reverse=True)
        records = []
        for path in paths[:200]:
            if path.stat().st_size > 131072:
                raise ValueError("档案过大，请检查本地档案文件")
            record = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(record, dict) or record.get("archive_id") != path.stem:
                raise ValueError("本地档案格式无效")
            cleaned = archive_record(record)
            cleaned["archive_id"] = path.stem
            records.append(cleaned)
        return records

    def save(self, report):
        record = archive_record(report)
        payload = json.dumps(record, ensure_ascii=False, allow_nan=False)
        if len(payload.encode("utf-8")) > 131072:
            raise ValueError("报告过大，未保存档案")
        self.path.mkdir(parents=True, exist_ok=True)
        # A completed .json is only visible after a full, flushed write. Failed .pending files remain for diagnosis.
        pending = self.path / (record["archive_id"] + ".pending")
        with pending.open("x", encoding="utf-8") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        pending.rename(self.path / (record["archive_id"] + ".json"))
        return record
