; Adapted from Shogun v1.3.2 commit 013d5ed, installer-uninstall.nsh.
; NSIS self-copies to TEMP before un.onInit. $INSTDIR identifies the directory
; containing the original root uninstaller, independent of the caller's cwd.
; Never replace it from a registry entry or temporary executable directory.
; The bundled engine validates directory identity and owns all file/registry work.

Function un.Fail
    StrCpy $LogLine "result=failure child_exit=$ChildStatus $InstallError"
    ${If} $ChildStatus == "4"
    ${OrIf} $RemovalRestored == "1"
        StrCpy $LogLine "result=completed_with_warning child_exit=$ChildStatus $InstallError"
    ${EndIf}
    Call un.WriteDiagnostic
    DetailPrint "$InstallError"
    DetailPrint "Diagnostics: $LogDirectory"
    ${If} $ChildStatus == "4"
        MessageBox MB_ICONEXCLAMATION|MB_OK "Patch removal from this folder completed:$\r$\n$INSTDIR$\r$\n$\r$\nSome recovery-file cleanup or diagnostics could not finish.$\r$\n$EngineOutput$\r$\n$\r$\nKeep any remaining recovery files. The patch does not need to be removed again.$\r$\n$\r$\nDiagnostics:$\r$\n$LogDirectory" /SD IDOK
        SetErrorLevel 4
    ${ElseIf} $RemovalRestored == "1"
        MessageBox MB_ICONEXCLAMATION|MB_OK "Removal from this folder completed:$\r$\n$INSTDIR$\r$\n$\r$\nThe final diagnostics could not be saved completely. Any saved logs are here:$\r$\n$LogDirectory" /SD IDOK
        SetErrorLevel 4
    ${ElseIf} $ChildStatus == "3"
        MessageBox MB_ICONEXCLAMATION|MB_OK "Game restoration completed, but patch removal needs another attempt to finish cleanup.$\r$\n$INSTDIR$\r$\n$\r$\n$EngineOutput$\r$\n$\r$\nKeep the uninstaller and recovery files, close the game, and retry.$\r$\n$\r$\nDiagnostics:$\r$\n$LogDirectory" /SD IDOK
        SetErrorLevel 3
    ${Else}
        MessageBox MB_ICONSTOP|MB_OK "The patch could not be completely removed from:$\r$\n$INSTDIR$\r$\n$\r$\n$EngineOutput$\r$\n$InstallError$\r$\n$\r$\nClose the game and retry. Keep the uninstaller and recovery files. If this folder was moved, return it to its original location.$\r$\n$\r$\nDiagnostics:$\r$\n$LogDirectory" /SD IDOK
        SetErrorLevel 2
    ${EndIf}
    IfSilent 0 +2
    Quit
    Abort
FunctionEnd

Function un.onInit
    StrCpy $OperationName "removal"
    StrCpy $ChildStatus "not-started"
    StrCpy $RemovalRestored "0"
    StrCpy $EngineUninstaller ""
    Call un.InitializeDiagnostics
    StrCpy $LogLine "operation=uninstall target=$INSTDIR"
    Call un.WriteDiagnostic
    ${If} $DiagnosticWriteFailed == "1"
        StrCpy $InstallError "error=diagnostic_write_failed native_error=$DiagnosticNativeError"
        Call un.Fail
    ${EndIf}
    StrCpy $InstallPhase "extraction-engine"
    Call un.LogPhase
    ClearErrors
    ; All restoration code and payload identity data remain available even when
    ; the original setup download is gone.
    !insertmacro MEDIEVAL_ENGINE_FILES
    ${If} ${Errors}
        StrCpy $InstallError "error=engine_extraction_failed"
        Call un.Fail
    ${EndIf}
FunctionEnd

Section "Uninstall"
    DetailPrint "Remove the unofficial patch from: $INSTDIR"
    DetailPrint "Your game, saves and unrelated content are kept."
    DetailPrint "Diagnostics: $LogDirectory"
    StrCpy $EngineOperation "Restore"
    Call un.InvokeEngine
    ${If} $EngineExitCode != "0"
    ${OrIf} $InstallError != ""
        Call un.Fail
    ${EndIf}
    StrCpy $InstallPhase "complete"
    Call un.LogPhase
    StrCpy $LogLine "result=success operation=uninstall"
    Call un.WriteDiagnostic
    ${If} $DiagnosticWriteFailed == "1"
        StrCpy $InstallError "error=diagnostic_write_failed after_restoration=1"
        Call un.Fail
    ${EndIf}
    DetailPrint "Unofficial patch removed. Your game is kept."
    SetErrorLevel 0
SectionEnd

Function un.LogUserAbort
    StrCpy $LogLine "user_cancelled=1"
    Call un.WriteDiagnostic
    SetErrorLevel 1
FunctionEnd

Function un.onGUIEnd
    ${If} $LogHandle != ""
        StrCpy $LogLine "uninstaller_closed=1"
        Call un.WriteDiagnostic
        FileClose $LogHandle
        StrCpy $LogHandle ""
    ${EndIf}
FunctionEnd
