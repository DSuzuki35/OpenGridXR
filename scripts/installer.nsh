!macro customInstall
  FileOpen $0 "$INSTDIR\opengrid-installed" w
  FileWrite $0 "NSIS"
  FileClose $0
!macroend

!macro customUnInstall
  Delete "$INSTDIR\opengrid-installed"
!macroend
