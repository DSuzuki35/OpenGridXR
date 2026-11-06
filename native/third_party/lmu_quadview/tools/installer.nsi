; Compiled only by build-installer.ps1, which supplies validated, explicit file lists.
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!define APP_NAME "LMU Quad View"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LMUQuadView"
Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\LMU Quad View"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
SetCompressor /SOLID lzma
SetDatablockOptimize on
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "LMU Quad View Mod"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "FileDescription" "LMU Quad View per-user installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "LMU Quad View contributors"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Install LMU Quad View for your Windows account.$\r$\n$\r$\nSettings, profiles, logs and runtime data survive upgrades and removal.$\r$\n$\r$\nInstallation does not change OpenXR registration. Open the launcher and use Setup when you want automatic startup. After an upgrade, reinstall automatic startup from Setup to approve the updated binaries."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "LMU Quad View is installed.$\r$\n$\r$\nOpen LMU Quad View from the Start menu and use Setup to configure your game and optional automatic startup."
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!include "${PAYLOAD_INCLUDE}"

; PROCESSENTRY32W is 556 bytes for this 32-bit Unicode bootstrapper, even on x64.
; Toolhelp is part of Windows: no helper process, WMI, or downloaded NSIS plugin.
!macro SafetyFunctions PREFIX
Function ${PREFIX}CheckProcesses
  System::Call 'kernel32::CreateToolhelp32Snapshot(i 2, i 0) p.r0'
  ${If} $0 == -1
    Goto cannotCheck
  ${EndIf}
  System::Alloc 556
  Pop $1
  ${If} $1 == 0
    System::Call 'kernel32::CloseHandle(p r0)'
    Goto cannotCheck
  ${EndIf}
  System::Call '*$1(i 556)'
  System::Call 'kernel32::Process32FirstW(p r0, p r1) i.r2'
  ${If} $2 == 0
    System::Free $1
    System::Call 'kernel32::CloseHandle(p r0)'
    Goto cannotCheck
  ${EndIf}
  processLoop:
    IntOp $3 $1 + 36
    System::Call '*$3(&w260 .r4)'
    ${If} $4 == "Le Mans Ultimate.exe"
    ${OrIf} $4 == "LMU-QuadView-Launcher.exe"
      System::Free $1
      System::Call 'kernel32::CloseHandle(p r0)'
      MessageBox MB_OK|MB_ICONSTOP "Close Le Mans Ultimate and every LMU Quad View launcher before installing, upgrading or removing LMU Quad View." /SD IDOK
      SetErrorLevel 2
      Quit
    ${EndIf}
    System::Call 'kernel32::Process32NextW(p r0, p r1) i.r2 ?e'
    Pop $5
    ${If} $2 <> 0
      Goto processLoop
    ${EndIf}
    System::Free $1
    System::Call 'kernel32::CloseHandle(p r0)'
    ${If} $5 == 18
      Return
    ${EndIf}
  cannotCheck:
    MessageBox MB_OK|MB_ICONSTOP "Cannot check running applications. No package files have been changed. Close this installer and try again." /SD IDOK
    SetErrorLevel 3
    Quit
FunctionEnd

Function ${PREFIX}CheckPlainDirectory
  Pop $6
  ; Inspect the directory and every ancestor. Never follow junctions during removal.
  checkParent:
    System::Call 'kernel32::GetFileAttributesW(w r6) i.r7'
    ${If} $7 != -1
      IntOp $8 $7 & 0x400
      ${If} $8 != 0
        MessageBox MB_OK|MB_ICONSTOP "A package directory is a junction or symbolic link. No files will be changed: $6" /SD IDOK
        SetErrorLevel 2
        Quit
      ${EndIf}
    ${EndIf}
    ${GetParent} "$6" $7
    ${If} $7 != ""
    ${AndIf} $7 != $6
      StrCpy $6 $7
      Goto checkParent
    ${EndIf}
FunctionEnd

Function ${PREFIX}CheckInstallLocation
  ; NSIS GetFullPathName requires an existing path; new install roots do not exist.
  System::Call 'kernel32::GetFullPathNameW(w "$INSTDIR", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i.r2'
  ${GetParent} "$0" $1
  ${If} $2 == 0
  ${OrIf} $2 >= ${NSIS_MAX_STRLEN}
  ${OrIf} $1 == ""
  ${OrIf} $0 == $1
    MessageBox MB_OK|MB_ICONSTOP "Choose a dedicated LMU Quad View folder, not a drive root." /SD IDOK
    SetErrorLevel 2
    Quit
  ${EndIf}
  StrCpy $INSTDIR $0
FunctionEnd
!macroend
!include "FileFunc.nsh"
!insertmacro SafetyFunctions ""
!insertmacro SafetyFunctions "un."

Function WaitForLauncherExit
  ${GetParameters} $0
  ${GetOptions} "$0" "/WAITPID=" $1
  ${IfNot} ${Errors}
    ; Waiting is advisory: the complete process-name guard below still runs.
    System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i r1) p.r2'
    ${If} $2 != 0
      System::Call 'kernel32::WaitForSingleObject(p r2, i 15000) i.r3'
      System::Call 'kernel32::CloseHandle(p r2)'
    ${EndIf}
  ${EndIf}
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "LMU Quad View requires 64-bit Windows." /SD IDOK
    Quit
  ${EndIf}
  SetShellVarContext current
  SetRegView 64
  Call CheckInstallLocation
  Call WaitForLauncherExit
  Call CheckProcesses
FunctionEnd

Function un.CheckAutoStart
  ; Never invoke --disable-autoload automatically: it can remove another package's
  ; protected global registration. Setup owns that operation and its UAC consent.
  ReadRegStr $0 HKCU "Software\LMUQuadView\AutoStart" "PackageRoot"
  ReadINIStr $2 "$LOCALAPPDATA\LMUQuadView\autoload.ini" "AutoStart" "PackageRoot"
  GetFullPathName $1 "$INSTDIR"
  ${If} $2 != ""
    GetFullPathName $2 "$2"
    ${If} $2 == $1
      Goto ownAutoStart
    ${EndIf}
  ${EndIf}
  ${If} $0 != ""
    GetFullPathName $0 "$0"
    ${If} $0 == $1
      Goto ownAutoStart
    ${EndIf}
  ${EndIf}
  Return
  ownAutoStart:
    MessageBox MB_OK|MB_ICONSTOP "Automatic startup is still assigned to this installation.$\r$\n$\r$\nOpen LMU Quad View, use Setup to remove automatic startup, accept its Windows administrator prompt, then close the launcher and run uninstall again.$\r$\n$\r$\nYour files have been kept." /SD IDOK
    SetErrorLevel 2
    Quit
FunctionEnd

Function un.onInit
  SetShellVarContext current
  SetRegView 64
  Call un.CheckInstallLocation
  Call un.CheckProcesses
  Call un.CheckAutoStart
FunctionEnd

Section "LMU Quad View (required)" SecCore
  SectionIn RO
  Call CheckInstallLocation
  Call CheckProcesses
  SetOverwrite on
  !insertmacro InstallPayload
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\LMU Quad View"
  CreateShortcut "$SMPROGRAMS\LMU Quad View\LMU Quad View.lnk" "$INSTDIR\LMU-QuadView-Launcher.exe"
  CreateShortcut "$SMPROGRAMS\LMU Quad View\Uninstall LMU Quad View.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "LMU Quad View Mod"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "LMU Quad View contributors"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\LMU-QuadView-Launcher.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\LMU Quad View.lnk" "$INSTDIR\LMU-QuadView-Launcher.exe"
SectionEnd

Section "Uninstall"
  Call un.CheckInstallLocation
  Call un.CheckProcesses
  Call un.CheckAutoStart
  !insertmacro RemovePayload
  Delete "$INSTDIR\Uninstall.exe"
  ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
  GetFullPathName $0 "$0"
  GetFullPathName $1 "$INSTDIR"
  ${If} $0 == $1
    Delete "$SMPROGRAMS\LMU Quad View\LMU Quad View.lnk"
    Delete "$SMPROGRAMS\LMU Quad View\Uninstall LMU Quad View.lnk"
    RMDir "$SMPROGRAMS\LMU Quad View"
    Delete "$DESKTOP\LMU Quad View.lnk"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
  ${EndIf}
  ; No recursive removal: settings, profiles, logs, runtime and unknown files stay.
  RMDir "$INSTDIR"
SectionEnd
