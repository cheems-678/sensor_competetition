@echo off
setlocal
cd /d "%~dp0"
if not exist ".venv\Scripts\pythonw.exe" (
  echo Project Python environment is missing. Follow README.md first.
  pause
  exit /b 1
)
start "" ".venv\Scripts\pythonw.exe" "main.py" %*
