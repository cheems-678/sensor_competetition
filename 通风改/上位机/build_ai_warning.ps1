param([string]$OutputName = '智能通风系统_AI预警演示版')
$ErrorActionPreference = 'Stop'
if ($OutputName -notin @('智能通风系统_AI预警演示版','智能通风系统_AI接口版','智能通风系统_AI主动监测版','智能通风系统_AI主动监测版_两分钟')) { throw 'Unsupported output name.' }
$warningProject = $PSScriptRoot
$warningPython = Join-Path $warningProject '.venv\Scripts\python.exe'
$warningOutput = Join-Path $warningProject ($OutputName + '.exe')
if (Test-Path -LiteralPath $warningOutput) { throw 'Output already exists; preserve it and choose a new output name.' }
Push-Location (Join-Path $warningProject 'frontend')
try {
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed.' }
} finally { Pop-Location }
$warningBuild = Join-Path $warningProject ('build\packaging\ai-warnings-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $warningBuild | Out-Null
$warningSpec = (Get-Content -LiteralPath (Join-Path $warningProject 'desktop.spec') -Raw).Replace('project = Path(SPECPATH)', 'project = Path(SPECPATH).parents[2]').Replace("name='智能通风系统_正式版'", ("name='" + $OutputName + "'")).Replace("excludes=['tkinter'", "excludes=['sitecustomize', 'tkinter'").Replace('runtime_hooks=[]', "runtime_hooks=[str(project / 'backend' / 'windows_platform_hook.py')]")
$warningSpecPath = Join-Path $warningBuild 'ai-warnings.spec'
Set-Content -LiteralPath $warningSpecPath -Value $warningSpec -Encoding utf8
$warningBootstrap = (Get-Content -LiteralPath (Join-Path $warningProject 'backend\windows_platform.py') -Raw) + "`ninstall_bounded_platform_queries()`n"
Set-Content -LiteralPath (Join-Path $warningBuild 'sitecustomize.py') -Value $warningBootstrap -Encoding utf8
$warningPreviousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = $warningBuild + $(if ($warningPreviousPythonPath) { ';' + $warningPreviousPythonPath } else { '' })
    & $warningPython -c "import platform,sysconfig; print('Build platform:', platform.platform()); print('Interpreter architecture:', sysconfig.get_platform()); assert platform.machine().lower() in ('amd64','x86_64') and sysconfig.get_platform() == 'win-amd64'"
    if ($LASTEXITCODE -ne 0) { throw 'Build platform verification failed.' }
    & $warningPython -m PyInstaller --noconfirm --distpath $warningProject --workpath (Join-Path $warningBuild 'work') $warningSpecPath
    if ($LASTEXITCODE -ne 0) { throw 'AI warning packaging failed.' }
} finally { $env:PYTHONPATH = $warningPreviousPythonPath }
Write-Output $warningOutput
