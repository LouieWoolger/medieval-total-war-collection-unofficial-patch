"""Exercise the battle HUD atlas repair guard as x86 native code."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="Win32 graphics hook")


def test_battle_icon_atlas_guard(tmp_path: Path) -> None:
    cc = Path(os.environ.get("MTW_CLANG_CL", r"C:\Program Files\LLVM\bin\clang-cl.exe"))
    assert cc.is_file(), "Set MTW_CLANG_CL to an x86-capable clang-cl"
    output = tmp_path / "native_battle_icon_atlas.exe"
    result = subprocess.run(
        [str(cc), "/nologo", "/TC", "--target=i686-pc-windows-msvc",
         "/O2", "/W4", "/WX",
         "/I" + str(ROOT / "vendor/r185/source"),
         "/Fe" + str(output), str(ROOT / "tests/native_battle_icon_atlas.c"),
         "/link", "/MACHINE:X86", "/BREPRO", "/INCREMENTAL:NO"],
        capture_output=True, text=True, timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "RESULT passed=16 failed=0" in run.stdout
