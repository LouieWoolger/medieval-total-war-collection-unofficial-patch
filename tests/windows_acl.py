"""Apply and restore ACL changes on test-owned Windows paths."""
from __future__ import annotations

import base64
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import uuid


def powershell(script: str) -> str:
    command = ["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive",
               "-ExecutionPolicy", "Bypass", "-EncodedCommand",
               base64.b64encode(script.encode("utf-16-le")).decode("ascii")]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    return result.stdout.strip()


def ps_literal(value: str | Path) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def security_api():
    library = ctypes.WinDLL("advapi32", use_last_error=True)
    library.GetFileSecurityW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p,
                                        wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    library.GetFileSecurityW.restype = wintypes.BOOL
    library.SetFileSecurityW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p]
    library.SetFileSecurityW.restype = wintypes.BOOL
    return library


def read_dacl(path: Path) -> bytes:
    library = security_api()
    needed = wintypes.DWORD()
    assert not library.GetFileSecurityW(str(path), 4, None, 0, ctypes.byref(needed))
    assert ctypes.get_last_error() == 122  # ERROR_INSUFFICIENT_BUFFER, size query only.
    buffer = ctypes.create_string_buffer(needed.value)
    assert library.GetFileSecurityW(str(path), 4, buffer, len(buffer), ctypes.byref(needed)), ctypes.get_last_error()
    return buffer.raw[:needed.value]


@contextmanager
def deny_directory_right(directory: Path, right: str):
    """Deny the current account one directory right and restore the original DACL."""
    original = read_dacl(directory)
    # Keep restoration data outside the affected directory if the test is interrupted.
    saved = directory.parent / ("original-dacl-" + uuid.uuid4().hex + ".bin")
    saved.write_bytes(original)
    saved.with_suffix(".json").write_text(
        json.dumps({"target": str(directory), "descriptor": saved.name}), encoding="utf-8")
    try:
        powershell(
            "$ErrorActionPreference='Stop'\n"
            f"$path={ps_literal(directory)}\n"
            "$acl=[IO.Directory]::GetAccessControl($path)\n"
            "$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User\n"
            f"$right=[Security.AccessControl.FileSystemRights]::{right}\n"
            "$rule=New-Object Security.AccessControl.FileSystemAccessRule($sid,$right,[Security.AccessControl.InheritanceFlags]::None,[Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Deny)\n"
            "$acl.AddAccessRule($rule)\n"
            "[IO.Directory]::SetAccessControl($path,$acl)\n"
        )
        yield
    finally:
        # Restore the original descriptor including its inheritance flags.
        buffer = ctypes.create_string_buffer(original)
        assert security_api().SetFileSecurityW(str(directory), 4, buffer), ctypes.get_last_error()
        assert read_dacl(directory) == original
