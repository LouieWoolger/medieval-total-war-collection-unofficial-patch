Unicode true
XPStyle on

!define SOURCE_DIR "${__FILEDIR__}"
!include "include\product.nsh"
!ifndef OUTPUT_FILE
!define OUTPUT_FILE "${SOURCE_DIR}\dist\${PRODUCT_OUTPUT_FILENAME}"
!endif

!ifndef NATIVE_HELPER
!define NATIVE_HELPER "${SOURCE_DIR}\build\medieval_fix_patcher.exe"
!endif

Name "${PRODUCT_NAME}"
Caption "${PRODUCT_SETUP_CAPTION}"
OutFile "${OUTPUT_FILE}"
RequestExecutionLevel user
ManifestSupportedOS all
InstallDir "$EXEDIR"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
UninstallCaption "Remove Unofficial Medieval: Total War Collection Patch"
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
!include WinVer.nsh
!include "include\ui.nsh"
${Using:StrFunc} StrStr
${Using:StrFunc} StrRep
${Using:StrFunc} StrLoc

!macro CHECK_PREVIEW_HOVER HANDLE KEY
    System::Call "*(i 0, i 0, i 0, i 0) p.r2"
    System::Call "user32::GetWindowRect(p${HANDLE}, p r2)i.r3"
    ${If} $3 <> 0
        System::Call "*$2(i.r3, i.r4, i.r5, i.r6)"
        ${If} $0 >= $3
        ${AndIf} $0 <= $5
        ${AndIf} $1 >= $4
        ${AndIf} $1 <= $6
            System::Free $2
            StrCpy $R0 "${KEY}"
            Call SetPreview
            Return
        ${EndIf}
    ${EndIf}
    System::Free $2
!macroend

!define MUI_ABORTWARNING
!define MUI_CUSTOMFUNCTION_ABORT LogUserAbort
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

!define MUI_UNABORTWARNING
!define MUI_CUSTOMFUNCTION_UNABORT un.LogUserAbort
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Only patch files will be removed. Your game, saves and other files will be kept."
!define MUI_UNCONFIRMPAGE_TEXT_LOCATION "Game folder:"
!define MUI_PAGE_HEADER_SUBTEXT ""
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "Unofficial Patch Removed"
!define MUI_FINISHPAGE_TEXT "The patch was removed from:$\r$\n$INSTDIR$\r$\n$\r$\nYour game and personal files have been kept."
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Var Dialog
Var TargetText
Var BrowseButton
Var CompatibilityCheck
Var TerrainSelected
Var ScrollCheck
Var ScrollSelected
Var SpriteCheck
Var SpriteSelected
Var PreviewBitmap
Var PreviewImage
Var PreviewTitle
Var PreviewText
Var PreviewWarningText
Var CurrentPreviewKey
Var PatchPageFont
Var PatchPageTitleFont
Var PatchPageBodyFont
Var PageVisited
Var SavedTargetDir
Var SelectedComponent
Var ComponentPlatformSupported
Var EngineExitCode
Var EngineOutput
Var KofiButton
Var DiscordButton
Var KofiBadgeImage
Var DiscordBadgeImage
Var FinishBadgeHoverState

!include "installer-support.nsh"

Function .onInit
    StrCpy $OperationName "installation"
    Call InitializeDiagnostics
    ${GetOptions} $CommandOptions "/REQUIREOWNER=" $RequiredOwnerSid
    Call RequireDiagnostics
    StrCpy $InstallPhase "extraction-ui"
    Call LogPhase
    ClearErrors
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=compatibility.bmp "${SOURCE_DIR}\assets\compatibility.bmp"
    File /oname=campaign-scrolling.bmp "${SOURCE_DIR}\assets\campaign-scrolling.bmp"
    File /oname=sprite-clipping.bmp "${SOURCE_DIR}\assets\sprite-clipping.bmp"
    File /oname=discord-badge.bmp "${SOURCE_DIR}\assets\discord-badge.bmp"
    File /oname=discord-badge-hover.bmp "${SOURCE_DIR}\assets\discord-badge-hover.bmp"
    File /oname=kofi-badge.bmp "${SOURCE_DIR}\assets\kofi-badge.bmp"
    File /oname=kofi-badge-hover.bmp "${SOURCE_DIR}\assets\kofi-badge-hover.bmp"
    ${If} ${Errors}
        StrCpy $InstallError "error=ui_extraction_failed"
        Call FailInstallation
    ${EndIf}
    StrCpy $InstallPhase "extraction-engine"
    Call LogPhase
    ClearErrors
    !insertmacro MEDIEVAL_ENGINE_FILES
    ${If} ${Errors}
        StrCpy $InstallError "error=engine_extraction_failed"
        Call FailInstallation
    ${EndIf}

    StrCpy $TerrainSelected "1"
    StrCpy $ScrollSelected "1"
    StrCpy $SpriteSelected "1"
    StrCpy $0 ""
    ${GetOptions} $CommandOptions "/TERRAINFIX=" $0
    ${If} $0 == "0"
        StrCpy $TerrainSelected "0"
    ${ElseIf} $0 == "1"
        StrCpy $TerrainSelected "1"
    ${ElseIf} $0 != ""
        StrCpy $InstallError "error=invalid_terrainfix_option value=$0"
        Call FailInstallation
    ${EndIf}
    StrCpy $0 ""
    ${GetOptions} $CommandOptions "/SCROLLFIX=" $0
    ${If} $0 == "0"
        StrCpy $ScrollSelected "0"
    ${ElseIf} $0 == "1"
        StrCpy $ScrollSelected "1"
    ${ElseIf} $0 != ""
        StrCpy $InstallError "error=invalid_scrollfix_option value=$0"
        Call FailInstallation
    ${EndIf}
    StrCpy $0 ""
    ${GetOptions} $CommandOptions "/SPRITEFIX=" $0
    ${If} $0 == "0"
        StrCpy $SpriteSelected "0"
    ${ElseIf} $0 == "1"
        StrCpy $SpriteSelected "1"
    ${ElseIf} $0 != ""
        StrCpy $InstallError "error=invalid_spritefix_option value=$0"
        Call FailInstallation
    ${EndIf}
    Call SelectEnginePayload

    StrCpy $PageVisited "0"
    StrCpy $SavedTargetDir ""
    StrCpy $SelectedComponent "1"
    ; NSIS consumes /D= before .onInit and removes it from $CMDLINE. Inspect
    ; the native command line only to detect the override; never reparse its path.
    System::Call 'kernel32::GetCommandLineW() p.r0'
    System::Call 'shlwapi::StrStrW(p r0,w " /D=") p.r1'
    ${If} $1 == 0
        Call DetectGamePath
    ${EndIf}
FunctionEnd

Function RequireComponentPlatform
    ${If} $TerrainSelected != "1"
        Return
    ${EndIf}
    StrCpy $ComponentPlatformSupported "0"
    ${If} ${AtLeastWin7}
        StrCpy $ComponentPlatformSupported "1"
    ${EndIf}
    ; Restrictive capability probe on a marked disposable target. It can only
    ; refuse installation, never enable the component on an unsupported OS.
    ReadEnvStr $0 "MTW_ENABLE_LIFECYCLE_FAULTS"
    ${If} $0 == "1"
    ${AndIf} ${FileExists} "$INSTDIR\.umtwp-test-fixture"
        ReadEnvStr $0 "MTW_TEST_COMPONENT_OS"
        ${If} $0 == "pre-win7"
            StrCpy $ComponentPlatformSupported "0"
        ${EndIf}
    ${EndIf}
    ${If} $ComponentPlatformSupported != "1"
        StrCpy $SelectedComponent "0"
        StrCpy $InstallError "error=unsupported_component_os component=terrain-movement-fix minimum=Windows7 no_selection=1"
        StrCpy $LogLine "$InstallError"
        Call WriteDiagnostic
        MessageBox MB_ICONSTOP|MB_OK "Terrain Movement Fix requires Windows 7 or later. Deselect it to install either direct EXE fix independently.$\r$\n$\r$\nThe game was not changed.$\r$\n$\r$\nDiagnostics:$\r$\n$LogDirectory" /SD IDOK
        SetErrorLevel 2
        Quit
    ${EndIf}
FunctionEnd

Function SelectEnginePayload
    StrCpy $EnginePayloadDirectory "$NativeDirectory\payload-scroll-sprite-off"
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
    StrCpy $EngineOperation $R0
    Call InvokeEngine
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
    ${If} $TerrainSelected == "1"
        ${NSD_Check} $CompatibilityCheck
    ${EndIf}
    ${NSD_OnClick} $CompatibilityCheck PreviewTerrain
    ${NSD_CreateCheckbox} 12 124 295 24 "${PRODUCT_SCROLL_COMPONENT_NAME}"
    Pop $ScrollCheck
    !insertmacro SET_TAHOMA $ScrollCheck $PatchPageFont
    ${If} $ScrollSelected == "1"
        ${NSD_Check} $ScrollCheck
    ${EndIf}
    ${NSD_OnClick} $ScrollCheck PreviewScrolling
    ${NSD_CreateCheckbox} 12 154 295 24 "${PRODUCT_SPRITE_COMPONENT_NAME}"
    Pop $SpriteCheck
    !insertmacro SET_TAHOMA $SpriteCheck $PatchPageFont
    ${If} $SpriteSelected == "1"
        ${NSD_Check} $SpriteCheck
    ${EndIf}
    ${NSD_OnClick} $SpriteCheck PreviewSprite
    ${NSD_CreateGroupBox} 340 62 506 430 "Preview"
    Pop $0
    !insertmacro SET_TAHOMA $0 $PatchPageFont
    ${NSD_CreateBitmap} 352 92 480 270 ""
    Pop $PreviewBitmap
    ${NSD_CreateLabel} 352 374 480 28 ""
    Pop $PreviewTitle
    !insertmacro SET_TAHOMA $PreviewTitle $PatchPageTitleFont
    ${NSD_CreateLabel} 352 410 480 56 ""
    Pop $PreviewText
    !insertmacro SET_TAHOMA $PreviewText $PatchPageBodyFont
    ${NSD_CreateLabel} 352 474 480 24 ""
    Pop $PreviewWarningText
    !insertmacro SET_TAHOMA $PreviewWarningText $PatchPageBodyFont
    SetCtlColors $PreviewWarningText FF0000 F0F0F0
    ShowWindow $PreviewWarningText ${SW_HIDE}

    StrCpy $PreviewImage ""
    StrCpy $CurrentPreviewKey ""
    StrCpy $R0 "terrain"
    Call SetPreview
    ${NSD_CreateTimer} PreviewHoverTimer 120
    nsDialogs::Show
    ${NSD_KillTimer} PreviewHoverTimer
    ${If} $PreviewImage != ""
        System::Call "gdi32::DeleteObject(p$PreviewImage)"
        StrCpy $PreviewImage ""
    ${EndIf}
    System::Call "gdi32::DeleteObject(p$PatchPageFont)"
    System::Call "gdi32::DeleteObject(p$PatchPageTitleFont)"
    System::Call "gdi32::DeleteObject(p$PatchPageBodyFont)"
FunctionEnd

Function PreviewTerrain
    Pop $0
    StrCpy $R0 "terrain"
    Call SetPreview
FunctionEnd

Function PreviewScrolling
    Pop $0
    StrCpy $R0 "scrolling"
    Call SetPreview
FunctionEnd

Function PreviewSprite
    Pop $0
    StrCpy $R0 "sprite"
    Call SetPreview
FunctionEnd

Function PreviewHoverTimer
    Call PreviewFromCursor
FunctionEnd

Function PreviewFromCursor
    System::Call "*(i 0, i 0) p.r8"
    System::Call "user32::GetCursorPos(p r8)i.r9"
    ${If} $9 == 0
        System::Free $8
        Return
    ${EndIf}
    System::Call "*$8(i.r0, i.r1)"
    System::Free $8

    System::Call "user32::WindowFromPoint(ir0, ir1)p.r7"
    ${If} $7 == 0
        Return
    ${EndIf}
    ${If} $7 != $HWNDPARENT
        System::Call "user32::IsChild(p$HWNDPARENT, pr7)i.r9"
        ${If} $9 == 0
            Return
        ${EndIf}
    ${EndIf}

    !insertmacro CHECK_PREVIEW_HOVER $CompatibilityCheck "terrain"
    !insertmacro CHECK_PREVIEW_HOVER $ScrollCheck "scrolling"
    !insertmacro CHECK_PREVIEW_HOVER $SpriteCheck "sprite"
FunctionEnd

Function SetPreview
    ${If} $CurrentPreviewKey == $R0
        Return
    ${EndIf}
    StrCpy $CurrentPreviewKey "$R0"

    ${If} $PreviewImage != ""
        System::Call "gdi32::DeleteObject(p$PreviewImage)"
        StrCpy $PreviewImage ""
    ${EndIf}

    ${If} $R0 == "sprite"
        ${NSD_SetText} $PreviewTitle "Pre-battle Screen Crash Fix"
        ${NSD_SetText} $PreviewText "Fixes a crash that can occur on the pre-battle screen during the campaign."
        ${NSD_SetText} $PreviewWarningText ""
        ShowWindow $PreviewWarningText ${SW_HIDE}
        StrCpy $1 "$PLUGINSDIR\sprite-clipping.bmp"
    ${ElseIf} $R0 == "scrolling"
        ${NSD_SetText} $PreviewTitle "Campaign Map Scroll Fix"
        ${NSD_SetText} $PreviewText "Fixes campaign-map scrolling speed at high frame rates."
        ${NSD_SetText} $PreviewWarningText ""
        ShowWindow $PreviewWarningText ${SW_HIDE}
        StrCpy $1 "$PLUGINSDIR\campaign-scrolling.bmp"
    ${Else}
        ${NSD_SetText} $PreviewTitle "Terrain Movement Fix"
        ${NSD_SetText} $PreviewText "Installs dgVoodoo2 to fix click-to-move and drag-formation issues on modern Windows systems."
        ${NSD_SetText} $PreviewWarningText "Windows XP is not supported."
        ShowWindow $PreviewWarningText ${SW_SHOW}
        StrCpy $1 "$PLUGINSDIR\compatibility.bmp"
    ${EndIf}

    ${NSD_SetImage} $PreviewBitmap "$1" $PreviewImage
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
    ${NSD_GetState} $CompatibilityCheck $0
    StrCpy $TerrainSelected "0"
    ${If} $0 == ${BST_CHECKED}
        StrCpy $TerrainSelected "1"
    ${EndIf}
    ${NSD_GetState} $ScrollCheck $0
    StrCpy $ScrollSelected "0"
    ${If} $0 == ${BST_CHECKED}
        StrCpy $ScrollSelected "1"
    ${EndIf}
    ${NSD_GetState} $SpriteCheck $0
    StrCpy $SpriteSelected "0"
    ${If} $0 == ${BST_CHECKED}
        StrCpy $SpriteSelected "1"
    ${EndIf}
    Call RestoreDefaultWizard
FunctionEnd

Function CompatibilityPageLeave
    ${NSD_GetText} $TargetText $INSTDIR
    StrCpy $SavedTargetDir "$INSTDIR"
    ${NSD_GetState} $CompatibilityCheck $0
    StrCpy $TerrainSelected "0"
    ${If} $0 == ${BST_CHECKED}
        StrCpy $TerrainSelected "1"
    ${EndIf}
    ${NSD_GetState} $ScrollCheck $0
    StrCpy $ScrollSelected "0"
    ${If} $0 == ${BST_CHECKED}
        StrCpy $ScrollSelected "1"
    ${EndIf}
    ${NSD_GetState} $SpriteCheck $0
    StrCpy $SpriteSelected "0"
    ${If} $0 == ${BST_CHECKED}
        StrCpy $SpriteSelected "1"
    ${EndIf}
    ${If} $TerrainSelected != "1"
    ${AndIf} $ScrollSelected != "1"
    ${AndIf} $SpriteSelected != "1"
        MessageBox MB_OK|MB_ICONEXCLAMATION "Select at least one fix to continue."
        Abort
    ${EndIf}
    Call RequireComponentPlatform
    Call SelectEnginePayload
    StrCpy $R0 "Inspect"
    StrCpy $EngineUninstaller ""
    Push $R0
    Call ExtractEngineResult
    ${If} $EngineExitCode != "0"
        MessageBox MB_OK|MB_ICONSTOP "The selected folder could not be used.$\r$\n$\r$\n$EngineOutput$\r$\n$InstallError$\r$\n$\r$\nDiagnostics:$\r$\n$LogDirectory"
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
    Call RequireComponentPlatform
    StrCpy $InstallPhase "prepare-uninstaller"
    Call LogPhase
    ClearErrors
    ; The engine commits this root-level executable in the same transaction as
    ; payload files, private originals, receipt and Windows registration.
    WriteUninstaller "$PLUGINSDIR\${PRODUCT_UNINSTALLER_FILENAME}"
    ${If} ${Errors}
        StrCpy $InstallError "error=uninstaller_generation_failed"
        Call FailInstallation
    ${EndIf}
    StrCpy $EngineUninstaller "$NativeDirectory\${PRODUCT_UNINSTALLER_FILENAME}"
    DetailPrint "Validating and applying the selected fixes..."
    StrCpy $LogLine "campaign_scroll_fix_selected=$ScrollSelected"
    Call WriteDiagnostic
    StrCpy $LogLine "sprite_clipping_fix_selected=$SpriteSelected"
    Call WriteDiagnostic
    DetailPrint "Diagnostics: $LogDirectory"
    StrCpy $R0 "Install"
    Push $R0
    Call ExtractEngineResult
    ${If} $EngineExitCode == "740"
        Call InstallWithAdministratorPermission
    ${EndIf}
    ${If} $EngineExitCode != "0"
    ${OrIf} $InstallError != ""
        Call FailInstallation
    ${EndIf}
    StrCpy $InstallPhase "complete"
    Call LogPhase
    StrCpy $LogLine "result=success operation=install"
    Call WriteDiagnostic
    Call RequireDiagnostics
    DetailPrint "Selected fixes, recovery state and uninstaller verified."
    SetErrorLevel 0
SectionEnd

!include "installer-uninstall.nsh"
