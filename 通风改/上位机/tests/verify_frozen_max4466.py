"""Opt-in archive check: no desktop, physical serial, database or external API."""
import argparse
import hashlib
import json
import re
import types
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--acoustic-model", action="store_true")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    # Reuse the process-local bounded Windows metadata adapter for PyInstaller imports.
    import sys
    sys.path.insert(0, str(project))
    from backend.windows_platform import install_bounded_platform_queries
    install_bounded_platform_queries()
    from PyInstaller.archive.readers import CArchiveReader

    archive = CArchiveReader(str(args.exe))
    names = {name.replace("\\", "/"): name for name in archive.toc}
    scripts = [name for name, entry in archive.toc.items() if entry[-1] == "s"]
    assert scripts.index("windows_platform_hook") < scripts.index("pyi_rth_setuptools")
    index = archive.extract(names["frontend/build/index.html"])
    assert index == (project / "frontend/build/index.html").read_bytes()
    assets = []
    for reference in sorted(set(re.findall(r'(?:src|href)="\./([^"?#]+)', index.decode()))):
        data = archive.extract(names["frontend/build/" + reference])
        assert data == (project / "frontend/build" / reference).read_bytes()
        assets.append({"path": reference, "sha256": hashlib.sha256(data).hexdigest()})
        if reference.endswith(".js"):
            assert all(marker in data.decode() for marker in
                       ("主机声音 · MAX4466", "sound_p2p_", "ADC计数", "PA0", "PA4", "PA6", "PB0", "PA5"))
            if args.acoustic_model:
                assert all(marker in data.decode() for marker in ("粮仓声音点位", "应用阈值", "声学视角复位", "幅度超限"))
        elif reference.endswith(".css") and args.acoustic_model:
            assert "acoustic-alert" in data.decode() and "prefers-reduced-motion" in data.decode()

    pyz = archive.open_embedded_archive(next(name for name, entry in archive.toc.items() if entry[-1] == "z"))
    assert "sitecustomize" not in pyz.toc
    verified_modules = []
    # Verify current application code, not just the executable filename or UI label.
    for source in sorted((project / "backend").glob("*.py")) + [project / "desktop_check.py"]:
        name = "desktop_check" if source.name == "desktop_check.py" else "backend" if source.stem == "__init__" else "backend." + source.stem
        if name not in pyz.toc:
            continue  # Optional unimported helpers are not part of the product.
        code = pyz.extract(name)
        assert code == compile(source.read_text(encoding="utf-8"), code.co_filename, "exec"), name
        verified_modules.append(name)
    assert {"backend.protocol", "backend.controller", "backend.demo", "backend.service", "desktop_check"}.issubset(verified_modules)
    import marshal
    entry = marshal.loads(archive.extract("main"))
    assert entry == compile((project / "main.py").read_text(encoding="utf-8"), entry.co_filename, "exec")

    protocol_module = types.ModuleType("backend.protocol")
    protocol_module.__package__ = "backend"
    exec(pyz.extract("backend.protocol"), protocol_module.__dict__)
    sys.modules["backend.protocol"] = protocol_module
    demo_module = types.ModuleType("backend.demo")
    demo_module.__package__ = "backend"
    exec(pyz.extract("backend.demo"), demo_module.__dict__)
    protocol = protocol_module.LoRaProtocol
    transport = demo_module.DemoSerial()
    try:
        transport.write(protocol.cmd_read_telemetry(42))
        with transport._condition:
            transport._responses = [(0, frame) for _, frame in transport._responses]
        frame = transport.read(59)
        assert len(frame) == 59
        packet = protocol.parse_packet(frame)
        values = protocol.decode_telemetry(packet["data"])
        assert [values[f"sound_p2p_{i}"] for i in range(1, 6)] == [0, 186, 93, 47, 25]
        assert values["sound_rms_1"] is None and values["sound_rms_2"] is None
        assert values["mq2"]["valid"] and values["ultrasonic"]["distance_mm"] == 250
    finally:
        transport.close()
    report = {"passed": True, "assets": assets, "current_source_modules": verified_modules,
              "sample_frame_bytes": 59, "five_p2p": [0, 186, 93, 47, 25], "acoustic_model_assets": args.acoustic_model,
              "startup_hook_before_setuptools": True, "sitecustomize_bundled": False,
              "physical_serial_commands": 0, "production_database_access": False,
              "production_config_access": False, "external_api_calls": 0,
              "exe_sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
