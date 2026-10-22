@echo off
cd /d "%~dp0.."
python tools\quake2.py start
if errorlevel 1 goto failed
python tools\quake2.py play
if errorlevel 1 goto failed
exit /b 0
:failed
pause
exit /b 1
