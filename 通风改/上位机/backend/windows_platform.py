"""Bound Windows metadata queries used by desktop dependencies; no OS changes."""
from __future__ import annotations
import ctypes
from ctypes import wintypes
import platform
import queue
import sys
import threading


def bounded_wmi_query(original, owner, timeout):
    """Keep real query results/errors; let CPython's native fallback handle timeouts."""
    def query(*args):
        if not owner._wmi:
            return original(*args)
        results = queue.SimpleQueue()
        def run():
            try:
                results.put((True, tuple(original(*args))))
            except Exception as exc:
                results.put((False, exc))
        worker = threading.Thread(target=run, name="Windows metadata query", daemon=True)
        worker.start()
        worker.join(timeout)
        if worker.is_alive():
            owner._wmi = None
            raise OSError("WMI metadata query timed out; use CPython native fallback")
        ok, value = results.get()
        if not ok:
            raise value
        return value
    return query


def native_machine():
    process_machine, architecture = wintypes.WORD(), wintypes.WORD()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.IsWow64Process2.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.WORD), ctypes.POINTER(wintypes.WORD)]
    if not kernel.IsWow64Process2(kernel.GetCurrentProcess(), ctypes.byref(process_machine), ctypes.byref(architecture)):
        raise ctypes.WinError(ctypes.get_last_error())
    known = {0x8664: "AMD64", 0xAA64: "ARM64", 0x014c: "x86"}
    if architecture.value not in known:
        raise OSError("Unknown Windows architecture")
    return known[architecture.value]


def install_bounded_platform_queries(timeout=2.0):
    if sys.platform != "win32" or not hasattr(platform, "_wmi_query"):
        return
    if getattr(platform._wmi_query, "_desktop_bounded", False):
        return
    query = bounded_wmi_query(platform._wmi_query, platform, timeout)
    query._desktop_bounded = True
    platform._wmi_query = query
    original_machine = platform._get_machine_win32
    def machine():
        return original_machine() or native_machine()
    platform._get_machine_win32 = machine
