"""智能通风系统桌面入口；所有设备行为由独立控制线程管理。"""
from __future__ import annotations

import argparse
import ctypes
import functools
import json
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


APP_DIR = Path(__file__).resolve().parent


def startup_error(message: str, *, dialog=True):
    if sys.stderr is not None:
        print(message, file=sys.stderr)
    if dialog and sys.platform == "win32" and sys.stderr is None:
        ctypes.windll.user32.MessageBoxW(None, message, "智能通风系统", 0x10)
    return 1


def resource_dir() -> Path:
    """Support source checkout and a future PyInstaller resource bundle."""
    return Path(getattr(sys, "_MEIPASS", APP_DIR)) / "frontend" / "build"


def database_path() -> str:
    if getattr(sys, "frozen", False):
        return str(Path(sys.executable).resolve().parent / "sensor_data.db")
    return "sensor_data.db"


def fit_window(work_width: int, work_height: int, scale: float = 1.0):
    """Return logical sizes fitting the work area, including a margin for chrome."""
    available_width = max(320, int(work_width / scale) - 32)
    available_height = max(240, int(work_height / scale) - 48)
    minimum = (min(880, available_width), min(640, available_height))
    return min(1120, available_width), min(800, available_height), minimum


def window_geometry():
    if sys.platform == "win32":
        from ctypes import wintypes
        user32 = ctypes.windll.user32
        # This affects this application's process only, never Windows settings.
        user32.SetProcessDPIAware()
        area = wintypes.RECT()
        if user32.SystemParametersInfoW(0x0030, 0, ctypes.byref(area), 0):
            dpi = user32.GetDpiForSystem() if hasattr(user32, "GetDpiForSystem") else 96
            return fit_window(area.right - area.left, area.bottom - area.top, dpi / 96)
    return 1120, 800, (880, 640)


class AssetHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass


class AssetServer:
    """Serve only bundled static assets on a random loopback port; no device API."""
    def __init__(self, directory: Path):
        handler = functools.partial(AssetHandler, directory=str(directory))
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       name="desktop-assets", daemon=True)

    def start(self, demo=False):
        self.thread.start()
        return f"http://127.0.0.1:{self.server.server_port}/index.html?desktop=1&demo={int(demo)}"

    def close(self):
        if self.thread.is_alive():
            self.server.shutdown()
            self.thread.join(timeout=2)
        self.server.server_close()


def run(argv=None) -> int:
    parser = argparse.ArgumentParser(description="智能通风系统桌面监控")
    parser.add_argument("--demo", action="store_true", help="模拟串口与内存数据库，禁止接触硬件")
    parser.add_argument("--legacy", action="store_true", help="运行保留的 Tkinter 界面")
    parser.add_argument("--check", action="store_true", help="只检查桌面资源与依赖，不创建数据库或打开串口")
    parser.add_argument("--self-test-output", help=argparse.SUPPRESS)
    parser.add_argument("--self-test-sample", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.legacy and (args.demo or args.check):
        parser.error("--legacy 不可与 --demo 或 --check 同时使用；隔离预览请直接使用 --demo。")
    if args.self_test_output and (args.demo or args.legacy or args.check):
        parser.error("隔离打包验收不可与其他运行模式组合。")
    if args.self_test_sample and not args.self_test_output:
        parser.error("--self-test-sample 需要 --self-test-output。")
    if args.legacy:
        try:
            from legacy_tk import MonitorApp
        except ModuleNotFoundError as exc:
            if exc.name != "tkinter":
                raise
            return startup_error("当前 Python 不带 Tkinter。请用原来的 Python 环境运行 legacy_tk.py，例如 py -3.14 legacy_tk.py。")
        MonitorApp().mainloop()
        return 0
    assets = resource_dir()
    if not (assets / "index.html").is_file():
        return startup_error("缺少界面构建资源。请在 frontend/ 执行 npm ci 和 npm run build。", dialog=not args.check)
    try:
        import webview
    except ImportError:
        return startup_error("缺少桌面依赖。请用本目录 .venv/Scripts/python.exe -m pip install -r requirements.txt。", dialog=not args.check)
    if args.check:
        print(json.dumps({"assets": str(assets), "runtime": sys.version.split()[0],
                          "window": window_geometry(), "hardware_access": False}, ensure_ascii=False))
        return 0
    if sys.platform != "win32":
        print("当前桌面入口面向 Windows WebView2；其他系统可使用 --legacy。", file=sys.stderr)
        return 1
    from backend.service import ControllerService, DesktopAPI
    probe = None
    if args.self_test_output:
        from desktop_check import PackagingCheck
        probe = PackagingCheck(args.self_test_output, args.self_test_sample)
    service = probe.service() if probe else ControllerService(demo=args.demo, db_path=database_path())
    server = AssetServer(assets)
    startup_exception = None
    try:
        from webview.guilib import initialize
        if initialize("edgechromium").renderer != "edgechromium":
            raise RuntimeError("缺少可用的 WebView2 Runtime，请安装后重新打开正式程序")
        service.start()
        if probe:
            probe.prepare_sample(service)
        url = server.start(demo=args.demo)
        width, height, minimum = window_geometry()
        window = webview.create_window(
            "智能通风系统" + (" · 隔离演示" if args.demo else ""), url,
            js_api=probe.api(service) if probe else DesktopAPI(service), width=width, height=height,
            min_size=minimum, background_color="#F6F7F9", text_select=True,
            minimized=bool(probe), focus=not bool(probe),
        )
        window.events.closed += service.close
        webview.start(func=probe.observe_and_close if probe else None,
                      args=(window,) if probe else None,
                      gui="edgechromium", debug=False, private_mode=True)
    except Exception as exc:
        startup_exception = exc
        if not probe:
            return startup_error(f"桌面启动失败：{exc}。请确认 Windows 已安装 WebView2 Runtime。")
    finally:
        service.close()
        server.close()
    if probe:
        return 0 if probe.save(webview.renderer, startup_exception) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
