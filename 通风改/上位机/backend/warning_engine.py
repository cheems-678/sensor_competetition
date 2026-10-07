"""Local, forward-only warning state. Owned exclusively by the controller thread."""
from __future__ import annotations

import copy
import math
from collections import deque

FIELDS = {f"{side}_{kind}": (side, kind) for side in ("master", "slave")
          for kind in ("temp", "humidity")}
PROVIDERS = {
    "deepseek": {"base_url": "https://api.deepseek.com", "model": "deepseek-flash"},
    "kimi": {"base_url": "https://api.moonshot.cn/v1", "model": "kimi-k3"},
    "custom": {"base_url": "", "model": ""},
}


class WarningEngine:
    WINDOW = 120.0
    GAP = 10.0
    CONFIRM = 30.0
    RECOVER = 60.0
    NEAR_DISTANCE_MM = 100
    DISTANCE_RECOVER_MM = 110
    DISTANCE_RECOVER_SECONDS = 3.0
    SMOKE_LIMIT = 10

    def __init__(self, enabled=False):
        self.enabled = bool(enabled)
        self.rates = {"temp": 0.4, "humidity": 1.0}
        self.events = []
        self._next_id = 0
        self._sample_id = -1
        self.series = {key: deque(maxlen=1200) for key in FIELDS}
        self.candidate = {}
        self.recovery = {}
        self.metrics = {}
        self.connected = False
        self.sensors = {
            "rain": dict(source="master", kind="rain", state="unavailable", current=None, threshold=1, unit="状态", message="雨滴数据不可用"),
            "smoke": dict(source="slave", kind="smoke", state="unavailable", current=None, threshold=self.SMOKE_LIMIT, unit="相对指数", message="烟雾数据不可用"),
            "distance": dict(source="slave", kind="distance", state="unavailable", current=None, threshold=self.NEAR_DISTANCE_MM / 10, unit="cm", message="测距数据不可用"),
        }
        self._distance_recover_at = None
        self._distance_last_at = None

    def _event(self, key):
        return next((item for item in reversed(self.events)
                     if item["key"] == key and item["status"] in ("active", "unavailable")), None)

    def _create(self, key, source, kind, stamp, now, evidence):
        self._next_id += 1
        event = dict(id=self._next_id, key=key, source=source, kind=kind,
                     status="active", occurred_at=stamp, occurred_seconds=now,
                     ended_at=None, read=False, evidence=evidence,
                     trigger="automatic",
                     analysis={"status": "idle", "result": None, "provider": None, "model": None})
        self.events.append(event)
        if len(self.events) > 200:
            index = next((i for i, e in enumerate(self.events) if e["status"] in ("resolved", "stopped")), 0)
            self.events.pop(index)
        return event

    def stop(self, stamp):
        for event in self.events:
            if event["status"] in ("active", "unavailable"):
                event.update(status="stopped", ended_at=stamp)
        self.reset_window()
        self.connected = False
        for sensor in self.sensors.values():
            sensor.update(state="unavailable", current=None, message="已断开，数据不可用")
        self._distance_recover_at = self._distance_last_at = None

    def reset_window(self):
        self._sample_id = -1
        for series in self.series.values():
            series.clear()
        self.candidate.clear()
        self.recovery.clear()
        self.metrics.clear()

    def set_enabled(self, enabled, stamp):
        if type(enabled) is not bool:
            raise ValueError("检测开关必须为布尔值")
        if self.enabled != enabled:
            for event in self.events:
                if event["kind"] in ("temp", "humidity") and event.get("trigger") != "manual" and event["status"] in ("active", "unavailable"):
                    event.update(status="stopped", ended_at=stamp)
            self.candidate.clear()
            self.recovery.clear()
        self.enabled = enabled

    def set_rates(self, temp, humidity, stamp):
        if any(type(value) not in (float, int) or not math.isfinite(value) or value <= 0
               for value in (temp, humidity)):
            raise ValueError("变化速度必须为有限正数")
        if self.rates != {"temp": temp, "humidity": humidity}:
            for event in self.events:
                if event["kind"] in ("temp", "humidity") and event["status"] in ("active", "unavailable"):
                    event.update(status="stopped", ended_at=stamp)
            self.candidate.clear()
            self.recovery.clear()
            self.rates = {"temp": float(temp), "humidity": float(humidity)}

    def communication_timeout(self, stamp, now):
        if self.connected and self._event("communication") is None:
            self._create("communication", "link", "communication", stamp, now,
                         {"message": "遥测应答超时，当前环境状态未知"})

    def _sensor_result(self, kind, key, stamp, now, current, abnormal, message, extra=None):
        sensor = self.sensors[kind]
        event = self._event(key)
        if current is None:
            sensor.update(state="unavailable", current=None, message=message)
            if event:
                event["status"] = "unavailable"
            return
        sensor.update(state="abnormal" if abnormal else "normal", current=current, message=message, observed_at=stamp)
        evidence = {field: copy.deepcopy(sensor[field]) for field in ("current", "threshold", "unit", "message")}
        evidence["trigger_value"] = event["evidence"].get("trigger_value", event["evidence"].get("current")) if event else current
        evidence.update(detected_at=stamp, **(extra or {}))
        if abnormal:
            if event is None:
                self._create(key, sensor["source"], kind, stamp, now, evidence)
            else:
                event.update(status="active", evidence=evidence)
        elif event:
            event.update(status="resolved", ended_at=stamp, evidence=evidence)

    def observe_rain(self, rain_state, stamp, now):
        valid = type(rain_state) is int and rain_state in (0, 1)
        self._sensor_result("rain", "master_rain", stamp, now, rain_state if valid else None,
                            rain_state == 1 if valid else False,
                            "下雨（降雨提示）" if rain_state == 1 and valid else "无雨" if valid else "雨滴数据不可用",
                            {"rain_state": rain_state if valid else None, "condition": "雨滴状态=1"})

    def observe_smoke(self, index, stamp, now, pa7_mv=None, raw=None):
        valid = type(index) in (int, float) and math.isfinite(index) and 0 <= index <= 100
        self._sensor_result("smoke", "slave_smoke", stamp, now, index if valid else None,
                            index > self.SMOKE_LIMIT if valid else False,
                            "烟雾相对指数超限" if valid and index > self.SMOKE_LIMIT else "正常检测" if valid else "烟雾数据不可用",
                            {"pa7_mv": pa7_mv, "raw": raw, "condition": "相对指数>10"})

    def observe_distance(self, distance_mm, stamp, now):
        event = self._event("slave_distance")
        valid = type(distance_mm) in (int, float) and math.isfinite(distance_mm) and 50 <= distance_mm <= 500
        if not valid:
            self._distance_recover_at = self._distance_last_at = None
            self._sensor_result("distance", "slave_distance", stamp, now, None, False, "测距数据不可用")
            return
        if self._distance_last_at is not None and (now < self._distance_last_at or now - self._distance_last_at >= 2):
            self._distance_recover_at = None
        self._distance_last_at = now
        abnormal = distance_mm < self.NEAR_DISTANCE_MM
        if event:
            if distance_mm >= self.DISTANCE_RECOVER_MM:
                if self._distance_recover_at is None:
                    self._distance_recover_at = now
                abnormal = now - self._distance_recover_at < self.DISTANCE_RECOVER_SECONDS
            else:
                self._distance_recover_at = None
                abnormal = True
        else:
            self._distance_recover_at = None
        message = "粮面距离过近" if abnormal else "正常检测"
        if abnormal and distance_mm >= self.NEAR_DISTANCE_MM:
            message = "粮面距离过近：等待≥11 cm连续有效3秒恢复"
        self._sensor_result("distance", "slave_distance", stamp, now, distance_mm / 10, abnormal, message,
                            {"distance_mm": distance_mm, "condition": "距离<10 cm",
                             "recover_threshold": self.DISTANCE_RECOVER_MM / 10, "recover_seconds": self.DISTANCE_RECOVER_SECONDS})

    def inspect_sensors(self):
        states = [sensor["state"] for sensor in self.sensors.values()]
        status = "abnormal" if "abnormal" in states else "unavailable" if all(s == "unavailable" for s in states) else "partial" if "unavailable" in states else "normal"
        event_ids = [event["id"] for key in ("master_rain", "slave_smoke", "slave_distance") if (event := self._event(key))]
        return dict(status=status, readings=copy.deepcopy(self.sensors), event_ids=event_ids)

    def expire(self, now):
        for key, series in self.series.items():
            if series and now - series[-1][0] > self.GAP:
                series.clear()
                self.candidate.pop(key, None)
                self.recovery.pop(key, None)
                self.metrics.pop(key, None)
                event = self._event(key)
                if event:
                    event["status"] = "unavailable"

    @staticmethod
    def slope(series):
        origin = series[0][0]
        xs = [point[0] - origin for point in series]
        ys = [point[1] for point in series]
        mean_x, mean_y = sum(xs) / len(xs), sum(ys) / len(ys)
        denominator = sum((x - mean_x) ** 2 for x in xs)
        return 60 * sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / denominator

    def observe(self, sample_id, now, stamp, readings):
        if sample_id <= self._sample_id:
            return
        self._sample_id = sample_id
        self.connected = True
        comm = self._event("communication")
        if comm:
            comm.update(status="resolved", ended_at=stamp)
        self.expire(now)
        for key, (side, kind) in FIELDS.items():
            value = readings.get(key)
            series = self.series[key]
            if type(value) not in (float, int) or not math.isfinite(value):
                self.candidate.pop(key, None)
                self.recovery.pop(key, None)
                self.metrics.pop(key, None)
                event = self._event(key)
                if event:
                    event["status"] = "unavailable"
                continue
            if series and now <= series[-1][0]:
                continue
            series.append((now, float(value)))
            # Preserve one point at/before the boundary for a full measured span.
            while len(series) > 2 and series[1][0] <= now - self.WINDOW:
                series.popleft()
            span = now - series[0][0]
            self.metrics[key] = {"span_seconds": min(span, self.WINDOW), "rate": None}
            if span < self.WINDOW:
                continue
            rate = self.slope(series)
            self.metrics[key]["rate"] = rate
            limit = self.rates[kind]
            event = self._event(key)
            evidence = dict(current=value, rate=round(rate, 6), threshold=limit,
                            window_seconds=self.WINDOW, sample_count=len(series))
            if event:
                event.update(status="active", evidence=evidence)
                if rate < limit * 0.8:
                    self.recovery.setdefault(key, now)
                    if now - self.recovery[key] >= self.RECOVER:
                        event.update(status="resolved", ended_at=stamp)
                        self.recovery.pop(key, None)
                else:
                    self.recovery.pop(key, None)
            elif self.enabled and rate >= limit - 1e-9:
                self.candidate.setdefault(key, now)
                if now - self.candidate[key] >= self.CONFIRM:
                    self._create(key, side, kind, stamp, now, evidence)
                    self.candidate.pop(key, None)
            else:
                self.candidate.pop(key, None)

    def inspect(self, now, stamp, connected):
        """Freeze a manual assessment; short/invalid channels never count as normal."""
        self.expire(now)
        channels = {}
        event_ids = []
        for key, (side, kind) in FIELDS.items():
            series = self.series[key]
            available = bool(connected and series and key in self.metrics and now - series[-1][0] <= self.GAP)
            span = min(series[-1][0] - series[0][0], self.WINDOW) if available else 0
            rate = self.slope(series) if available and len(series) >= 2 else None
            complete = available and span >= self.WINDOW
            exceeded = complete and rate >= self.rates[kind] - 1e-9
            deadband = min(0.02 if kind == "temp" else 0.05, self.rates[kind] * 0.1)
            direction = "unknown" if rate is None else "rising" if rate > deadband else "falling" if rate < -deadband else "stable"
            values = [point[1] for point in series] if available else []
            channels[key] = dict(source=side, kind=kind, state="ready" if complete else "insufficient" if available else "unavailable",
                                 direction=direction, current=values[-1] if values else None,
                                 start=values[0] if values else None, rate=rate, threshold=self.rates[kind],
                                 span_seconds=span, sample_count=len(values), exceeded=bool(exceeded),
                                 min=min(values) if values else None, max=max(values) if values else None)
            if exceeded:
                evidence = dict(current=values[-1], rate=round(rate, 6), threshold=self.rates[kind],
                                window_seconds=self.WINDOW, sample_count=len(values))
                event = self._event(key)
                if event is None:
                    event = self._create(key, side, kind, stamp, now, evidence)
                    event["trigger"] = "manual"
                else:
                    event.update(status="active", evidence=evidence)
                event_ids.append(event["id"])
        if event_ids:
            status, message = "abnormal", "手动检查发现上升趋势超限，已生成或更新预警。"
        elif all(item["state"] == "ready" for item in channels.values()):
            status, message = "normal", "本次未发现配置中的上升趋势超限。"
        elif any(item["state"] == "ready" for item in channels.values()):
            status, message = "partial", "部分通道未发现超限，其余数据不足或不可用，不能完整判定。"
        elif any(item["state"] == "insufficient" for item in channels.values()):
            status, message = "insufficient", "有效数据不足两分钟，仅显示短时趋势，尚不能完整判定预警。"
        else:
            status, message = "unavailable", "没有可用温湿度数据，请确认连接及传感器读数。"
        return dict(checked_at=stamp, status=status, message=message, channels=channels,
                    event_ids=event_ids, stale=False)

    def snapshot(self):
        return copy.deepcopy(dict(enabled=self.enabled, rates=self.rates, metrics=self.metrics, window_seconds=self.WINDOW,
                                  near_distance_mm=self.NEAR_DISTANCE_MM,
                                  sensors=self.sensors,
                                  active_count=sum(e["status"] in ("active", "unavailable") for e in self.events),
                                  events=list(reversed(self.events))))
