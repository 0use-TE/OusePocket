@echo off
cd /d "%~dp0.."
python tools\doom.py start
if errorlevel 1 goto failed
python tools\doom.py play
if errorlevel 1 goto failed
exit /b 0
:failed
pause
exit /b 1
