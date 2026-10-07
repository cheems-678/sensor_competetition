"""Background API/simulation worker. No device or controller state access."""
from __future__ import annotations
import queue
import threading
from .ai_client import ChatAPIClient, AIClientError, validate_explanation


def simulated_explanation(event):
    kind = event["kind"]
    if kind == "trend_review":
        evidence = event["evidence"]
        sensors = evidence.get("sensor_review", {})
        suffix = "；附加检查：" + "；".join(item["message"] for item in sensors.get("readings", {}).values()) if sensors else ""
        return dict(summary=evidence["message"] + suffix, possible_causes=["需结合现场环境和传感器状态解释当前变化"],
                    suggested_checks=["比较主从机温湿度变化并继续观察", "确认有效数据时长及现场条件"],
                    limitations="模拟解释，未调用 API；不足两分钟的趋势不能完整判定，不代表现场环境全面正常。")
    label = {"temp": "温度", "humidity": "湿度", "communication": "通信", "distance": "粮面距离", "rain": "降雨", "smoke": "烟雾"}[kind]
    evidence = event["evidence"]
    if kind == "communication":
        summary = evidence["message"]
        causes = ["通信链路或设备应答可能异常"]
        checks = ["检查设备供电、串口连接与通信日志"]
    elif kind == "distance":
        summary = f"仓顶到粮面距离{evidence['current']:g} cm；过近条件<{evidence['threshold']:g} cm。当前状态：{event.get('status', 'active')}。"
        causes = ["粮面接近仓顶传感器", "传感器安装位置或回波条件变化"]
        checks = ["人工核对粮面与传感器的实际间距", "检查传感器安装及回波，不按相对图示推算储量"]
    elif kind == "rain":
        summary = f"当前雨滴检测：{'下雨' if evidence['current'] == 1 else '无雨'}；这是降雨提示，非传感器故障。"
        causes = ["传感器检测面遇水", "残留水滴或检测面状态变化"]
        checks = ["核对现场天气和传感器表面状态", "检查窗口和设备防雨条件，由人工决定操作"]
    elif kind == "smoke":
        summary = f"烟雾相对指数{evidence['current']:g}，超限条件>{evidence['threshold']:g}；当前状态：{event.get('status', 'active')}。"
        causes = ["烟雾或其他可燃气体响应变化", "模块预热、环境或传感器状态变化"]
        checks = ["核对PA7电压及现场情况", "检查传感器供电、接线和预热状态；指数不是浓度百分比或ppm"]
    else:
        summary = f"{label}两分钟趋势为{evidence['rate']:g}/分钟，超过配置{evidence['threshold']:g}/分钟。"
        causes = ["环境条件变化或通风条件变化", "测量位置或传感器状态变化"]
        checks = ["比较主从机实测读数并观察后续趋势", "检查传感器状态及现场通风条件"]
    return dict(summary=summary, possible_causes=causes, suggested_checks=checks,
                limitations="模拟解释，未调用 API；不能据此确定故障原因或粮食状态。")


class ExplanationWorker:
    def __init__(self, explain=simulated_explanation, client=None):
        self.jobs = queue.Queue(maxsize=32)
        self.results = queue.Queue()
        self.explain = explain
        self.thread = None
        self.closed = threading.Event()
        self.client = client or ChatAPIClient()
        self.cancelled = set()
        self.cancel_lock = threading.Lock()

    def submit(self, token, event, config=None, context=None):
        if self.closed.is_set():
            return False
        try:
            self.jobs.put_nowait((token, event, config, context))
        except queue.Full:
            return False
        if self.thread is None:
            self.thread = threading.Thread(target=self._run, name="AI warning analysis", daemon=True)
            self.thread.start()
        return True

    def _run(self):
        while not self.closed.is_set():
            try:
                token, event, config, context = self.jobs.get(timeout=0.1)
            except queue.Empty:
                continue
            try:
                if self.is_cancelled(token):
                    continue
                result = self.client.explain(event, context, config, lambda: self.is_cancelled(token)) if config else self.explain(event)
                result = validate_explanation(result)
                self.results.put((token, result, None))
            except AIClientError as error:
                self.results.put((token, None, str(error) if config else "模拟分析失败，请重试"))
            except Exception:
                self.results.put((token, None, "AI分析失败，请检查配置后重试" if config else "模拟分析失败，请重试"))
            finally:
                with self.cancel_lock:
                    self.cancelled.discard(token)

    def cancel(self, tokens):
        with self.cancel_lock:
            self.cancelled.update(tokens)

    def is_cancelled(self, token):
        with self.cancel_lock:
            return self.closed.is_set() or token in self.cancelled

    def close(self):
        self.closed.set()
        while True:
            try:
                self.jobs.get_nowait()
            except queue.Empty:
                break
        if self.thread:
            self.thread.join(timeout=1)
