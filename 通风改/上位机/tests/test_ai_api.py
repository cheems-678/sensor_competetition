"""Injected HTTP, isolated encrypted fixtures and memory databases only."""
import base64
import copy
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from urllib.error import HTTPError, URLError

ROOT = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(ROOT))
from backend.ai_client import ChatAPIClient, AIClientError, NoRedirect, validate_profile
from backend.ai_config import EncryptedAIStore, MemoryAIStore, AIConfigError, dpapi
from backend.controller import Controller
from backend.storage import TelemetryDatabase
from backend.warning_service import ExplanationWorker
from backend.warning_replay import run_scenario

FAKE_KEY = "qa-only-secret-not-a-real-key"
ANSWER = dict(summary="温度持续升高", possible_causes=["环境条件变化"],
              suggested_checks=["检查现场环境"], limitations="无法确定具体原因")


class FakeOpener:
    def __init__(self, answer=None, error=None, finish="stop"):
        self.answer, self.error, self.finish = answer or ANSWER, error, finish
        self.requests = []
        self.entered = threading.Event()

    def open(self, request, timeout):
        self.requests.append((request, timeout))
        self.entered.set()
        if self.error:
            raise self.error
        content = self.answer if isinstance(self.answer, str) else json.dumps(self.answer, ensure_ascii=False)
        return io.BytesIO(json.dumps({"choices": [{"finish_reason": self.finish, "message": {"content": content}}]}).encode())


class APIClientTests(unittest.TestCase):
    def config(self, provider="deepseek", **values):
        base = "https://api.moonshot.cn/v1" if provider == "kimi" else "https://api.deepseek.com"
        model = "kimi-k3" if provider == "kimi" else "deepseek-flash"
        return dict(provider=provider, base_url=base, model=model, api_key=FAKE_KEY, **values)

    def test_vendor_payloads_and_secret_not_in_prompt(self):
        for provider in ("deepseek", "kimi", "custom"):
            transport = FakeOpener()
            with patch("socket.socket", side_effect=AssertionError("external network forbidden")):
                result = ChatAPIClient(transport).explain({"kind": "temp"}, {"data_source": "telemetry"}, self.config(provider))
            request, timeout = transport.requests[0]
            self.assertEqual(timeout, 45)
            self.assertEqual(request.get_header("Authorization"), "Bearer " + FAKE_KEY)
            body = json.loads(request.data)
            self.assertNotIn(FAKE_KEY, json.dumps(body))
            self.assertEqual(result, ANSWER)
            self.assertFalse(body["stream"])
            if provider == "deepseek":
                self.assertEqual(body["thinking"], {"type": "disabled"})
                self.assertEqual(request.full_url, "https://api.deepseek.com/chat/completions")
            if provider == "kimi":
                self.assertEqual(body["reasoning_effort"], "low")
                self.assertEqual(request.full_url, "https://api.moonshot.cn/v1/chat/completions")
            if provider == "custom":
                self.assertNotIn("thinking", body)
                self.assertNotIn("response_format", body)

    def test_full_endpoint_does_not_duplicate_path_and_other_models_not_forced(self):
        config = self.config("kimi")
        config.update(model="kimi-other-model", base_url="https://api.moonshot.cn/v1/chat/completions/")
        transport = FakeOpener()
        ChatAPIClient(transport).explain({}, {}, config)
        self.assertTrue(transport.requests[0][0].full_url.endswith("/v1/chat/completions"))
        self.assertNotIn("reasoning_effort", json.loads(transport.requests[0][0].data))

    def test_http_errors_never_echo_upstream_or_secret(self):
        for code in (400, 401, 402, 403, 404, 429, 500):
            error = HTTPError("https://host/" + FAKE_KEY, code, FAKE_KEY, {}, io.BytesIO(FAKE_KEY.encode()))
            with self.assertRaises(AIClientError) as raised:
                ChatAPIClient(FakeOpener(error=error)).explain({}, {}, self.config())
            self.assertNotIn(FAKE_KEY, str(raised.exception))

    def test_timeouts_invalid_outputs_and_cancellation(self):
        cases = [(FakeOpener(error=TimeoutError(FAKE_KEY)), "超时"),
                 (FakeOpener(error=URLError(FAKE_KEY)), "无法连接"),
                 (FakeOpener(answer="invalid " + FAKE_KEY), "格式无效"),
                 (FakeOpener(finish="length"), "截断"),
                 (FakeOpener(answer={"summary": "bad"}), "格式无效")]
        for transport, message in cases:
            with self.assertRaisesRegex(AIClientError, message) as raised:
                ChatAPIClient(transport).explain({}, {}, self.config())
            self.assertNotIn(FAKE_KEY, str(raised.exception))
        transport = FakeOpener()
        with self.assertRaisesRegex(AIClientError, "取消"):
            ChatAPIClient(transport).explain({}, {}, self.config(), lambda: True)
        self.assertFalse(transport.requests)

    def test_code_fences_and_key_echo_redaction(self):
        answer = copy.deepcopy(ANSWER); answer["summary"] = FAKE_KEY
        result = ChatAPIClient(FakeOpener(answer="```json\n" + json.dumps(answer) + "\n```")).explain({}, {}, self.config())
        self.assertEqual(result["summary"], "[已隐藏]")

    def test_redirects_and_invalid_profile_are_rejected(self):
        with self.assertRaises(AIClientError):
            NoRedirect().redirect_request(None, None, 302, "", {}, "https://other")
        for address in ("http://host", "https://key@host", "https://host?key=x", "https://host#x"):
            with self.assertRaises(ValueError):
                validate_profile("custom", address, "model")


class ConfigTests(unittest.TestCase):
    def test_real_dpapi_file_survives_controller_restart_in_isolated_path(self):
        if sys.platform != "win32":
            self.skipTest("Windows DPAPI only")
        directory = pathlib.Path(tempfile.mkdtemp(prefix="ai-dpapi-restart-", dir=ROOT / "build/ui-preview"))
        store = EncryptedAIStore(directory / "restart.dpapi")
        first = Controller(TelemetryDatabase(":memory:"), ai_store=store, port_provider=lambda: [], start_readers=False)
        try:
            first.save_ai_config("deepseek", "https://api.deepseek.com", "deepseek-flash", "api", FAKE_KEY, True)
            self.assertNotIn(FAKE_KEY.encode(), store.path.read_bytes())
        finally:
            first.close()
        second = Controller(TelemetryDatabase(":memory:"), ai_store=store, port_provider=lambda: [], start_readers=False)
        try:
            profile = second.snapshot()["ai_settings"]["profiles"]["deepseek"]
            self.assertTrue(profile["has_key"])
            self.assertTrue(profile["remembered"])
            self.assertNotIn(FAKE_KEY, str(second.snapshot()))
        finally:
            second.close()

    def test_current_user_dpapi_roundtrip_without_plaintext_file(self):
        if sys.platform != "win32":
            self.skipTest("Windows DPAPI only")
        value = ("encrypted-fixture:" + FAKE_KEY).encode()
        ciphertext = dpapi(value)
        self.assertNotIn(FAKE_KEY.encode(), ciphertext)
        self.assertEqual(dpapi(ciphertext, decrypt=True), value)

    def test_atomic_encrypted_save_reload_and_failure_keeps_original(self):
        # Retain isolated QA fixtures; never touch the real AppData configuration.
        directory = pathlib.Path(tempfile.mkdtemp(prefix="ai-config-fixture-", dir=ROOT / "build/ui-preview"))
        encode = lambda value: base64.b64encode(bytes(byte ^ 97 for byte in value))
        decode = lambda value: bytes(byte ^ 97 for byte in base64.b64decode(value))
        store = EncryptedAIStore(directory / "test.dpapi", encode, decode)
        data = {"version": 1, "credentials": {"deepseek": FAKE_KEY}}
        store.save(data)
        self.assertNotIn(FAKE_KEY.encode(), store.path.read_bytes())
        self.assertEqual(store.load(), data)
        digest = hashlib.sha256(store.path.read_bytes()).hexdigest()
        with patch("backend.ai_config.os.replace", side_effect=OSError(FAKE_KEY)):
            with self.assertRaises(AIConfigError) as raised:
                store.save({"version": 1, "credentials": {}})
        self.assertNotIn(FAKE_KEY, str(raised.exception))
        self.assertEqual(hashlib.sha256(store.path.read_bytes()).hexdigest(), digest)
        store.path.write_bytes(b"corrupt QA ciphertext")
        with self.assertRaises(AIConfigError):
            store.load()
        self.assertEqual(store.path.read_bytes(), b"corrupt QA ciphertext")


class ControllerAPITests(unittest.TestCase):
    def setUp(self):
        self.store = MemoryAIStore(persistent=True)  # Injected restart fixture, never AppData.
        self.controller = Controller(TelemetryDatabase(":memory:"), ai_store=self.store,
                                     clock=lambda: 2000, port_provider=lambda: [], start_readers=False)
        self.transport = FakeOpener()
        self.controller.explanations = ExplanationWorker(client=ChatAPIClient(self.transport))

    def tearDown(self):
        self.controller.close()

    def save(self, provider="deepseek", key=FAKE_KEY, remember=True, address="https://api.deepseek.com"):
        self.controller.save_ai_config(provider, address, "deepseek-flash", "api", key, remember)

    def drain(self):
        token, result, error = self.controller.explanations.results.get(timeout=2)
        self.controller.explanations.results.put((token, result, error))
        self.controller._drain_explanations()

    def test_encrypted_preference_reload_and_public_snapshots_no_secret(self):
        self.save()
        self.controller.save_ai_config("kimi", "https://api.moonshot.cn/v1", "kimi-k3", "api", "qa-only-kimi-secret", True)
        other = Controller(TelemetryDatabase(":memory:"), ai_store=self.store, port_provider=lambda: [], start_readers=False)
        try:
            public = other.snapshot()["ai_settings"]
            self.assertTrue(public["profiles"]["deepseek"]["has_key"])
            self.assertTrue(public["profiles"]["kimi"]["remembered"])
            self.assertNotIn(FAKE_KEY, str(other.snapshot()))
            self.assertNotIn("qa-only-kimi-secret", str(other.snapshot()))
        finally:
            other.close()

    def test_memory_only_keys_and_endpoint_change_require_reentry(self):
        self.save(remember=False)
        self.assertTrue(self.controller.snapshot()["ai_settings"]["profiles"]["deepseek"]["has_key"])
        self.assertFalse(self.store.data["credentials"])
        self.controller.save_ai_config("deepseek", "https://other-host/v1", "model", "api", "", True)
        self.assertFalse(self.controller.snapshot()["ai_settings"]["profiles"]["deepseek"]["has_key"])
        with self.assertRaisesRegex(ValueError, "API Key"):
            self.controller.test_ai_connection()

    def test_storage_failure_is_transactional_and_no_configuration_on_startup(self):
        self.assertIsNone(self.store.data)
        original = self.controller.snapshot()["ai_settings"]
        with patch.object(self.store, "save", side_effect=AIConfigError("保存失败")):
            with self.assertRaises(AIConfigError):
                self.save()
        self.assertEqual(original, self.controller.snapshot()["ai_settings"])

    def test_connection_test_does_not_send_telemetry_and_analysis_is_real(self):
        self.save()
        self.controller.test_ai_connection()
        self.controller.test_ai_connection()
        self.drain()
        self.assertEqual(self.controller.ai_test["status"], "complete")
        body = json.loads(self.transport.requests[0][0].data)
        self.assertIn("connection_test", body["messages"][1]["content"])
        self.assertNotIn("current_measurements", body["messages"][1]["content"])
        self.controller.warning_engine.set_enabled(True, "qa")
        run_scenario(self.controller.warning_engine, "warming", offset=1600)
        event = self.controller.warning_engine.events[-1]
        self.controller.analyze_warning(event["id"])
        self.drain()
        self.assertEqual(event["analysis"]["mode"], "api")
        self.assertEqual(event["analysis"]["result"], ANSWER)
        self.assertNotIn(FAKE_KEY, str(self.controller.snapshot()))
        body = json.loads(self.transport.requests[-1][0].data)
        self.assertNotIn("raw_frame", str(body))
        self.assertNotIn("port\"", str(body))

    def test_failures_do_not_change_local_warning_or_fake_success(self):
        self.save()
        self.controller.explanations = ExplanationWorker(client=ChatAPIClient(FakeOpener(error=TimeoutError())))
        self.controller.warning_engine.set_enabled(True, "qa")
        run_scenario(self.controller.warning_engine, "warming")
        event = self.controller.warning_engine.events[-1]
        self.controller.analyze_warning(event["id"])
        self.drain()
        self.assertEqual(event["status"], "active")
        self.assertEqual(event["analysis"]["status"], "error")
        self.assertIn("超时", event["analysis"]["result"]["error"])

    def test_queued_requests_cancelled_on_config_switch(self):
        self.save()
        entered, release = threading.Event(), threading.Event()
        class BlockingClient:
            def explain(client, event, context, config, cancelled):
                entered.set(); release.wait(2)
                return ANSWER
        self.controller.explanations = ExplanationWorker(client=BlockingClient())
        self.controller.test_ai_connection()
        self.assertTrue(entered.wait(1))
        self.controller.warning_engine.set_enabled(True, "qa")
        run_scenario(self.controller.warning_engine, "warming")
        event = self.controller.warning_engine.events[-1]
        self.controller.analyze_warning(event["id"])
        self.controller.update_ai_settings("kimi")
        release.set()
        time.sleep(0.1)
        self.controller._drain_explanations()
        self.assertEqual(event["analysis"]["status"], "stale")
        self.assertEqual(self.controller.ai_test["status"], "stale")


if __name__ == "__main__":
    unittest.main()
