@echo off
setlocal
set "LMU_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%LMU_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%i in (`"%LMU_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LMU_VS=%%i"
if not defined LMU_VS exit /b 1
call "%LMU_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-msvc.ps1" %*
exit /b %ERRORLEVEL%
