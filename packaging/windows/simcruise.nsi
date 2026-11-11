; SimCruise Windows installer (NSIS 3). Built by scripts/build-windows.sh:
;   makensis -DVERSION=1.1.0 -DSTAGE=build/windows/stage -DOUTFILE=... packaging/windows/simcruise.nsi
; Installs per machine into Program Files, adds Start menu and optional desktop
; shortcuts for all users, an uninstaller and an "Apps & features" entry. The player's cache
; and preferences live in %APPDATA%\Godot\app_userdata\SimCruise and are kept on
; uninstall unless the player ticks the box to remove them.
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef VERSION
  !error "Pass -DVERSION=X.Y.Z"
!endif
!ifndef STAGE
  !error "Pass -DSTAGE=<folder with SimCruise.exe, atlas.exe and Credits>"
!endif
!ifndef OUTFILE
  !define OUTFILE "SimCruise-${VERSION}-windows-x64-setup.exe"
!endif

!define APP "SimCruise"
!define PUBLISHER "SimCruise contributors"
!define URL "https://github.com/simcruisegame/simcruise"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ManifestDPIAware true

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "MIT License; data and assets under their own licences (see Credits)"

!define MUI_ICON "simcruise.ico"
!define MUI_UNICON "simcruise.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Play SimCruise now"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchUnelevated
!define MUI_FINISHPAGE_LINK "Project page, guides and bug reports"
!define MUI_FINISHPAGE_LINK_LOCATION "${URL}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "SimCruise needs 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  ; A reinstall goes where the last install went (InstallDirRegKey cannot do
  ; this: it is read before .onInit, in the 32-bit registry view). /D= wins.
  ${If} $INSTDIR == "$PROGRAMFILES64\${APP}"
    ReadRegStr $0 HKLM "Software\${APP}" "InstallDir"
    ${If} $0 != ""
      StrCpy $INSTDIR $0
    ${EndIf}
  ${EndIf}
  ; Per-machine install: Start menu and desktop shortcuts for all users, so a
  ; standard user who elevated with an administrator account still gets them.
  SetShellVarContext all
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

; The installer runs elevated; starting the game from it directly would run the
; game as administrator with that account's %APPDATA%. Explorer starts it as the
; signed-in user instead.
Function LaunchUnelevated
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\SimCruise.exe"'
FunctionEnd

; Earlier builds of this installer put the shortcuts in the installing
; account's own Start menu and desktop; an upgrade or uninstall removes those.
!macro REMOVE_USER_SHORTCUTS
  SetShellVarContext current
  Delete "$DESKTOP\${APP}.lnk"
  RMDir /r "$SMPROGRAMS\${APP}"
  SetShellVarContext all
!macroend
Function RemoveUserShortcuts
  !insertmacro REMOVE_USER_SHORTCUTS
FunctionEnd
Function un.RemoveUserShortcuts
  !insertmacro REMOVE_USER_SHORTCUTS
FunctionEnd

Section "SimCruise (required)" SecGame
  SectionIn RO
  Call RemoveUserShortcuts
  SetOutPath "$INSTDIR"
  File "${STAGE}\SimCruise.exe"
  File "${STAGE}\atlas.exe"
  File "${STAGE}\LICENSE.txt"
  File "${STAGE}\README.txt"
  File "simcruise.ico"
  SetOutPath "$INSTDIR\Credits"
  File /r "${STAGE}\Credits\*.*"
  SetOutPath "$INSTDIR"

  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\SimCruise.exe" "" "$INSTDIR\simcruise.ico"
  CreateShortcut "$SMPROGRAMS\${APP}\Uninstall ${APP}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\${APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "${URL}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\simcruise.ico"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\SimCruise.exe" "" "$INSTDIR\simcruise.ico"
SectionEnd

Section "un.SimCruise" SecUnGame
  SectionIn RO
  Delete "$INSTDIR\SimCruise.exe"
  Delete "$INSTDIR\atlas.exe"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\simcruise.ico"
  RMDir /r "$INSTDIR\Credits"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\${APP}.lnk"
  RMDir /r "$SMPROGRAMS\${APP}"
  Call un.RemoveUserShortcuts
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "Software\${APP}"
SectionEnd

Section /o "un.Map cache and settings" SecUnData
  ; Godot keeps user data per user under %APPDATA% (here: the account the
  ; uninstaller runs as).
  SetShellVarContext current
  RMDir /r "$APPDATA\Godot\app_userdata\SimCruise"
  SetShellVarContext all
SectionEnd

LangString DESC_Game ${LANG_ENGLISH} "The game and its map helper."
LangString DESC_Desktop ${LANG_ENGLISH} "A shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecGame} $(DESC_Game)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
