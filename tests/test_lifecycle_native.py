"""Compile and exercise the native guards on disposable files; no game or registry writes."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import pefile
import pytest

ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="native Win32 filesystem fixtures")


def directory_identity(path: Path) -> str:
    class Info(ctypes.Structure):
        _fields_ = [("attributes", wintypes.DWORD), ("creation", wintypes.FILETIME),
                    ("access", wintypes.FILETIME), ("write", wintypes.FILETIME),
                    ("volume", wintypes.DWORD), ("size_hi", wintypes.DWORD),
                    ("size_lo", wintypes.DWORD), ("links", wintypes.DWORD),
                    ("index_hi", wintypes.DWORD), ("index_lo", wintypes.DWORD)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.POINTER(Info)]
    kernel.GetFileInformationByHandle.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x81, 3, None, 3, 0x02200000, None)
    assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    try:
        info = Info()
        assert kernel.GetFileInformationByHandle(handle, ctypes.byref(info)), ctypes.get_last_error()
        return f"{info.volume:08X}:{info.index_hi:08X}{info.index_lo:08X}"
    finally:
        kernel.CloseHandle(handle)


def generate_legacy_fixtures(path: Path) -> None:
    """PowerShell is used only to prove compatibility with its former receipt serialization."""
    powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    script = path / "legacy-fixtures.ps1"
    script.write_text(r"""
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -ne 5){throw 'Legacy receipt fixtures require Windows PowerShell 5.1'}
$utf8=New-Object Text.UTF8Encoding($false)
$value=[ordered]@{schema='v2';text="'`"<>&/"+[char]0x2028+[char]0x2029+[char]0x85+[char]0xe9+[char]0xd83d+[char]0xde00;controls="`b`t`n`f`r"+[char]1;bool=$true;nil=$null;nested=@([ordered]@{z=1;a=-9223372036854775808})}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ps-canonical.json'),($value|ConvertTo-Json -Depth 40 -Compress),$utf8)
$receipt=[ordered]@{schema='unofficial-medieval-total-war-patch-install-v2';version='1.0.0';game_root=$PSScriptRoot;identity='11223344:0011223344556677';files=@([ordered]@{name='dgVoodoo.conf';original_exists=$false;original_sha256=$null;installed_sha256=('A'*64);length=1024});warnings=@();legacy=$true}
$hash=[Security.Cryptography.SHA256]::Create()
try{$receipt['integrity_sha256']=([BitConverter]::ToString($hash.ComputeHash($utf8.GetBytes(($receipt|ConvertTo-Json -Depth 40 -Compress))))).Replace('-','')}finally{$hash.Dispose()}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'legacy-v2-receipt.json'),($receipt|ConvertTo-Json -Depth 40),$utf8)
""", encoding="utf-8-sig")
    result = subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-File", str(script)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr


def compile_c_foundations(directory: Path) -> Path:
    """Build the C foundation fixture used by the shipped installer backend."""
    compiler = os.environ.get("MTW_CC") or shutil.which("i686-w64-mingw32-gcc.exe") or shutil.which("gcc.exe")
    assert compiler, "Set MTW_CC to an i686 MinGW C99 compiler"
    triple = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
    assert triple == "i686-w64-mingw32", triple
    version = subprocess.check_output([compiler, "-dumpfullversion", "-dumpversion"], text=True).strip()
    compiler_directory = Path(compiler).resolve().parent
    prefix = compiler_directory.parent / "libexec/gcc" / triple / version
    args = [compiler]
    if (prefix / "cc1.exe").is_file():
        args += ["-B", str(prefix) + os.sep]
    output = directory / "native_foundations_c.exe"
    args += ["-std=c99", "-D_WIN32_WINNT=0x0501", "-Os", "-Wall", "-Wextra", "-Werror",
             "-municode", "-static", "-static-libgcc", "-Wl,--major-subsystem-version,5,--minor-subsystem-version,1",
             "-I", str(ROOT / "src"), str(ROOT / "tests/native_foundations.c"), "-o", str(output)]
    env = dict(os.environ, PATH=str(compiler_directory) + os.pathsep + os.environ["PATH"])
    built = subprocess.run(args, capture_output=True, text=True, timeout=120, env=env)
    (directory / "compile.json").write_text(json.dumps({"command": args, "exit": built.returncode,
                                                       "stdout": built.stdout, "stderr": built.stderr}, indent=2), encoding="utf-8")
    assert built.returncode == 0, built.stdout + built.stderr
    assert not built.stderr, built.stderr
    nm = compiler_directory / "i686-w64-mingw32-nm.exe"
    if not nm.is_file():
        nm = compiler_directory / "nm.exe"
    symbols = subprocess.run([str(nm), "-C", str(output)], capture_output=True, text=True, timeout=30)
    assert symbols.returncode == 0, symbols.stdout + symbols.stderr
    (directory / "symbols.txt").write_text(symbols.stdout, encoding="utf-8")
    assert not re.search(r"__cxa_|__gxx_|_Unwind_|std::|operator new|operator delete", symbols.stdout)
    with pefile.PE(str(output)) as pe:
        assert pe.FILE_HEADER.Machine == 0x14C
        assert pe.OPTIONAL_HEADER.Magic == 0x10B
        assert (pe.OPTIONAL_HEADER.MajorSubsystemVersion, pe.OPTIONAL_HEADER.MinorSubsystemVersion) == (5, 1)
        assert not pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].VirtualAddress
        imports = {entry.dll.decode().lower() for entry in pe.DIRECTORY_ENTRY_IMPORT}
        assert imports <= {"kernel32.dll", "msvcrt.dll"}
    return output


def test_c_foundation_ownership_hash_and_diagnostics(tmp_path: Path, record_testsuite_property) -> None:
    """Catch leaks on OOM, checksum drift, or falsely successful diagnostic writes."""
    executable = compile_c_foundations(tmp_path)
    source = tmp_path / "unchanged-input.bin"
    source.write_bytes(bytes(range(256)) * 521 + b"recovery-state must survive")
    before = source.read_bytes()
    result = subprocess.run([str(executable), "--suite", str(source), str(tmp_path / "diagnostic.log")],
                            capture_output=True, text=True, encoding="utf-8", errors="strict", timeout=60)
    (tmp_path / "c-foundations.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert source.read_bytes() == before
    assert result.returncode == 0, result.stdout + result.stderr
    assert not result.stderr, result.stderr
    summary = re.search(r"RESULT passed=(\d+) failed=(\d+)", result.stdout)
    assert summary and int(summary[1]) >= 40 and summary[2] == "0", result.stdout
    record_testsuite_property("c_native_assertions", int(summary[1]))
    # Independent Python values cover SHA padding and the file read chunk boundary.
    for length in (0, 1, 55, 56, 63, 64, 65, 127, 128, 32767, 32768, 32769, 1000000):
        data = bytes((index * 37 + 11) % 256 for index in range(length))
        path = tmp_path / f"sha-{length}.bin"
        path.write_bytes(data)
        hashed = subprocess.run([str(executable), "--hash", str(path)], capture_output=True,
                                text=True, timeout=20)
        assert hashed.returncode == 0, hashed.stdout + hashed.stderr
        assert hashed.stdout.strip() == hashlib.sha256(data).hexdigest().upper()
        assert not hashed.stderr
        assert path.read_bytes() == data
    record_testsuite_property("c_python_hash_vectors", 13)
