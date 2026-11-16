@echo off
rem Starts the operator console and opens its page. Needs Python 3.9 or newer (python.org).
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 neuro\console\console.py --open %*
) else (
  python neuro\console\console.py --open %*
)
if errorlevel 1 pause
