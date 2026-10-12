"""Compile and exercise the menu conversion hook's native selector and guards."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="Win32 graphics hook")


def test_exact_menu_conversion_selector(tmp_path: Path) -> None:
    cc = os.environ.get("MTW_CC") or shutil.which("i686-w64-mingw32-gcc.exe")
    assert cc, "Set MTW_CC to an i686 MinGW C99 compiler"
    triple = subprocess.check_output([cc, "-dumpmachine"], text=True).strip()
    assert triple == "i686-w64-mingw32"
    version = subprocess.check_output([cc, "-dumpfullversion", "-dumpversion"], text=True).strip()
    bindir = Path(cc).resolve().parent
    output = tmp_path / "native_menu_focus_conversion.exe"
    result = subprocess.run(
        [cc, "-B", str(bindir.parent / "libexec/gcc" / triple / version) + os.sep,
         "-std=c99", "-Os", "-Wall", "-Wextra", "-Werror",
         "-I", str(ROOT / "vendor/r185/source"),
         str(ROOT / "tests/native_menu_focus_conversion.c"), "-o", str(output)],
        capture_output=True, text=True, timeout=120,
        env=dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ["PATH"]),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "RESULT passed=18 failed=0" in run.stdout


def test_shader_marker_survives_duplicate_objects_and_device_recreation(
    tmp_path: Path,
) -> None:
    clang = shutil.which("clang-cl.exe") or str(
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) /
        "LLVM/bin/clang-cl.exe"
    )
    assert Path(clang).is_file(), "Install the x86-capable clang-cl build tool"
    output = tmp_path / "native_menu_shader_marker.exe"
    result = subprocess.run(
        [clang, "/nologo", "/TC", "--target=i686-pc-windows-msvc",
         "/O2", "/W4", "/WX", "/I" + str(ROOT / "vendor/r185/source"),
         "/Fe" + str(output),
         str(ROOT / "tests/native_menu_shader_marker.c"),
         "/link", "/MACHINE:X86", "/BREPRO", "/INCREMENTAL:NO",
         "d3d11.lib"],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "RESULT passed=16 failed=0" in run.stdout
