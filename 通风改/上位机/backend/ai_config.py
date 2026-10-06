"""Current-user DPAPI storage. Plaintext exists only in memory; no secret logging."""
from __future__ import annotations
import copy
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import sys
import uuid


class AIConfigError(Exception):
    pass


class MemoryAIStore:
    def __init__(self, persistent=False):
        self.data = None
        self.persistent = persistent  # True only for an explicitly injected restart fixture.

    def load(self):
        return copy.deepcopy(self.data)

    def save(self, data):
        self.data = copy.deepcopy(data)


class Blob(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]


def dpapi(data, decrypt=False):
    if sys.platform != "win32":
        raise AIConfigError("本机加密保存需要Windows")
    buffer = ctypes.create_string_buffer(data, len(data))
    source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    target = Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LocalFree.argtypes = [wintypes.HLOCAL]
    kernel.LocalFree.restype = wintypes.HLOCAL
    if decrypt:
        operation = crypt.CryptUnprotectData
        description = None
    else:
        operation = crypt.CryptProtectData
        description = "SensorVentilation AI configuration"
    operation.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p if decrypt else wintypes.LPCWSTR,
                          ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    operation.restype = wintypes.BOOL
    if not operation(ctypes.byref(source), description, None, None, None, 1, ctypes.byref(target)):
        raise AIConfigError("无法解密本机AI配置，请在当前Windows用户下重新配置" if decrypt else "本机加密失败，配置未保存")
    try:
        return ctypes.string_at(target.data, target.size)
    finally:
        kernel.LocalFree(ctypes.cast(target.data, wintypes.HLOCAL))


class EncryptedAIStore:
    MAGIC = b"SENSOR-AI-DPAPI-1\n"
    persistent = True

    def __init__(self, path=None, encrypt=None, decrypt=None):
        base = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local")
        self.path = Path(path) if path is not None else base / "SensorVentilation" / "ai-config.dpapi"
        self.encrypt = encrypt or dpapi
        self.decrypt = decrypt or (lambda data: dpapi(data, decrypt=True))

    def load(self):
        if not self.path.exists():
            return None
        try:
            binary = self.path.read_bytes()
            if len(binary) > 65536 or not binary.startswith(self.MAGIC):
                raise ValueError()
            value = json.loads(self.decrypt(binary[len(self.MAGIC):]).decode("utf-8"))
            if not isinstance(value, dict) or value.get("version") != 1:
                raise ValueError()
            return value
        except Exception:
            raise AIConfigError("保存的AI配置无法读取或解密，请重新填写配置") from None

    def save(self, data):
        try:
            plain = json.dumps(data, ensure_ascii=False, allow_nan=False).encode("utf-8")
            encrypted = self.MAGIC + self.encrypt(plain)
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_name(self.path.name + "." + uuid.uuid4().hex + ".tmp")
            temporary.write_bytes(encrypted)
            os.replace(temporary, self.path)
        except Exception:
            # Never include raw data, upstream errors, or secrets in diagnostics.
            raise AIConfigError("本机加密配置保存失败，原配置保持不变") from None
