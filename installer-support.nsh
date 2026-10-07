; Diagnostics and CreateProcess/Wait runner adapted from Shogun v1.3.2,
; commit 013d5ed, installer-support.nsh. File/registry transactions are owned
; exclusively by Medieval's bundled native lifecycle helper.
Var LogDirectory
Var LogBase
Var InstallerLog
Var HelperLog
Var ConsoleLog
Var ConsoleHandle
Var NullInputHandle
Var ConsoleFlushResult
Var ConsoleFlushError
Var LogHandle
Var LogLine
Var InstallPhase
Var InstallError
Var CommandOptions
Var ChildStatus
Var DiagnosticWriteFailed
Var DiagnosticNativeError
Var RequestedLogBase
Var LogFlushResult
Var LogFlushNativeError
Var OperationName
Var Utf8Buffer
Var Utf8Pointer
Var Utf8Size
Var Utf8End
Var Utf8Overflow
Var PatcherOutput
Var DisplayLog
Var NativeDirectory
Var EnginePayloadDirectory
Var EngineOperation
Var EngineInstaller
Var EngineUninstaller
Var RequestHandle
Var RequestWriteFailed
Var RemovalRestored
Var RequiredOwnerSid
Var ElevationArguments
Var ElevatedProcess
Var ElevatedExit

!macro MEDIEVAL_ENGINE_FILES
    InitPluginsDir
    ; NSIS may use an 8.3 TEMP alias. Pass the existing long path to the guarded
    ; helper, while retaining declared PLUGINSDIR archive destinations.
    System::Call 'kernel32::GetLongPathNameW(w "$PLUGINSDIR",w.r0,i ${NSIS_MAX_STRLEN}) i.r1'
    ${If} $1 == 0
    ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
        SetErrors
    ${Else}
        StrCpy $NativeDirectory "$0"
    SetOutPath "$PLUGINSDIR"
    File /oname=medieval_fix_patcher.exe "${NATIVE_HELPER}"
    File /oname=LICENSE.txt "${SOURCE_DIR}\LICENSE"
    File /oname=MinGW-w64-runtime.txt "${SOURCE_DIR}\licenses\MinGW-w64-runtime.txt"
    SetOutPath "$PLUGINSDIR\payload-scroll-sprite-off"
    File /oname=payload-manifest.json "${SOURCE_DIR}\vendor\runtime\payload-manifest-scroll-sprite-off.json"
    File /oname=D3D9.dll "${SOURCE_DIR}\vendor\runtime\D3D9-scroll-sprite-off.dll"
    File /oname=dgVoodoo_D3D9.dll "${SOURCE_DIR}\vendor\runtime\dgVoodoo_D3D9.dll"
    File /oname=ddraw.dll "${SOURCE_DIR}\vendor\runtime\ddraw.dll"
    File /oname=D3DImm.dll "${SOURCE_DIR}\vendor\runtime\D3DImm.dll"
    File /oname=dgVoodoo.conf "${SOURCE_DIR}\vendor\runtime\dgVoodoo.conf"
    ; This is the sole newly installable Terrain runtime. Both EXE fixes are
    ; disabled in its proxy, even when selected in the installer.
    ${EndIf}
!macroend
; Shared installer/uninstaller diagnostics and native process runner.
!macro MEDIEVAL_DIAGNOSTICS PREFIX
Function ${PREFIX}WriteDiagnostic
    ${If} $LogHandle != ""
        ClearErrors
        FileWriteUTF16LE $LogHandle "$LogLine$\r$\n"
        ${If} ${Errors}
            StrCpy $DiagnosticWriteFailed "1"
        ${EndIf}
        ; Use named variables; RunHelper keeps its process handle in $5.
        System::Call 'kernel32::FlushFileBuffers(p $LogHandle) i.s ?e'
        Pop $LogFlushNativeError
        Pop $LogFlushResult
        ${If} $LogFlushResult == 0
            StrCpy $DiagnosticWriteFailed "1"
            StrCpy $DiagnosticNativeError "$LogFlushNativeError"
        ${EndIf}
    ${EndIf}
FunctionEnd

Function ${PREFIX}LogPhase
    StrCpy $LogLine "phase=$InstallPhase"
    Call ${PREFIX}WriteDiagnostic
FunctionEnd

Function ${PREFIX}TryLogDirectory
    StrCpy $LogHandle ""
    ClearErrors
    CreateDirectory "$LogBase"
    GetTempFileName $LogDirectory "$LogBase"
    ${If} ${Errors}
        Return
    ${EndIf}
    ; GetTempFileName reserves a unique name. Only remove the file we just made.
    Delete "$LogDirectory"
    CreateDirectory "$LogDirectory"
    System::Call 'kernel32::GetLongPathNameW(w "$LogDirectory",w.r0,i ${NSIS_MAX_STRLEN}) i.r1'
    ${If} $1 == 0
    ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
        Return
    ${EndIf}
    StrCpy $LogDirectory "$0"
    StrCpy $InstallerLog "$LogDirectory\installer.log"
    StrCpy $HelperLog "$LogDirectory\helper.log"
    StrCpy $ConsoleLog "$LogDirectory\helper-console.log"
    ClearErrors
    FileOpen $LogHandle "$InstallerLog" w
    ${If} ${Errors}
        StrCpy $LogHandle ""
        Return
    ${EndIf}
    FileWriteUTF16LE /BOM $LogHandle "Unofficial Medieval Patch $OperationName diagnostics$\r$\n"
    ${If} ${Errors}
        FileClose $LogHandle
        StrCpy $LogHandle ""
    ${EndIf}
FunctionEnd

Function ${PREFIX}InitializeDiagnostics
    ${GetParameters} $CommandOptions
    StrCpy $LogBase ""
    ${GetOptions} $CommandOptions "/LOGDIR=" $LogBase
    StrCpy $RequestedLogBase "$LogBase"
    ${If} $LogBase != ""
        Call ${PREFIX}TryLogDirectory
    ${EndIf}
    ${If} $LogHandle == ""
        StrCpy $LogBase "$LOCALAPPDATA\Unofficial Medieval Patch\Logs"
        Call ${PREFIX}TryLogDirectory
    ${EndIf}
    ${If} $LogHandle == ""
        StrCpy $LogBase "$TEMP\Unofficial Medieval Patch Logs"
        Call ${PREFIX}TryLogDirectory
    ${EndIf}
    ${If} $LogHandle == ""
        StrCpy $LogBase "$EXEDIR\Unofficial Medieval Patch Logs"
        Call ${PREFIX}TryLogDirectory
    ${EndIf}
    ${If} $LogHandle == ""
        MessageBox MB_ICONSTOP|MB_OK "No writable diagnostic location is available. The game was not changed. Run this installer with /LOGDIR= followed by a writable folder." /SD IDOK
        SetErrorLevel 2
        Quit
    ${EndIf}
    StrCpy $LogLine "version=${PRODUCT_VERSION}"
    Call ${PREFIX}WriteDiagnostic
    StrCpy $LogLine "installer=$EXEPATH"
    Call ${PREFIX}WriteDiagnostic
    StrCpy $LogLine "helper_log=$HelperLog"
    Call ${PREFIX}WriteDiagnostic
    StrCpy $LogLine "helper_console_log=$ConsoleLog"
    Call ${PREFIX}WriteDiagnostic
    StrCpy $LogLine "requested_log_directory=$RequestedLogBase"
    Call ${PREFIX}WriteDiagnostic
    System::Call 'kernel32::GetCurrentProcessId() i.r0'
    StrCpy $LogLine "installer_pid=$0"
    Call ${PREFIX}WriteDiagnostic
FunctionEnd

; Native diagnostics are UTF-8, regardless of the user's Windows ANSI codepage.
; FileRead would corrupt non-ASCII paths. Read raw bytes up to a whole line,
; then use the XP-compatible decoder. Oversized lines remain intact on disk.
Function ${PREFIX}ReadHelperLine
    StrCpy $PatcherOutput ""
    StrCpy $Utf8Size 0
    StrCpy $Utf8End 0
    StrCpy $Utf8Overflow 0
    System::Alloc 4096
    Pop $Utf8Buffer
    ${If} $Utf8Buffer == 0
        SetErrors
        Return
    ${EndIf}
    StrCpy $Utf8Pointer $Utf8Buffer
    ${Do}
        ClearErrors
        FileReadByte $2 $3
        ${If} ${Errors}
            StrCpy $Utf8End 1
            ${ExitDo}
        ${EndIf}
        ${If} $3 == 10
            ${ExitDo}
        ${EndIf}
        ${If} $3 != 13
            ${If} $Utf8Size < 4095
                System::Call '*$Utf8Pointer(&i1 r3)'
                IntOp $Utf8Pointer $Utf8Pointer + 1
                IntOp $Utf8Size $Utf8Size + 1
            ${Else}
                StrCpy $Utf8Overflow 1
            ${EndIf}
        ${EndIf}
    ${Loop}
    System::Call '*$Utf8Pointer(&i1 0)'
    ${If} $Utf8Overflow == 0
        System::Call 'kernel32::MultiByteToWideChar(i 65001,i 0,p$Utf8Buffer,i -1,w.r0,i ${NSIS_MAX_STRLEN}) i.r1'
        ${If} $1 != 0
            StrCpy $PatcherOutput $0
        ${Else}
            StrCpy $PatcherOutput "[Diagnostic line too long for this window; see the complete helper log.]"
        ${EndIf}
    ${Else}
        StrCpy $PatcherOutput "[Diagnostic line too long for this window; see the complete helper log.]"
    ${EndIf}
    System::Free $Utf8Buffer
    ${If} $Utf8Size == 0
    ${AndIf} $Utf8End == 1
        SetErrors
    ${Else}
        ClearErrors
    ${EndIf}
FunctionEnd

Function ${PREFIX}ShowHelperDetails
    StrCpy $EngineOutput "See the complete diagnostic logs in $LogDirectory."
    ; Both streams are retained without a fixed-size stdout capture buffer.
    ; Individual oversized display lines remain complete on disk.
    StrCpy $DisplayLog "$HelperLog"
    Call ${PREFIX}DisplayLogFile
    StrCpy $DisplayLog "$ConsoleLog"
    Call ${PREFIX}DisplayLogFile
FunctionEnd

Function ${PREFIX}DisplayLogFile
    ClearErrors
    FileOpen $2 "$DisplayLog" r
    ${If} ${Errors}
        Return
    ${EndIf}
    ${Do}
        ClearErrors
        Call ${PREFIX}ReadHelperLine
        ${If} ${Errors}
            ${ExitDo}
        ${EndIf}
        DetailPrint "$PatcherOutput"
        ${If} $PatcherOutput != ""
            StrCpy $EngineOutput "$PatcherOutput"
        ${EndIf}
    ${Loop}
    FileClose $2
FunctionEnd
Function ${PREFIX}RunHelper
    ; NSIS is x86: STARTUPINFOW is 68 bytes and PROCESS_INFORMATION is 16.
    ; Capture CreateProcess's own native error at the API boundary. ExecWait's
    ; generic error flag cannot reliably retain it for a later GetLastError.
    StrCpy $ChildStatus "not-started"
    StrCpy $0 '"$NativeDirectory\medieval_fix_patcher.exe" --request "$NativeDirectory\request.ini" --output Human'
    ; Inheritable console/NUL handles capture startup diagnostics. This
    ; captures errors before the helper can open its own diagnostic stream.
    System::Call '*(i 12,p 0,i 1) p.r1'
    System::Call 'kernel32::CreateFileW(w "$ConsoleLog",i 0x40000000,i 1,p r1,i 2,i 0x80,p 0) p.s ?e'
    Pop $4
    Pop $ConsoleHandle
    ${If} $ConsoleHandle == -1
        System::Free $1
        StrCpy $InstallError "error=console_log_open_failed native_error=$4"
        Return
    ${EndIf}
    System::Call 'kernel32::CreateFileW(w "NUL",i 0x80000000,i 3,p r1,i 3,i 0x80,p 0) p.s ?e'
    Pop $4
    Pop $NullInputHandle
    System::Free $1
    ${If} $NullInputHandle == -1
        System::Call 'kernel32::CloseHandle(p $ConsoleHandle)'
        StrCpy $InstallError "error=child_input_open_failed native_error=$4"
        Return
    ${EndIf}
    ; dwFlags=STARTF_USESTDHANDLES; the last three fields are stdin/out/err.
    System::Call '*(i 68,p 0,p 0,p 0,i 0,i 0,i 0,i 0,i 0,i 0,i 0,i 0x100,i 0,p 0,p $NullInputHandle,p $ConsoleHandle,p $ConsoleHandle) p.r1'
    System::Call '*(p 0,p 0,i 0,i 0) p.r2'
    System::Call 'kernel32::CreateProcessW(w "$NativeDirectory\medieval_fix_patcher.exe",w r0,p 0,p 0,i 1,i 0x08000000,p 0,w "$EnginePayloadDirectory",p r1,p r2) i.r3 ?e'
    Pop $4
    System::Call 'kernel32::CloseHandle(p $NullInputHandle)'
    ${If} $3 == 0
        System::Call 'kernel32::CloseHandle(p $ConsoleHandle)'
        System::Free $1
        System::Free $2
        StrCpy $InstallError "error=helper_start_failed native_error=$4"
        Return
    ${EndIf}
    System::Call '*$2(p.r5,p.r6,i.r7,i.r8)'
    System::Free $1
    System::Free $2
    System::Call 'kernel32::CloseHandle(p r6)'
    StrCpy $LogLine "child_pid=$7"
    Call ${PREFIX}WriteDiagnostic
    ${Do}
        System::Call 'kernel32::WaitForSingleObject(p r5,i 100) i.r3'
        ${If} $3 == 0
            ${ExitDo}
        ${EndIf}
        ${If} $3 != 258
            StrCpy $InstallError "error=helper_wait_failed wait_status=$3"
            ; Keep handle open until process completes; do not pretend a
            ; running transaction failed or delete its extracted executable.
            System::Call 'kernel32::WaitForSingleObject(p r5,i -1)'
            ${ExitDo}
        ${EndIf}
        Sleep 10
    ${Loop}
    System::Call 'kernel32::GetExitCodeProcess(p r5,*i.r0) i.r3 ?e'
    Pop $4
    System::Call 'kernel32::CloseHandle(p r5)'
    System::Call 'kernel32::FlushFileBuffers(p $ConsoleHandle) i.s ?e'
    Pop $ConsoleFlushError
    Pop $ConsoleFlushResult
    System::Call 'kernel32::CloseHandle(p $ConsoleHandle)'
    ${If} $3 == 0
        StrCpy $InstallError "error=helper_exit_status_failed native_error=$4"
        Return
    ${EndIf}
    StrCpy $ChildStatus "$0"
    ${If} $ConsoleFlushResult == 0
        StrCpy $InstallError "error=console_log_flush_failed native_error=$ConsoleFlushError child_exit=$ChildStatus"
    ${EndIf}
FunctionEnd
!macroend

!macro MEDIEVAL_ENGINE_REQUEST PREFIX
Function ${PREFIX}WriteEngineRequest
    StrCpy $RequestWriteFailed "0"
    ; The self-copied NSIS remover can have a short TEMP alias in EXEPATH.
    ; Normalize this supplied input before the helper applies its path guard.
    System::Call 'kernel32::GetLongPathNameW(w "$EXEPATH",w.r0,i ${NSIS_MAX_STRLEN}) i.r1'
    ${If} $1 == 0
    ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
        StrCpy $InstallError "error=installer_path_resolution_failed"
        Return
    ${EndIf}
    StrCpy $EngineInstaller "$0"
    ClearErrors
    FileOpen $RequestHandle "$PLUGINSDIR\request.ini" w
    ${If} ${Errors}
        StrCpy $InstallError "error=engine_request_open_failed"
        Return
    ${EndIf}
    ; One UTF-16LE field per line. The engine accepts only these recognized keys;
    ; values never enter a shell or the command-line argument parser.
    FileWriteUTF16LE /BOM $RequestHandle "operation=$EngineOperation$\r$\n"
    FileWriteUTF16LE $RequestHandle "target=$INSTDIR$\r$\n"
    FileWriteUTF16LE $RequestHandle "payload=$EnginePayloadDirectory$\r$\n"
    FileWriteUTF16LE $RequestHandle "version=${PRODUCT_VERSION}$\r$\n"
    FileWriteUTF16LE $RequestHandle "installer=$EngineInstaller$\r$\n"
    FileWriteUTF16LE $RequestHandle "uninstaller=$EngineUninstaller$\r$\n"
    FileWriteUTF16LE $RequestHandle "log=$HelperLog$\r$\n"
    FileWriteUTF16LE $RequestHandle "require_owner=$RequiredOwnerSid$\r$\n"
    FileWriteUTF16LE $RequestHandle "terrain_fix=$TerrainSelected$\r$\n"
    FileWriteUTF16LE $RequestHandle "scroll_fix=$ScrollSelected$\r$\n"
    FileWriteUTF16LE $RequestHandle "sprite_fix=$SpriteSelected$\r$\n"
    ${If} ${Errors}
        StrCpy $RequestWriteFailed "1"
    ${EndIf}
    System::Call 'kernel32::FlushFileBuffers(p $RequestHandle) i.s ?e'
    Pop $LogFlushNativeError
    Pop $LogFlushResult
    FileClose $RequestHandle
    ${If} $RequestWriteFailed == "1"
    ${OrIf} $LogFlushResult == 0
        StrCpy $InstallError "error=engine_request_write_failed native_error=$LogFlushNativeError"
    ${EndIf}
FunctionEnd

Function ${PREFIX}InvokeEngine
    SetOutPath "$EnginePayloadDirectory"
    StrCpy $EngineExitCode "2"
    StrCpy $ChildStatus "not-started"
    StrCpy $InstallError ""
    StrCpy $EngineOutput ""
    StrCpy $InstallPhase "$EngineOperation"
    Call ${PREFIX}LogPhase
    StrCpy $LogLine "operation=$EngineOperation target=$INSTDIR"
    Call ${PREFIX}WriteDiagnostic
    ${If} $DiagnosticWriteFailed == "1"
        StrCpy $InstallError "error=diagnostic_write_failed native_error=$DiagnosticNativeError"
        Return
    ${EndIf}
    Call ${PREFIX}WriteEngineRequest
    ${If} $InstallError != ""
        Return
    ${EndIf}
    Call ${PREFIX}RunHelper
    StrCpy $LogLine "child_exit=$ChildStatus"
    Call ${PREFIX}WriteDiagnostic
    Call ${PREFIX}ShowHelperDetails
    ${If} $EngineOperation == "Restore"
    ${AndIf} $ChildStatus == "0"
        StrCpy $RemovalRestored "1"
    ${EndIf}
    ${If} $InstallError != ""
        Return
    ${EndIf}
    StrCpy $EngineExitCode "$ChildStatus"
    ${If} $ChildStatus != "0"
        StrCpy $InstallError "error=engine_failed child_exit=$ChildStatus"
        Return
    ${EndIf}
    ${IfNot} ${FileExists} "$HelperLog"
        StrCpy $InstallError "error=helper_log_missing"
        StrCpy $EngineExitCode "3"
        Return
    ${EndIf}
    ${If} $DiagnosticWriteFailed == "1"
        StrCpy $InstallError "error=diagnostic_write_failed native_error=$DiagnosticNativeError child_exit=$ChildStatus"
        StrCpy $EngineExitCode "3"
    ${EndIf}
FunctionEnd
!macroend

!insertmacro MEDIEVAL_DIAGNOSTICS ""
!insertmacro MEDIEVAL_DIAGNOSTICS "un."
!insertmacro MEDIEVAL_ENGINE_REQUEST ""
!insertmacro MEDIEVAL_ENGINE_REQUEST "un."

Function FailInstallation
    Call LogPhase
    StrCpy $LogLine "result=failure $InstallError"
    Call WriteDiagnostic
    DetailPrint "$InstallError"
    DetailPrint "Diagnostics: $LogDirectory"
    MessageBox MB_ICONSTOP|MB_OK "Installation could not complete during $InstallPhase.$\r$\n$EngineOutput$\r$\n$InstallError$\r$\n$\r$\nDiagnostics:$\r$\n$LogDirectory$\r$\n$\r$\nKeep any recovery files and include these logs when requesting help." /SD IDOK
    ${If} $EngineExitCode == "740"
        SetErrorLevel 740
    ${ElseIf} $ChildStatus == "3"
    ${OrIf} $EngineExitCode == "3"
        SetErrorLevel 3
    ${Else}
        SetErrorLevel 2
    ${EndIf}
    IfSilent 0 +2
    Quit
    Abort
FunctionEnd

; Only a validated, owned machine-wide legacy entry can request elevation.
; Relaunch the complete setup so it extracts and validates its own helper and
; payload. Bind it to this Windows account and the already selected game path.
Function InstallWithAdministratorPermission
    IfSilent elevationDone
    StrCpy $InstallError ""
    StrCpy $ElevatedProcess ""
    System::Call 'kernel32::GetCurrentProcess() p.r0'
    System::Call 'advapi32::OpenProcessToken(p r0,i 8,*p.r1) i.r2'
    ${If} $2 == 0
        Goto elevationIdentityFailed
    ${EndIf}
    System::Call 'advapi32::GetTokenInformation(p r1,i 1,p 0,i 0,*i.r2)'
    System::Alloc $2
    Pop $3
    ${If} $3 == 0
        System::Call 'kernel32::CloseHandle(p r1)'
        Goto elevationIdentityFailed
    ${EndIf}
    System::Call 'advapi32::GetTokenInformation(p r1,i 1,p r3,i r2,*i.r2) i.r4'
    System::Call 'kernel32::CloseHandle(p r1)'
    ${If} $4 != 0
        System::Call '*$3(p.r0)'
        System::Call 'advapi32::ConvertSidToStringSidW(p r0,*p.r1) i.r4'
        ${If} $4 != 0
            System::Call 'kernel32::lstrcpynW(w.r0,p r1,i ${NSIS_MAX_STRLEN})'
            StrCpy $RequiredOwnerSid "$0"
            System::Call 'kernel32::LocalFree(p r1)'
        ${EndIf}
    ${EndIf}
    System::Free $3
    ${If} $4 == 0
    ${OrIf} $RequiredOwnerSid == ""
        Goto elevationIdentityFailed
    ${EndIf}
    DetailPrint "Windows permission is needed to update an older patch entry."
    ; NSIS /D= must be last and unquoted, including paths with spaces.
    StrCpy $ElevationArguments '/S /REQUIREOWNER=$RequiredOwnerSid /LOGDIR="$LogDirectory\elevated" /TERRAINFIX=$TerrainSelected /SCROLLFIX=$ScrollSelected /SPRITEFIX=$SpriteSelected /D=$INSTDIR'
    StrLen $0 $ElevationArguments
    ${If} $0 >= 1023
        StrCpy $InstallError "The administrator command is too long. Close setup and use Run as administrator."
        Return
    ${EndIf}
    StrCpy $LogLine "elevation_requested=1 target=$INSTDIR owner=$RequiredOwnerSid"
    Call WriteDiagnostic
    Call RequireDiagnostics
    ; x86 SHELLEXECUTEINFOW is 60 bytes. NOCLOSEPROCESS|NOASYNC retains a
    ; waitable process handle; success is the child's exit plus Verify below.
    ; Register operands keep quotes in command arguments out of System.dll's
    ; own signature parser (interpolating them corrupts the struct fields).
    StrCpy $R0 "$EXEPATH"
    StrCpy $R1 "$ElevationArguments"
    StrCpy $R2 "$EXEDIR"
    System::Call '*(i 60,i 0x140,p $HWNDPARENT,w "runas",w r10,w r11,w r12,i 0,p 0,p 0,p 0,p 0,i 0,p 0,p 0) p.r0'
    System::Call 'shell32::ShellExecuteExW(p r0) i.r1 ?e'
    Pop $2
    ${If} $1 != 0
        IntOp $1 $0 + 56
        System::Call '*$1(p .s)'
        Pop $ElevatedProcess
    ${EndIf}
    System::Free $0
    ${If} $ElevatedProcess == ""
    ${OrIf} $ElevatedProcess == 0
        StrCpy $InstallError "Administrator permission was not granted (Windows error $2). The previous installation has been kept."
        Return
    ${EndIf}
    ${Do}
        System::Call 'kernel32::WaitForSingleObject(p $ElevatedProcess,i 250) i.r0'
    ${LoopWhile} $0 == 258
    StrCpy $ElevatedExit "2"
    ${If} $0 == 0
        System::Call 'kernel32::GetExitCodeProcess(p $ElevatedProcess,*i .s) i.r1'
        Pop $ElevatedExit
        ${If} $1 == 0
            StrCpy $ElevatedExit "2"
        ${EndIf}
    ${EndIf}
    System::Call 'kernel32::CloseHandle(p $ElevatedProcess)'
    StrCpy $LogLine "elevated_child_exit=$ElevatedExit diagnostics=$LogDirectory\elevated"
    Call WriteDiagnostic
    StrCpy $EngineExitCode "$ElevatedExit"
    ${If} $ElevatedExit != 0
        StrCpy $InstallError "The administrator operation did not complete. Diagnostics: $LogDirectory\elevated"
        Return
    ${EndIf}
    StrCpy $EngineOperation "Verify"
    Call InvokeEngine
    Return
elevationIdentityFailed:
    StrCpy $InstallError "Could not identify the Windows account for administrator permission. Close setup and use Run as administrator with the same account."
elevationDone:
FunctionEnd

Function RequireDiagnostics
    ${If} $DiagnosticWriteFailed == "1"
        StrCpy $InstallError "error=diagnostic_write_failed native_error=$DiagnosticNativeError"
        Call FailInstallation
    ${EndIf}
FunctionEnd

Function LogUserAbort
    StrCpy $LogLine "user_cancelled=1"
    Call WriteDiagnostic
    SetErrorLevel 1
FunctionEnd

Function .onGUIEnd
    ${If} $LogHandle != ""
        StrCpy $LogLine "installer_closed=1"
        Call WriteDiagnostic
        FileClose $LogHandle
        StrCpy $LogHandle ""
    ${EndIf}
FunctionEnd
