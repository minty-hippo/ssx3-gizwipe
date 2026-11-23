@echo off
rem SSX 3 GizWipe - build the ROM on Windows. Double-click, or drag your Gizmondo SSX 3 zip onto this file.
rem Everything missing (Python, numpy, Pillow, ffmpeg, MSYS2, BlocksDS) is installed on the first run.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_windows.ps1" %*
echo.
pause
