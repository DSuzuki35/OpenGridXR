@echo off
setlocal
cd /d "%~dp0"
if exist "release\0.3.0\win-unpacked\OpenGrid XR.exe" (
  start "" "release\0.3.0\win-unpacked\OpenGrid XR.exe"
) else if exist "release\0.2.0\win-unpacked\OpenGrid XR.exe" (
  start "" "release\0.2.0\win-unpacked\OpenGrid XR.exe"
) else if exist "release\win-unpacked\OpenGrid XR.exe" (
  start "" "release\win-unpacked\OpenGrid XR.exe"
) else (
  call npm start
)
