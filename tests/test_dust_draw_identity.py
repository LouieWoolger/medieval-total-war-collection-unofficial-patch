"""Compile and exercise the production dust phase selector on PE32."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="Win32 dust proxy")


def test_dust_phase_identity_and_reference_work(tmp_path: Path) -> None:
    clang = shutil.which("clang-cl.exe") or str(
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) /
        "LLVM/bin/clang-cl.exe"
    )
    assert Path(clang).is_file(), "Install the x86-capable clang-cl build tool"
    output = tmp_path / "native_dust_draw_identity.exe"
    result = subprocess.run(
        [clang, "/nologo", "/TC", "--target=i686-pc-windows-msvc",
         "/O2", "/W4", "/WX", "/I" + str(ROOT / "vendor/dust-fidelity"),
         "/Fe" + str(output),
         str(ROOT / "tests/native_dust_draw_identity.c"),
         "/link", "/MACHINE:X86", "/BREPRO", "/INCREMENTAL:NO"],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "RESULT passed=24 failed=0" in run.stdout
