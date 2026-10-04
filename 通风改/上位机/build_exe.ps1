$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot
$pythonExecutable = Join-Path $projectDirectory '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonExecutable)) {
    throw 'Project .venv is missing. Follow README.md first.'
}
Push-Location (Join-Path $projectDirectory 'frontend')
try {
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed.' }
} finally { Pop-Location }
$buildWorkspace = Join-Path $projectDirectory ('build\packaging\work-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& $pythonExecutable -m PyInstaller --noconfirm --distpath $projectDirectory --workpath $buildWorkspace (Join-Path $projectDirectory 'desktop.spec')
if ($LASTEXITCODE -ne 0) { throw 'Desktop packaging failed.' }
Write-Output (Join-Path $projectDirectory '智能通风系统_正式版.exe')
