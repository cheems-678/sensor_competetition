"""UI-independent state machine. All mutation belongs to one controller thread."""

from __future__ import annotations

import math
import copy
import queue
import threading
import time
from datetime import datetime
from typing import Callable

from .protocol import FrameStreamParser, LoRaProtocol
from .warning_engine import WarningEngine, PROVIDERS
from .warning_service import ExplanationWorker
from .ai_client import validate_profile, validate_key
from .ai_config import MemoryAIStore, AIConfigError
from .trend_archive import MemoryTrendArchive
from .smoke_index import smoke_index
from urllib.parse import urlsplit

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
    SOUND_KEYS = ("sound_rms_1", "sound_rms_2") + tuple(f"sound_p2p_{i}" for i in range(1, 6))

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
        ai_store=None,
        archive_store=None,
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
        self.fan_confirmed = {channel: None for channel in self.FAN_PINS}
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
        self.warning_engine = WarningEngine(enabled=self.demo)
        self.ai_settings = {"provider": "deepseek", "profiles": copy.deepcopy(PROVIDERS)}
        self._ai_revision = 0
        self._analysis_sequence = 0
        self._analysis_tokens = {}
        self.explanations = ExplanationWorker()
        self._ai_store = ai_store or MemoryAIStore()
        self._ai_credentials = {}
        self.ai_mode = "simulation" if self.demo else "api"
        self.ai_test = dict(status="idle", message="", provider=None, model=None)
        self._ai_test_token = None
        self.manual_trend = None
        self._archive_store = archive_store or MemoryTrendArchive()
        self.archive_error = ""
        try:
            self.archives = self._archive_store.load()
        except (OSError, ValueError, KeyError, TypeError):
            self.archives = []
            self.archive_error = "本地档案读取失败，请检查档案文件；已有文件未覆盖。"
        self._manual_token = None
        self._ai_values = {key: None for key in ("master_temp", "slave_temp", "master_humidity", "slave_humidity")}
        self._load_ai_config()

    def _load_ai_config(self):
        try:
            saved = self._ai_store.load()
            if saved is None:
                return
            settings = copy.deepcopy(self.ai_settings)
            for provider, profile in saved["profiles"].items():
                if provider == "custom" and not profile["base_url"] and not profile["model"]:
                    continue
                address, model = validate_profile(provider, profile["base_url"], profile["model"])
                settings["profiles"][provider] = dict(base_url=address, model=model)
            if saved["provider"] not in PROVIDERS or saved["mode"] not in ("api", "simulation"):
                raise ValueError()
            credentials = {}
            for provider, credential in saved.get("credentials", {}).items():
                if provider not in PROVIDERS or credential["base_url"] != settings["profiles"][provider]["base_url"]:
                    raise ValueError()
                credentials[provider] = dict(api_key=validate_key(credential["api_key"]),
                                             base_url=credential["base_url"], remembered=True)
            settings["provider"] = saved["provider"]
            self.ai_settings, self.ai_mode, self._ai_credentials = settings, saved["mode"], credentials
        except Exception:
            self._show_notice("AI配置读取失败", "保存的AI配置无法读取或解密，请重新填写配置；原文件未修改")

    def _public_ai_settings(self):
        settings = copy.deepcopy(self.ai_settings)
        for provider, profile in settings["profiles"].items():
            credential = self._ai_credentials.get(provider)
            profile.update(has_key=bool(credential), remembered=bool(credential and credential["remembered"]))
        settings.update(mode=self.ai_mode, connection=copy.deepcopy(self.ai_test), persistent=self._ai_store.persistent)
        return settings

    def save_ai_config(self, provider, base_url, model, mode, api_key="", remember=True):
        if mode not in ("api", "simulation") or type(remember) is not bool or not isinstance(api_key, str):
            raise ValueError("AI配置无效")
        address, model = validate_profile(provider, base_url, model)
        remember = remember and self._ai_store.persistent
        credentials = copy.deepcopy(self._ai_credentials)
        if credentials.get(provider, {}).get("base_url") != address:
            credentials.pop(provider, None)
        if api_key:
            credentials[provider] = dict(api_key=validate_key(api_key), base_url=address, remembered=remember)
        elif provider in credentials:
            credentials[provider]["remembered"] = remember
        settings = copy.deepcopy(self.ai_settings)
        settings["provider"] = provider
        settings["profiles"][provider] = dict(base_url=address, model=model)
        # Encrypt/save before publishing new state; a storage failure keeps old configuration.
        saved_credentials = {p: dict(api_key=c["api_key"], base_url=c["base_url"]) for p, c in credentials.items() if c["remembered"]}
        self._ai_store.save(dict(version=1, provider=provider, mode=mode,
                                 profiles=settings["profiles"], credentials=saved_credentials))
        self.ai_settings, self.ai_mode, self._ai_credentials = settings, mode, credentials
        self._ai_revision += 1
        self._invalidate_analysis()
        self._changed()

    def _request_ai_config(self):
        provider = self.ai_settings["provider"]
        profile = self.ai_settings["profiles"][provider]
        credential = self._ai_credentials.get(provider)
        if not credential or credential["base_url"] != profile["base_url"]:
            raise ValueError("请先填写当前厂家的API Key并应用配置")
        validate_profile(provider, profile["base_url"], profile["model"])
        return dict(provider=provider, **profile, api_key=credential["api_key"])

    def _analysis_context(self):
        now = self.clock()
        stats = {}
        for key, series in self.warning_engine.series.items():
            if series:
                readings = [point[1] for point in series]
                stats[key] = dict(min=min(readings), max=max(readings), mean=sum(readings)/len(readings),
                                  count=len(readings), span_seconds=series[-1][0]-series[0][0],
                                  last_age_seconds=max(0, now-series[-1][0]))
        return dict(data_source="demo" if self.demo else "telemetry", requested_at=self._warning_stamp(),
                    connected=self.serial_port is not None, sample_id=self.sample_id,
                    telemetry_age_seconds=None if self.last_telemetry_at is None else max(0, now-self.last_telemetry_at),
                    current_measurements=copy.deepcopy(self._ai_values),
                    units=dict(temperature="Celsius", humidity="percent_RH", humidity_rate="percentage_points_per_minute"),
                    trends=copy.deepcopy(self.warning_engine.metrics), statistics=stats,
                    fans=[dict(channel=c, pwm_duty_percent=self.duties[c], confirmation=self.fan_status[c]) for c in self.FAN_PINS],
                    windows=[dict(channel=c, confirmation=self.window_statuses[c]) for c in self.SERVO_PINS])

    def test_ai_connection(self):
        if self.ai_mode != "api":
            raise ValueError("请应用真实API模式后再测试")
        if self._ai_test_token is not None:
            return
        config = self._request_ai_config()
        self._analysis_sequence += 1
        token = (0, self.connection_generation, self._ai_revision, self._analysis_sequence)
        if not self.explanations.submit(token, {"purpose": "connection_test"}, config, {"data_source": "connection_test", "message": "没有现场数据，仅验证接口能返回JSON分析结构"}):
            raise ValueError("分析队列繁忙，请稍后测试")
        self._ai_test_token = token
        self.ai_test = dict(status="pending", message="正在测试API…", provider=config["provider"], model=config["model"])
        self._changed()

    def _warning_stamp(self):
        return self.wall_clock().isoformat(timespec="seconds")

    def _invalidate_analysis(self):
        self.explanations.cancel(list(self._analysis_tokens.values()) + ([self._ai_test_token] if self._ai_test_token else [])
                                 + ([self._manual_token] if self._manual_token else []))
        self._analysis_tokens.clear()
        self._ai_test_token = None
        self._manual_token = None
        if self.manual_trend is not None:
            self.manual_trend["stale"] = True
            self.manual_trend["analysis"]["status"] = "stale"
        if self.ai_test["status"] != "idle":
            self.ai_test.update(status="stale", message="配置或连接已变化，请重新测试API")
        for event in self.warning_engine.events:
            if event["analysis"]["status"] != "idle":
                event["analysis"].update(status="stale")

    def update_warning_settings(self, enabled, temp_rate, humidity_rate):
        # Validate all inputs before changing any setting.
        if type(enabled) is not bool or any(type(v) not in (float, int) or not math.isfinite(v) or v <= 0
                                            for v in (temp_rate, humidity_rate)):
            raise ValueError("检测参数无效，速度必须为有限正数")
        if self.warning_engine.rates != {"temp": temp_rate, "humidity": humidity_rate}:
            self._ai_revision += 1
            self._invalidate_analysis()
        self.warning_engine.set_rates(temp_rate, humidity_rate, self._warning_stamp())
        self.warning_engine.set_enabled(enabled, self._warning_stamp())
        self._changed()

    def update_ai_settings(self, provider, base_url=None, model=None):
        if not isinstance(provider, str) or provider not in PROVIDERS:
            raise ValueError("厂家配置无效")
        if base_url is None and model is None:
            # Selection does not rewrite a profile from a potentially old UI snapshot.
            self.ai_settings["provider"] = provider
            self._ai_revision += 1
            self._invalidate_analysis()
            self._changed()
            return
        if not isinstance(base_url, str) or not isinstance(model, str):
            raise ValueError("厂家配置无效")
        base_url, model = base_url.strip(), model.strip()
        parts = urlsplit(base_url)
        if base_url and (parts.scheme != "https" or not parts.hostname or parts.username or parts.password or parts.query or parts.fragment):
            raise ValueError("接口地址必须为不含凭据的HTTPS地址")
        if len(base_url) > 512 or len(model) > 128:
            raise ValueError("配置内容过长")
        # Empty custom drafts are permitted in this offline phase.
        if provider != "custom" and (not base_url or not model):
            raise ValueError("接口地址和模型不能为空")
        self.ai_settings["provider"] = provider
        self.ai_settings["profiles"][provider] = dict(base_url=base_url, model=model)
        if self._ai_credentials.get(provider, {}).get("base_url") != base_url:
            self._ai_credentials.pop(provider, None)
        self._ai_revision += 1
        self._invalidate_analysis()
        self._changed()

    def mark_warning_read(self, event_id):
        event = self._warning_event(event_id)
        event["read"] = True
        self._changed()

    def delete_warning(self, event_id):
        event = self._warning_event(event_id)
        token = self._analysis_tokens.pop(event_id, None)
        if token is not None:
            self.explanations.cancel([token])
        self.warning_engine.events.remove(event)
        self.warning_engine.candidate.pop(event["key"], None)
        self.warning_engine.recovery.pop(event["key"], None)
        self._changed()

    def _manual_report(self, report_id):
        if type(report_id) is not int or self.manual_trend is None or self.manual_trend["id"] != report_id:
            raise ValueError("本次报告已变化，请重新操作")
        return self.manual_trend

    def delete_manual_trend(self, report_id):
        self._manual_report(report_id)
        if self._manual_token is not None:
            self.explanations.cancel([self._manual_token])
        self._manual_token = None
        self.manual_trend = None
        self._changed()

    def archive_manual_trend(self, report_id):
        report = self._manual_report(report_id)
        if report["analysis"]["status"] == "pending":
            raise ValueError("请等待分析完成再记录到档案")
        if report.get("archive_id"):
            return
        if self.archive_error:
            raise ValueError(self.archive_error)
        record = self._archive_store.save(report)
        report["archive_id"] = record["archive_id"]
        self.archives.insert(0, record)
        self.archives = self.archives[:200]
        self._changed()

    def _warning_event(self, event_id):
        if type(event_id) is not int:
            raise ValueError("事件编号无效")
        event = next((e for e in self.warning_engine.events if e["id"] == event_id), None)
        if event is None:
            raise ValueError("预警事件不存在")
        return event

    def analyze_warning(self, event_id):
        event = self._warning_event(event_id)
        if event_id in self._analysis_tokens:
            return
        config = self._request_ai_config() if self.ai_mode == "api" else None
        self._analysis_sequence += 1
        token = (event_id, self.connection_generation, self._ai_revision, self._analysis_sequence)
        evidence = {key: copy.deepcopy(event[key]) for key in ("kind", "source", "status", "occurred_at", "evidence")}
        if not self.explanations.submit(token, evidence, config, self._analysis_context()):
            event["analysis"].update(status="error", result={"error": "分析队列繁忙，请重试"})
        else:
            self._analysis_tokens[event_id] = token
            provider = self.ai_settings["provider"]
            event["analysis"] = dict(status="pending", result=None, provider=provider,
                                     model=self.ai_settings["profiles"][provider]["model"], mode=self.ai_mode,
                                     data_source="demo" if self.demo else "telemetry")
        self._changed()

    def monitor_trends(self):
        if self._manual_token is not None:
            return
        self._refresh_mq2(self.clock())
        self._refresh_ultrasonic(self.clock())
        self._check_sensor_warnings(self.clock(), self._warning_stamp())
        report = self.warning_engine.inspect(self.clock(), self._warning_stamp(),
                                            (self.serial_port is not None and self.last_telemetry_at is not None)
                                            or (self.demo and self.warning_engine.connected))
        report["sensor_review"] = self.warning_engine.inspect_sensors()
        report["event_ids"] = list(dict.fromkeys(report["event_ids"] + report["sensor_review"]["event_ids"]))
        self._analysis_sequence += 1
        report.update(id=self._analysis_sequence, data_source="demo" if self.demo else "telemetry",
                      analysis=dict(status="idle", result=None, provider=self.ai_settings["provider"],
                                    model=self.ai_settings["profiles"][self.ai_settings["provider"]]["model"],
                                    mode=self.ai_mode, data_source="demo" if self.demo else "telemetry"))
        self.manual_trend = report
        if report["status"] == "unavailable" and report["sensor_review"]["status"] == "unavailable":
            report["analysis"].update(status="error", result={"error": "没有可用数据，未请求AI；请先确认实测读数。"})
            self._changed()
            return
        try:
            config = self._request_ai_config() if self.ai_mode == "api" else None
        except ValueError as error:
            report["analysis"].update(status="error", result={"error": str(error)})
            self._changed()
            return
        token = (-1, self.connection_generation, self._ai_revision, self._analysis_sequence)
        evidence = dict(kind="trend_review", purpose="manual_trend_review", evidence=copy.deepcopy({
            key: report[key] for key in ("checked_at", "status", "message", "channels", "event_ids", "sensor_review")}))
        if self.explanations.submit(token, evidence, config, self._analysis_context()):
            self._manual_token = token
            report["analysis"]["status"] = "pending"
        else:
            report["analysis"].update(status="error", result={"error": "分析队列繁忙，请重新点击监测。"})
        self._changed()

    def _drain_explanations(self):
        while True:
            try:
                token, result, error = self.explanations.results.get_nowait()
            except queue.Empty:
                return
            event_id = token[0]
            if event_id == -1:
                if token == self._manual_token and self.manual_trend is not None:
                    self._manual_token = None
                    self.manual_trend["analysis"].update(status="error" if error else "complete",
                                                         result={"error": error} if error else result,
                                                         analyzed_at=self._warning_stamp())
                    self._changed()
                continue
            if event_id == 0:
                if token == self._ai_test_token:
                    self._ai_test_token = None
                    self.ai_test.update(status="error" if error else "complete", message=error or "API连接成功，模型返回格式有效", tested_at=self._warning_stamp())
                    self._changed()
                continue
            if self._analysis_tokens.get(event_id) != token:
                continue
            self._analysis_tokens.pop(event_id, None)
            event = next((e for e in self.warning_engine.events if e["id"] == event_id), None)
            if event is not None:
                event["analysis"].update(status="error" if error else "complete",
                                         result={"error": error} if error else result,
                                         analyzed_at=self._warning_stamp())
                self._changed()

    def set_warning_scenario(self, name):
        if not self.demo:
            raise ValueError("模拟场景仅限演示模式")
        from .warning_replay import run_scenario, SCENARIOS
        if name not in SCENARIOS:
            raise ValueError("未知模拟场景")
        self.warning_engine.stop(self._warning_stamp())
        self._invalidate_analysis()
        # Virtual samples end at current monotonic time; serial generation stays intact.
        self._ai_revision += 1
        now = self.clock()
        lengths = {"normal": 360, "warming": 400, "humidity": 400, "spike": 600,
                   "missing": 380, "timeout": 370, "recovery": 1050, "reconnect": 410}
        from datetime import timedelta
        end = self.wall_clock()
        length = lengths[name]
        run_scenario(self.warning_engine, name,
                     stamp=lambda second: (end + timedelta(seconds=second - length)).isoformat(timespec="seconds"),
                     offset=now-length)
        # Resume real demo sample numbering without accepting duplicate frames.
        self.warning_engine._sample_id = self.sample_id
        self._ai_values = {key: series[-1][1] if series and key in self.warning_engine.metrics else None
                           for key, series in self.warning_engine.series.items()}
        self.last_telemetry_at = now if any(value is not None for value in self._ai_values.values()) else None
        for key, value in self._ai_values.items():
            self.values[key] = "--" if value is None else f"{value:g} {'℃' if key.endswith('temp') else '%RH'}"
        self._changed()

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
        self.warning_engine.reset_window()
        self.warning_engine.connected = True
        self._invalidate_analysis()
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
        self.warning_engine.stop(self._warning_stamp())
        self._invalidate_analysis()
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
            self.fan_confirmed[channel] = None
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
        self._ai_values = {key: None for key in self._ai_values}
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
        self.warning_engine.observe_distance(None, self._warning_stamp(), self.clock())
        self.warning_engine.observe_smoke(None, self._warning_stamp(), self.clock())
        self.warning_engine.observe_rain(None, self._warning_stamp(), self.clock())
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
            self.warning_engine.observe_smoke(None, self._warning_stamp(), now)
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
            self.warning_engine.observe_distance(None, self._warning_stamp(), now)
            self._changed()
        elif age != self.ultrasonic["age_ms"]:
            self.ultrasonic["age_ms"] = age
            self._changed()

    def read_once(self):
        self._request_telemetry(True)

    def _check_sensor_warnings(self, now, stamp):
        available = self.serial_port is not None or (self.demo and self.warning_engine.connected)
        rain = self.rain["state"] if available and self.rain["source"] == "master" else None
        mq = self.mq2
        index = smoke_index(mq["pa7_mv"]) if available and mq["valid"] and mq["age_ms"] is not None and mq["age_ms"] < 2000 else None
        distance = self.ultrasonic["distance_mm"] if available and self.ultrasonic["valid"] and self.ultrasonic["age_ms"] is not None and self.ultrasonic["age_ms"] < 2000 else None
        self.warning_engine.observe_rain(rain, stamp, now)
        self.warning_engine.observe_smoke(index, stamp, now, mq["pa7_mv"] if index is not None else None, mq["raw"] if index is not None else None)
        self.warning_engine.observe_distance(distance, stamp, now)

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
                self.warning_engine.communication_timeout(self._warning_stamp(), now)
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
        self._drain_explanations()
        before = copy.deepcopy(self.warning_engine.metrics)
        statuses = [e["status"] for e in self.warning_engine.events]
        self.warning_engine.expire(self.clock())
        if before != self.warning_engine.metrics or statuses != [e["status"] for e in self.warning_engine.events]:
            self._changed()
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
                self.fan_confirmed[channel] = None
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
                self.warning_engine.communication_timeout(self._warning_stamp(), now)
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
            self._ai_values = {key: values[name] for key, name, _ in self.FIELD_LABELS if key in self._ai_values}
            self.warning_engine.observe(self.sample_id, now, self._warning_stamp(),
                                        {key: values[name] for key, name, _ in self.FIELD_LABELS
                                         if key.endswith(("temp", "humidity"))})
            self._check_sensor_warnings(now, self._warning_stamp())
            for key, name, unit in self.FIELD_LABELS:
                value = values[name]
                self.values[key] = "--" if value is None else f"{value:g} {unit}"
            for key in self.SOUND_KEYS:
                value = values.get(key)
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
                    self.fan_confirmed[channel] = duty if packet["data"][1] == 0 else None
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
                self.fan_confirmed[pending[0]] = None
                self._changed()

    def snapshot(self, after_log_id: int = 0) -> dict:
        busy = self._window_busy()
        warnings = self.warning_engine.snapshot()
        warnings["manual"] = copy.deepcopy(self.manual_trend)
        warnings["archives"] = copy.deepcopy(self.archives)
        warnings["archive_persistent"] = self._archive_store.persistent
        warnings["archive_error"] = self.archive_error
        return {
            "revision": self.revision, "demo": self.demo,
            "warnings": warnings, "ai_settings": self._public_ai_settings(),
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
                      "status": self.fan_status[ch], "confirmed_duty": self.fan_confirmed[ch]} for ch, pin in self.FAN_PINS.items()],
            "window": {"busy": busy, "status": self.window_status},
            "windows": [{"channel": ch, "pin": pin, "status": self.window_statuses[ch]}
                        for ch, pin in self.SERVO_PINS.items()],
            "logs": [dict(item) for item in self.logs if item["id"] > after_log_id],
            "last_log_id": len(self.logs), "notice": dict(self.notice) if self.notice else None,
        }

    def close(self):
        self.disconnect()
        self.explanations.close()
        self._ai_credentials.clear()
        self.database.close()
