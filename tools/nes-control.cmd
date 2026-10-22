@echo off
cd /d "%~dp0.."
python tools\nes.py start
if errorlevel 1 goto failed
python tools\nes.py play
if errorlevel 1 goto failed
exit /b 0
:failed
pause
exit /b 1
