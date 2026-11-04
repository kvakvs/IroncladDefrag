; Installer of Ironclad Defrag (T140). Built by make-installer.bat, which passes:
;   VERSION  product version ("1.0.0", from project(VERSION))
;   COMMIT   short git hash with "-dirty" for a changed tree, or empty
;   BIN      folder with the Release IroncladDefrag.exe, icd-cli.exe and wxWidgets DLLs
;   CRT      the Visual C++ runtime folder (Microsoft.VC14x.CRT) whose DLLs are installed next to the programs
;   SRC      repository root (icon, CREDITS.md)
;   OUTFILE  the setup program to write
; Per-machine install into Program Files (the app always runs elevated anyway), Start menu shortcut, optional desktop
; shortcut, an entry in Installed apps, and an uninstaller that keeps the user's settings, logs and saved analyses
; unless asked to remove them.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"

!define PRODUCT "Ironclad Defrag"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\IroncladDefrag"
!if "${COMMIT}" == ""
    !define BUILD_TEXT "${VERSION}"
!else
    !define BUILD_TEXT "${VERSION} (${COMMIT})"
!endif

Name "${PRODUCT}"
Caption "${PRODUCT} ${BUILD_TEXT} Setup"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel admin
BrandingText "${PRODUCT} ${BUILD_TEXT}"

VIProductVersion "${VERSION}.0"
VIFileVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "Comments" "Build ${BUILD_TEXT}"
VIAddVersionKey "LegalCopyright" "Icon: Smashicons (flaticon.com)"

!define MUI_ICON "${SRC}\res\IroncladDefrag.ico"
!define MUI_UNICON "${SRC}\res\IroncladDefrag.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\IroncladDefrag.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Windows 10 or later, 64-bit (plan.md: Windows 10 22H2+, x64).
Function .onInit
    ${IfNot} ${RunningX64}
    ${OrIfNot} ${AtLeastWin10}
        MessageBox MB_ICONSTOP|MB_OK "${PRODUCT} needs 64-bit Windows 10 or later."
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Section "${PRODUCT}" SecApp
    SectionIn RO
    SetShellVarContext all
    SetOutPath "$INSTDIR"
    File "${BIN}\IroncladDefrag.exe"
    File "${BIN}\icd-cli.exe"
    File "${BIN}\wxbase334u_vc14x_x64.dll"
    File "${BIN}\wxmsw334u_core_vc14x_x64.dll"
    ; The Visual C++ runtime next to the programs (app-local), so no separate redistributable is needed.
    File "${CRT}\msvcp140.dll"
    File "${CRT}\msvcp140_1.dll"
    File "${CRT}\msvcp140_atomic_wait.dll"
    File "${CRT}\vcruntime140.dll"
    File "${CRT}\vcruntime140_1.dll"
    File "${SRC}\CREDITS.md"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    CreateShortcut "$SMPROGRAMS\${PRODUCT}.lnk" "$INSTDIR\IroncladDefrag.exe"

    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\IroncladDefrag.exe,0"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PRODUCT}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Comments" "Build ${BUILD_TEXT}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section /o "Desktop shortcut" SecDesktop
    SetShellVarContext all
    CreateShortcut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\IroncladDefrag.exe"
SectionEnd

Section "un.${PRODUCT}" UnSecApp
    SectionIn RO
    SetShellVarContext all
    ; Only the files the installer wrote, so nothing else in the folder is lost.
    Delete "$INSTDIR\IroncladDefrag.exe"
    Delete "$INSTDIR\icd-cli.exe"
    Delete "$INSTDIR\wxbase334u_vc14x_x64.dll"
    Delete "$INSTDIR\wxmsw334u_core_vc14x_x64.dll"
    Delete "$INSTDIR\msvcp140.dll"
    Delete "$INSTDIR\msvcp140_1.dll"
    Delete "$INSTDIR\msvcp140_atomic_wait.dll"
    Delete "$INSTDIR\vcruntime140.dll"
    Delete "$INSTDIR\vcruntime140_1.dll"
    Delete "$INSTDIR\CREDITS.md"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\${PRODUCT}.lnk"
    Delete "$DESKTOP\${PRODUCT}.lnk"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd

; Off by default: settings, logs (including the execution logs that let an interrupted run resume) and saved analyses
; stay unless the user asks.
Section /o "un.Settings, logs and saved analyses of this user" UnSecData
    SetShellVarContext current
    RMDir /r "$LOCALAPPDATA\IroncladDefrag"
SectionEnd
