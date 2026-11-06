@echo off
setlocal
rem NVIDIA Variable Rate Shading check (no game or headset needed; close LMU first).
"%~dp0VrsHardwareCheck.exe" > "%~dp0hardware-vrs-check.log" 2>&1
set "VRS_CHECK_EXIT=%ERRORLEVEL%"
type "%~dp0hardware-vrs-check.log"
echo.
echo Result code: %VRS_CHECK_EXIT% (0 = VRS works, per-eye maps verified)
pause
exit /b %VRS_CHECK_EXIT%
