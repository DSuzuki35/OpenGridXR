@echo off
setlocal
"%~dp0PortableStereoCheck.exe" --hardware > "%~dp0hardware-stereo-check.log" 2>&1
set "STEREO_CHECK_EXIT=%ERRORLEVEL%"
type "%~dp0hardware-stereo-check.log"
echo.
echo Result code: %STEREO_CHECK_EXIT%
pause
exit /b %STEREO_CHECK_EXIT%
