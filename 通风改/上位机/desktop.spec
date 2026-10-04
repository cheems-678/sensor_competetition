# -*- mode: python ; coding: utf-8 -*-
"""One-file Windows production build; use the local Python 3.13 environment."""
from importlib.metadata import distribution
from pathlib import Path
import re

project = Path(SPECPATH)
frontend = project / 'frontend' / 'build'
index = frontend / 'index.html'
if not index.is_file():
    raise RuntimeError('Build frontend before packaging the desktop application')

# Bundle only assets referenced by the current index; retain historical build files.
datas = [(str(index), 'frontend/build'), (str(project / 'THIRD_PARTY_NOTICES.md'), '.')]
for reference in set(re.findall(r'(?:src|href)="\./([^"?#]+)', index.read_text(encoding='utf-8'))):
    asset = (frontend / reference).resolve()
    if frontend.resolve() not in asset.parents or not asset.is_file():
        raise RuntimeError('Invalid frontend asset reference: ' + reference)
    datas.append((str(asset), 'frontend/build/' + str(Path(reference).parent)))

for package in ('pywebview', 'pythonnet', 'clr_loader', 'pyserial', 'bottle', 'proxy_tools',
                'typing_extensions', 'cffi', 'pycparser'):
    dist = distribution(package)
    for file in dist.files or []:
        if file.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'AUTHORS')):
            source = Path(dist.locate_file(file))
            if source.is_file():
                datas.append((str(source), 'licenses/python/' + package))

node_modules = project / 'frontend' / 'node_modules'
for package in ('react', 'react-dom', '@radix-ui/react-slot', 'class-variance-authority', 'clsx',
                'tailwind-merge', 'tailwindcss', 'lucide-react'):
    directory = node_modules / package
    for source in directory.glob('*'):
        if source.is_file() and source.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')):
            datas.append((str(source), 'licenses/frontend/' + package))

a = Analysis(
    [str(project / 'main.py')], pathex=[str(project)], binaries=[], datas=datas,
    hiddenimports=['webview.platforms.winforms', 'webview.platforms.edgechromium',
                   'serial.tools.list_ports_windows'], hookspath=[], hooksconfig={},
    runtime_hooks=[], excludes=['tkinter', '_tkinter', 'PyQt5', 'PyQt6', 'PySide2', 'PySide6',
                              'gi', 'gtk', 'cefpython3', 'webview.platforms.qt',
                              'webview.platforms.gtk', 'webview.platforms.cef',
                              'webview.platforms.mshtml'], noarchive=False, optimize=0,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name='智能通风系统_正式版',
          debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
          console=False, disable_windowed_traceback=False, argv_emulation=False,
          target_arch=None, codesign_identity=None, entitlements_file=None)
