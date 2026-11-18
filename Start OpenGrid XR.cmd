@echo off
setlocal
cd /d "%~dp0"
rem Read the version from package.json so the launcher follows each release.
set "VERSION="
for /f "tokens=2 delims=:, " %%v in ('findstr /r /c:"^ *\"version\":" package.json') do if not defined VERSION set "VERSION=%%~v"
if defined VERSION if exist "release\%VERSION%\win-unpacked\OpenGrid XR.exe" (
  start "" "release\%VERSION%\win-unpacked\OpenGrid XR.exe"
  exit /b 0
)
if exist "release\win-unpacked\OpenGrid XR.exe" (
  start "" "release\win-unpacked\OpenGrid XR.exe"
  exit /b 0
)
rem Development start: Electron loads dist\index.html, so build it first.
if not exist "dist\index.html" (
  call npm run build
  if errorlevel 1 exit /b 1
)
call npm start
