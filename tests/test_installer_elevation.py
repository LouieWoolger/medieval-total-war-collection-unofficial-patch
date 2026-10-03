"""Execute the production NSIS handoff body with a harmless child process.

Only the ShellExecute verb changes from runas to open, so automated tests never
display UAC. The real permission prompt is validated separately on Windows.
"""
import os
from pathlib import Path
import subprocess
import pytest


ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("child_exit", [0, 37])
def test_shell_handoff_preserves_child_exit_and_account(tmp_path, child_exit):
    source = (ROOT / "installer-support.nsh").read_text(encoding="utf-8")
    body = source.split("Function InstallWithAdministratorPermission\n", 1)[1].split("\nFunctionEnd", 1)[0]
    body = body.replace('w "runas"', 'w "open"')
    script = '''Unicode true
RequestExecutionLevel user
OutFile "handoff.exe"
!include LogicLib.nsh
!include FileFunc.nsh
Var EngineExitCode
Var InstallError
Var ElevatedProcess
Var RequiredOwnerSid
Var ElevationArguments
Var ElevatedExit
Var LogDirectory
Var LogLine
Var EngineOperation
Function WriteDiagnostic
  FileOpen $9 "$EXEDIR\\handoff.log" a
  FileSeek $9 0 END
  FileWrite $9 "$LogLine$\\r$\\n"
  FileClose $9
FunctionEnd
Function RequireDiagnostics
FunctionEnd
Function InvokeEngine
  StrCpy $LogLine "verified=$EngineOperation"
  Call WriteDiagnostic
  StrCpy $EngineExitCode "0"
FunctionEnd
Function InstallWithAdministratorPermission
''' + body + '''
FunctionEnd
Function .onInit
  IfSilent child
  ; Bound this fixture to one child even if a regression drops /S.
  IfFileExists "$EXEDIR\\launched.marker" child
  FileOpen $9 "$EXEDIR\\launched.marker" w
  FileClose $9
  StrCpy $LogDirectory "$EXEDIR\\logs with spaces"
  StrCpy $INSTDIR "$EXEDIR\\Game ü with spaces"
  Call InstallWithAdministratorPermission
  StrCpy $LogLine "parent=$EngineExitCode error=$InstallError"
  Call WriteDiagnostic
  SetErrorLevel $EngineExitCode
  Quit
child:
  ${GetParameters} $0
  ${GetOptions} $0 "/REQUIREOWNER=" $1
  FileOpen $9 "$EXEDIR\\child.txt" w
  FileWrite $9 "$1$\\r$\\n$INSTDIR"
  FileClose $9
  SetErrorLevel 37
  Quit
FunctionEnd
Section
SectionEnd
'''
    script = script.replace('SetErrorLevel 37', 'SetErrorLevel ' + str(child_exit))
    path = tmp_path / 'handoff.nsi'
    path.write_text(script, encoding='utf-8-sig')
    compiler = Path(os.environ['MTW_TEST_MAKENSIS'])
    built = subprocess.run([str(compiler), '/V2', str(path)], cwd=tmp_path, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, (built.stdout, built.stderr)
    result = subprocess.run([str(tmp_path / 'handoff.exe')], cwd=tmp_path, capture_output=True, timeout=30)
    assert result.returncode == child_exit, (result.returncode, (tmp_path / 'handoff.log').read_text())
    sid, target = (tmp_path / 'child.txt').read_text().splitlines()
    assert sid.startswith('S-1-5-21-')
    assert target == str(tmp_path / 'Game ü with spaces')
    log = (tmp_path / 'handoff.log').read_text()
    assert f'elevated_child_exit={child_exit}' in log
    assert ('verified=Verify' in log) == (child_exit == 0)
