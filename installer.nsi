Unicode true
XPStyle on

!define SOURCE_DIR "${__FILEDIR__}"
!include "include\product.nsh"

Name "${PRODUCT_NAME}"
Caption "${PRODUCT_SETUP_CAPTION}"
OutFile "${SOURCE_DIR}\dist\${PRODUCT_OUTPUT_FILENAME}"
RequestExecutionLevel user
InstallDir "$EXEDIR"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
BrandingText " "

VIProductVersion "${PRODUCT_VERSION_QUAD}"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName" "${PRODUCT_COMPANY}"
VIAddVersionKey "FileDescription" "${PRODUCT_SETUP_CAPTION}"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "LegalCopyright" "${PRODUCT_COPYRIGHT}"

!include MUI2.nsh
!include LogicLib.nsh
!include nsDialogs.nsh
!include WinMessages.nsh
!include FileFunc.nsh
!include StrFunc.nsh
!include x64.nsh
!include "include\ui.nsh"
${Using:StrFunc} StrStr
${Using:StrFunc} StrRep
${Using:StrFunc} StrLoc

!define MUI_ABORTWARNING
!define MUI_ICON "${SOURCE_DIR}\assets\medieval.ico"
!define MUI_UNICON "${SOURCE_DIR}\assets\medieval.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${SOURCE_DIR}\assets\welcome-finish.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${SOURCE_DIR}\assets\welcome-finish.bmp"
!define MUI_FONT "Tahoma"
!define MUI_ABORTWARNING_TEXT "Are you sure you want to quit the Unofficial Medieval: Total War Collection Patch Setup?"
!define MUI_WELCOMEPAGE_TITLE "Install Unofficial Medieval Total War Collection Patch"
!define MUI_WELCOMEPAGE_TEXT "This installer patches your existing Medieval: Total War Collection folder."
!define MUI_FINISHPAGE_TITLE "Installation complete"
!define MUI_FINISHPAGE_TEXT "Selected options were applied to your game. Have fun!"

!define MUI_PAGE_CUSTOMFUNCTION_SHOW WelcomePageShow
!insertmacro MUI_PAGE_WELCOME
!ifdef MUI_PAGE_CUSTOMFUNCTION_SHOW
!undef MUI_PAGE_CUSTOMFUNCTION_SHOW
!endif
Page custom CompatibilityPageCreate CompatibilityPageLeave
!define MUI_PAGE_CUSTOMFUNCTION_SHOW InstallPageShow
!insertmacro MUI_PAGE_INSTFILES
!ifdef MUI_PAGE_CUSTOMFUNCTION_SHOW
!undef MUI_PAGE_CUSTOMFUNCTION_SHOW
!endif
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishPageShow
!define MUI_PAGE_CUSTOMFUNCTION_DESTROYED FinishPageDestroyed
!insertmacro MUI_PAGE_FINISH
!ifdef MUI_PAGE_CUSTOMFUNCTION_SHOW
!undef MUI_PAGE_CUSTOMFUNCTION_SHOW
!endif
!ifdef MUI_PAGE_CUSTOMFUNCTION_DESTROYED
!undef MUI_PAGE_CUSTOMFUNCTION_DESTROYED
!endif

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Var Dialog
Var TargetText
Var BrowseButton
Var CompatibilityCheck
Var PreviewBitmap
Var PreviewImage
Var PreviewTitle
Var PreviewText
Var PreviewWarningText
Var PatchPageFont
Var PatchPageTitleFont
Var PatchPageBodyFont
Var PageVisited
Var SavedTargetDir
Var SelectedComponent
Var EngineExitCode
Var EngineOutput
Var WasManaged
Var KofiButton
Var DiscordButton
Var KofiBadgeImage
Var DiscordBadgeImage
Var FinishBadgeHoverState

Function .onInit
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=compatibility.bmp "${SOURCE_DIR}\assets\compatibility.bmp"
    File /oname=discord-badge.bmp "${SOURCE_DIR}\assets\discord-badge.bmp"
    File /oname=discord-badge-hover.bmp "${SOURCE_DIR}\assets\discord-badge-hover.bmp"
    File /oname=kofi-badge.bmp "${SOURCE_DIR}\assets\kofi-badge.bmp"
    File /oname=kofi-badge-hover.bmp "${SOURCE_DIR}\assets\kofi-badge-hover.bmp"
    File /oname=install-engine.ps1 "${SOURCE_DIR}\src\install-engine.ps1"
    CreateDirectory "$PLUGINSDIR\payload"
    SetOutPath "$PLUGINSDIR\payload"
    File /oname=payload-manifest.json "${SOURCE_DIR}\vendor\runtime\payload-manifest.json"
    File /oname=D3D9.dll "${SOURCE_DIR}\vendor\runtime\D3D9.dll"
    File /oname=dgVoodoo_D3D9.dll "${SOURCE_DIR}\vendor\runtime\dgVoodoo_D3D9.dll"
    File /oname=ddraw.dll "${SOURCE_DIR}\vendor\runtime\ddraw.dll"
    File /oname=D3DImm.dll "${SOURCE_DIR}\vendor\runtime\D3DImm.dll"
    File /oname=dgVoodoo.conf "${SOURCE_DIR}\vendor\runtime\dgVoodoo.conf"
    SetOutPath "$PLUGINSDIR"

    StrCpy $PageVisited "0"
    StrCpy $SavedTargetDir ""
    StrCpy $SelectedComponent "1"
    Call DetectGamePath
FunctionEnd

Function DetectGamePath
    ${If} ${FileExists} "$INSTDIR\Medieval_TW.exe"
        Return
    ${EndIf}
    ${If} ${FileExists} "$EXEDIR\Medieval_TW.exe"
        StrCpy $INSTDIR "$EXEDIR"
        Return
    ${EndIf}

    SetRegView 32
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 345260" "InstallLocation"
    Call TryDetectedGamePath
    ${If} $9 == "1"
        Return
    ${EndIf}
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 345260" "InstallLocation"
    Call TryDetectedGamePath
    ${If} $9 == "1"
        Return
    ${EndIf}

    ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
    Call TrySteamRoot
    ${If} $9 == "1"
        Return
    ${EndIf}
    ReadRegStr $0 HKLM "Software\Valve\Steam" "InstallPath"
    Call TrySteamRoot
    ${If} $9 == "1"
        Return
    ${EndIf}

    ${If} ${RunningX64}
        SetRegView 64
        ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 345260" "InstallLocation"
        Call TryDetectedGamePath
        ${If} $9 == "1"
            SetRegView 32
            Return
        ${EndIf}
        ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 345260" "InstallLocation"
        Call TryDetectedGamePath
        ${If} $9 == "1"
            SetRegView 32
            Return
        ${EndIf}
        ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
        Call TrySteamRoot
        ${If} $9 == "1"
            SetRegView 32
            Return
        ${EndIf}
        ReadRegStr $0 HKLM "Software\Valve\Steam" "InstallPath"
        Call TrySteamRoot
        ${If} $9 == "1"
            SetRegView 32
            Return
        ${EndIf}
        SetRegView 32
    ${EndIf}

    SetRegView 32
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\1397939414_is1" "InstallLocation"
    Call TryDetectedGamePath
    ${If} $9 == "1"
        Return
    ${EndIf}
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\1397939414_is1" "InstallLocation"
    Call TryDetectedGamePath
    ${If} $9 == "1"
        Return
    ${EndIf}

    ${If} ${RunningX64}
        SetRegView 64
        ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\1397939414_is1" "InstallLocation"
        Call TryDetectedGamePath
        ${If} $9 == "1"
            SetRegView 32
            Return
        ${EndIf}
        ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\1397939414_is1" "InstallLocation"
        Call TryDetectedGamePath
        ${If} $9 == "1"
            SetRegView 32
            Return
        ${EndIf}
        SetRegView 32
    ${EndIf}

    StrCpy $INSTDIR "$EXEDIR"
FunctionEnd

Function TryDetectedGamePath
    StrCpy $9 "0"
    ${If} $0 == ""
        Return
    ${EndIf}
    ${StrRep} $0 "$0" "/" "\"
    ${If} ${FileExists} "$0\Medieval_TW.exe"
        StrCpy $INSTDIR "$0"
        StrCpy $9 "1"
    ${EndIf}
FunctionEnd

Function TrySteamRoot
    StrCpy $9 "0"
    ${If} $0 == ""
        Return
    ${EndIf}

    ${StrRep} $0 "$0" "/" "\"
    StrCpy $1 "$0\steamapps\common\Total War Medieval 1 Gold"
    ${If} ${FileExists} "$1\Medieval_TW.exe"
        StrCpy $INSTDIR "$1"
        StrCpy $9 "1"
        Return
    ${EndIf}

    StrCpy $1 "$0\steamapps\libraryfolders.vdf"
    ${IfNot} ${FileExists} "$1"
        Return
    ${EndIf}
    ClearErrors
    FileOpen $2 "$1" r
    ${If} ${Errors}
        Return
    ${EndIf}
    ${Do}
        ClearErrors
        FileRead $2 $3
        ${If} ${Errors}
            ${ExitDo}
        ${EndIf}
        StrCpy $4 "$3"
        Call TrySteamLibraryFolderLine
        ${If} $9 == "1"
            FileClose $2
            Return
        ${EndIf}
    ${Loop}
    FileClose $2
FunctionEnd

Function TrySteamLibraryFolderLine
    StrCpy $9 "0"
    ${StrStr} $5 "$4" "$\"path$\""
    ${If} $5 == ""
        Return
    ${EndIf}
    StrCpy $5 "$5" "" 6
    ${StrStr} $5 "$5" "$\""
    ${If} $5 == ""
        Return
    ${EndIf}
    StrCpy $5 "$5" "" 1
    ${StrLoc} $6 "$5" "$\"" ">"
    ${If} $6 == ""
        Return
    ${EndIf}
    StrCpy $5 "$5" $6
    ${StrRep} $5 "$5" "\\" "\"
    StrCpy $5 "$5\steamapps\common\Total War Medieval 1 Gold"
    ${If} ${FileExists} "$5\Medieval_TW.exe"
        StrCpy $INSTDIR "$5"
        StrCpy $9 "1"
    ${EndIf}
FunctionEnd

Function ExtractEngineResult
    Exch $R0
    SetOutPath "$PLUGINSDIR\payload"
    nsExec::ExecToStack /TIMEOUT=180000 '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\install-engine.ps1" -Operation "$R0" -Target "$INSTDIR" -PayloadDirectory "$PLUGINSDIR\payload" -InstallerVersion "${PRODUCT_VERSION}" -InstallerPath "$EXEPATH" -OutputMode Human'
    SetOutPath "$PLUGINSDIR"
    Pop $EngineExitCode
    Pop $EngineOutput
    Pop $R0
FunctionEnd

Function RestoreDefaultWizard
    LockWindow on
    System::Call 'user32::SetWindowPos(p$HWNDPARENT,p0,i0,i0,i503,i390,i0x16)'
    GetDlgItem $0 $HWNDPARENT 1034
    System::Call 'user32::SetWindowPos(p$0,p0,i0,i0,i498,i57,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1036
    System::Call 'user32::SetWindowPos(p$0,p0,i0,i57,i508,i2,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1037
    System::Call 'user32::SetWindowPos(p$0,p0,i15,i8,i420,i16,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1038
    System::Call 'user32::SetWindowPos(p$0,p0,i23,i26,i413,i26,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1039
    System::Call 'user32::SetWindowPos(p$0,p0,i453,i13,i32,i32,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1028
    System::Call 'user32::SetWindowPos(p$0,p0,i8,i305,i483,i13,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1256
    System::Call 'user32::SetWindowPos(p$0,p0,i8,i305,i483,i13,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1035
    System::Call 'user32::SetWindowPos(p$0,p0,i8,i313,i480,i2,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1045
    System::Call 'user32::SetWindowPos(p$0,p0,i0,i313,i508,i2,i0x14)'
    GetDlgItem $0 $HWNDPARENT 3
    System::Call 'user32::SetWindowPos(p$0,p0,i252,i326,i75,i23,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1
    System::Call 'user32::SetWindowPos(p$0,p0,i327,i326,i75,i23,i0x14)'
    GetDlgItem $0 $HWNDPARENT 2
    System::Call 'user32::SetWindowPos(p$0,p0,i413,i326,i75,i23,i0x14)'
    LockWindow off
FunctionEnd

Function ResizePatchWizard
    LockWindow on
    System::Call 'user32::SetWindowPos(p$HWNDPARENT,p0,i0,i0,i900,i660,i0x16)'
    System::Call 'user32::SetWindowPos(p$Dialog,p0,i0,i0,i848,i500,i0x16)'
    GetDlgItem $0 $HWNDPARENT 1034
    System::Call 'user32::SetWindowPos(p$0,p0,i0,i0,i894,i57,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1036
    System::Call 'user32::SetWindowPos(p$0,p0,i0,i57,i894,i2,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1037
    System::Call 'user32::SetWindowPos(p$0,p0,i15,i8,i790,i16,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1038
    System::Call 'user32::SetWindowPos(p$0,p0,i23,i26,i790,i26,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1039
    System::Call 'user32::SetWindowPos(p$0,p0,i844,i13,i32,i32,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1028
    System::Call 'user32::SetWindowPos(p$0,p0,i8,i572,i870,i13,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1256
    System::Call 'user32::SetWindowPos(p$0,p0,i8,i572,i870,i13,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1035
    System::Call 'user32::SetWindowPos(p$0,p0,i8,i585,i870,i2,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1045
    System::Call 'user32::SetWindowPos(p$0,p0,i0,i585,i894,i2,i0x14)'
    GetDlgItem $0 $HWNDPARENT 3
    System::Call 'user32::SetWindowPos(p$0,p0,i628,i593,i75,i23,i0x14)'
    GetDlgItem $0 $HWNDPARENT 1
    System::Call 'user32::SetWindowPos(p$0,p0,i708,i593,i75,i23,i0x14)'
    GetDlgItem $0 $HWNDPARENT 2
    System::Call 'user32::SetWindowPos(p$0,p0,i794,i593,i75,i23,i0x14)'
    LockWindow off
FunctionEnd

Function WelcomePageShow
    Call RestoreDefaultWizard
FunctionEnd

Function InstallPageShow
    Call RestoreDefaultWizard
FunctionEnd

Function CompatibilityPageCreate
    IfSilent compatibilitySilent compatibilityInteractive
compatibilitySilent:
    Return
compatibilityInteractive:
    ${If} $PageVisited == "0"
        StrCpy $SavedTargetDir "$INSTDIR"
        StrCpy $PageVisited "1"
    ${ElseIf} $SavedTargetDir != ""
        StrCpy $INSTDIR "$SavedTargetDir"
    ${EndIf}

    !insertmacro MUI_HEADER_TEXT "Select patches" "Recommended options are selected by default. Hover over an option for more information."
    nsDialogs::Create 1018
    Pop $Dialog
    Call ResizePatchWizard
    ${NSD_OnBack} CompatibilityPageBack

    CreateFont $PatchPageFont "Tahoma" "10" "400"
    CreateFont $PatchPageTitleFont "Tahoma" "12" "700"
    CreateFont $PatchPageBodyFont "Tahoma" "10" "400"

    ${NSD_CreateLabel} 0 0 100% 18 "Game folder"
    Pop $0
    !insertmacro SET_TAHOMA $0 $PatchPageFont
    ${NSD_CreateText} 0 24 688 26 "$INSTDIR"
    Pop $TargetText
    !insertmacro SET_TAHOMA $TargetText $PatchPageFont
    ${NSD_CreateButton} 700 23 110 28 "Browse..."
    Pop $BrowseButton
    !insertmacro SET_TAHOMA $BrowseButton $PatchPageFont
    ${NSD_OnClick} $BrowseButton BrowseTarget

    ${NSD_CreateGroupBox} 0 62 320 264 "Recommended"
    Pop $0
    !insertmacro SET_TAHOMA $0 $PatchPageFont
    ${NSD_CreateCheckbox} 12 94 295 24 "${PRODUCT_COMPONENT_NAME}"
    Pop $CompatibilityCheck
    !insertmacro SET_TAHOMA $CompatibilityCheck $PatchPageFont
    ${NSD_Check} $CompatibilityCheck
    ${NSD_CreateGroupBox} 340 62 506 430 "Preview"
    Pop $0
    !insertmacro SET_TAHOMA $0 $PatchPageFont
    ${NSD_CreateBitmap} 352 92 480 270 ""
    Pop $PreviewBitmap
    ${NSD_SetImage} $PreviewBitmap "$PLUGINSDIR\compatibility.bmp" $PreviewImage
    ${NSD_CreateLabel} 352 374 480 28 "Terrain Movement Fix"
    Pop $PreviewTitle
    !insertmacro SET_TAHOMA $PreviewTitle $PatchPageTitleFont
    ${NSD_CreateLabel} 352 410 480 56 "Installs dgVoodoo2 to fix click-to-move and drag-formation issues on modern Windows systems."
    Pop $PreviewText
    !insertmacro SET_TAHOMA $PreviewText $PatchPageBodyFont
    ${NSD_CreateLabel} 352 474 480 24 "Windows XP is not supported."
    Pop $PreviewWarningText
    !insertmacro SET_TAHOMA $PreviewWarningText $PatchPageBodyFont
    SetCtlColors $PreviewWarningText FF0000 F0F0F0

    nsDialogs::Show
FunctionEnd

Function BrowseTarget
    Pop $0
    ${NSD_GetText} $TargetText $1
    nsDialogs::SelectFolderDialog "Select the game folder" "$1"
    Pop $2
    ${If} $2 != "error"
        ${NSD_SetText} $TargetText "$2"
    ${EndIf}
FunctionEnd

Function CompatibilityPageBack
    ${NSD_GetText} $TargetText $SavedTargetDir
    Call RestoreDefaultWizard
FunctionEnd

Function CompatibilityPageLeave
    ${NSD_GetText} $TargetText $INSTDIR
    StrCpy $SavedTargetDir "$INSTDIR"
    ${NSD_GetState} $CompatibilityCheck $0
    ${If} $0 != ${BST_CHECKED}
        MessageBox MB_OK|MB_ICONEXCLAMATION "Select ${PRODUCT_COMPONENT_NAME} to continue."
        Abort
    ${EndIf}
    StrCpy $R0 "Inspect"
    Push $R0
    Call ExtractEngineResult
    ${If} $EngineExitCode != "0"
        MessageBox MB_OK|MB_ICONSTOP "The selected folder is not supported.$\r$\n$\r$\n$EngineOutput"
        Abort
    ${EndIf}
    Call RestoreDefaultWizard
FunctionEnd

Function FinishPageShow
    Call RestoreDefaultWizard
    System::Call 'user32::SetWindowPos(p$mui.FinishPage.Text,p0,i180,i92,i293,i76,i0x14)'
    StrCpy $FinishBadgeHoverState ""
    ${NSD_CreateBitmap} 120u 169u 92u 17u ""
    Pop $DiscordButton
    ${NSD_SetImage} $DiscordButton "$PLUGINSDIR\discord-badge.bmp" $DiscordBadgeImage
    ${NSD_OnClick} $DiscordButton OpenDiscordInvite
    ${NSD_CreateBitmap} 223u 169u 92u 17u ""
    Pop $KofiButton
    ${NSD_SetImage} $KofiButton "$PLUGINSDIR\kofi-badge.bmp" $KofiBadgeImage
    ${NSD_OnClick} $KofiButton OpenKofiPage
    ${NSD_CreateTimer} FinishBadgeHoverTimer 60
FunctionEnd

Function FinishPageDestroyed
    ${NSD_KillTimer} FinishBadgeHoverTimer
    ${If} $KofiBadgeImage != ""
        ${NSD_FreeImage} $KofiBadgeImage
        StrCpy $KofiBadgeImage ""
    ${EndIf}
    ${If} $DiscordBadgeImage != ""
        ${NSD_FreeImage} $DiscordBadgeImage
        StrCpy $DiscordBadgeImage ""
    ${EndIf}
FunctionEnd

Function FinishBadgeHoverTimer
    System::Call "*(i 0, i 0) p.r8"
    System::Call "user32::GetCursorPos(p r8)i.r9"
    ${If} $9 == 0
        System::Free $8
        Return
    ${EndIf}
    System::Call "*$8(i.r0, i.r1)"
    System::Free $8
    StrCpy $R0 ""
    !insertmacro CHECK_FINISH_BADGE_HOVER $DiscordButton "discord"
    !insertmacro CHECK_FINISH_BADGE_HOVER $KofiButton "kofi"
    Call SetFinishBadgeHover
FunctionEnd

Function SetFinishBadgeHover
    ${If} $FinishBadgeHoverState == $R0
        Return
    ${EndIf}
    StrCpy $FinishBadgeHoverState "$R0"
    ${If} $DiscordBadgeImage != ""
        ${NSD_FreeImage} $DiscordBadgeImage
    ${EndIf}
    ${If} $R0 == "discord"
        ${NSD_SetImage} $DiscordButton "$PLUGINSDIR\discord-badge-hover.bmp" $DiscordBadgeImage
    ${Else}
        ${NSD_SetImage} $DiscordButton "$PLUGINSDIR\discord-badge.bmp" $DiscordBadgeImage
    ${EndIf}
    ${If} $KofiBadgeImage != ""
        ${NSD_FreeImage} $KofiBadgeImage
    ${EndIf}
    ${If} $R0 == "kofi"
        ${NSD_SetImage} $KofiButton "$PLUGINSDIR\kofi-badge-hover.bmp" $KofiBadgeImage
    ${Else}
        ${NSD_SetImage} $KofiButton "$PLUGINSDIR\kofi-badge.bmp" $KofiBadgeImage
    ${EndIf}
FunctionEnd

Function OpenDiscordInvite
    Pop $0
    ExecShell "open" "${PRODUCT_DISCORD_URL}"
FunctionEnd

Function OpenKofiPage
    Pop $0
    ExecShell "open" "${PRODUCT_KOFI_URL}"
FunctionEnd

Section "${PRODUCT_COMPONENT_NAME}" MainSection
    SectionIn RO
    SetOutPath "$PLUGINSDIR"
    StrCpy $WasManaged "0"
    ${If} ${FileExists} "$INSTDIR\.unofficial-medieval-total-war-patch\install-manifest.json"
        StrCpy $WasManaged "1"
    ${EndIf}

    ClearErrors
    WriteUninstaller "$PLUGINSDIR\Uninstall.exe"
    ${If} ${Errors}
        DetailPrint "Could not prepare the restore/uninstall program."
        IfSilent 0 +3
        SetErrorLevel 2
        Quit
        MessageBox MB_OK|MB_ICONSTOP "Could not prepare the restore/uninstall program. No game files were changed."
        SetErrorLevel 2
        Quit
    ${EndIf}

    DetailPrint "Validating and applying the Terrain Movement Fix..."
    StrCpy $R0 "Install"
    Push $R0
    Call ExtractEngineResult
    DetailPrint "$EngineOutput"
    ${If} $EngineExitCode != "0"
        IfSilent 0 +3
        SetErrorLevel 2
        Quit
        MessageBox MB_OK|MB_ICONSTOP "Installation stopped safely.$\r$\n$\r$\n$EngineOutput"
        SetErrorLevel 2
        Quit
    ${EndIf}

    CreateDirectory "$INSTDIR\.unofficial-medieval-total-war-patch"
    ClearErrors
    CopyFiles /SILENT "$PLUGINSDIR\Uninstall.exe" "$INSTDIR\.unofficial-medieval-total-war-patch"
    ${If} ${Errors}
        Goto installerMetadataFailure
    ${EndIf}

    SetRegView 32
    ClearErrors
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "Publisher" "${PRODUCT_COMPANY}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "DisplayIcon" "$INSTDIR\.unofficial-medieval-total-war-patch\Uninstall.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "UninstallString" '"$INSTDIR\.unofficial-medieval-total-war-patch\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "QuietUninstallString" '"$INSTDIR\.unofficial-medieval-total-war-patch\Uninstall.exe" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "NoRepair" 0
    ${If} ${Errors}
        Goto installerMetadataFailure
    ${EndIf}
    Goto installerComplete

installerMetadataFailure:
    DetailPrint "Windows uninstall registration could not be completed."
    ${If} $WasManaged == "0"
        DetailPrint "Rolling back the fresh runtime installation..."
        StrCpy $R0 "Restore"
        Push $R0
        Call ExtractEngineResult
        DetailPrint "$EngineOutput"
    ${EndIf}
    IfSilent 0 +3
    SetErrorLevel 2
    Quit
        MessageBox MB_OK|MB_ICONSTOP "The selected patch files are safe, but Windows uninstall registration failed.$\r$\n$\r$\n$EngineOutput"
    SetErrorLevel 2
    Quit

installerComplete:
    DetailPrint "Terrain Movement Fix runtime, backups, receipt, and uninstaller verified."
SectionEnd

Function un.onInit
    SetRegView 32
    ReadRegStr $INSTDIR HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch" "InstallLocation"
    ${If} $INSTDIR == ""
        ${GetParent} "$EXEDIR" $INSTDIR
    ${EndIf}
FunctionEnd

Function un.RunRestoreEngine
    SetOutPath "$PLUGINSDIR\payload"
    nsExec::ExecToStack /TIMEOUT=180000 '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\install-engine.ps1" -Operation "Restore" -Target "$INSTDIR" -PayloadDirectory "$PLUGINSDIR\payload" -InstallerVersion "${PRODUCT_VERSION}" -InstallerPath "$EXEPATH" -OutputMode Human'
    SetOutPath "$PLUGINSDIR"
    Pop $EngineExitCode
    Pop $EngineOutput
FunctionEnd

Section Uninstall
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=install-engine.ps1 "${SOURCE_DIR}\src\install-engine.ps1"
    CreateDirectory "$PLUGINSDIR\payload"
    SetOutPath "$PLUGINSDIR\payload"
    File /oname=payload-manifest.json "${SOURCE_DIR}\vendor\runtime\payload-manifest.json"
    File /oname=D3D9.dll "${SOURCE_DIR}\vendor\runtime\D3D9.dll"
    File /oname=dgVoodoo_D3D9.dll "${SOURCE_DIR}\vendor\runtime\dgVoodoo_D3D9.dll"
    File /oname=ddraw.dll "${SOURCE_DIR}\vendor\runtime\ddraw.dll"
    File /oname=D3DImm.dll "${SOURCE_DIR}\vendor\runtime\D3DImm.dll"
    File /oname=dgVoodoo.conf "${SOURCE_DIR}\vendor\runtime\dgVoodoo.conf"
    SetOutPath "$PLUGINSDIR"

    DetailPrint "Restoring the complete pre-install state..."
    StrCpy $R0 "Restore"
    Call un.RunRestoreEngine
    DetailPrint "$EngineOutput"
    ${If} $EngineExitCode != "0"
        IfSilent 0 +3
        SetErrorLevel 2
        Quit
        MessageBox MB_OK|MB_ICONSTOP "Restore stopped safely because the installed state changed.$\r$\n$\r$\n$EngineOutput"
        SetErrorLevel 2
        Abort
    ${EndIf}

    SetRegView 32
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Unofficial Medieval Total War Collection Patch"
    RMDir "$INSTDIR\.unofficial-medieval-total-war-patch"
SectionEnd
