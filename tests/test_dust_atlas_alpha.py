"""The particle atlas mask can be restored without changing its RGB art."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.skipif(os.name != "nt", reason="Windows native graphics build")
def test_dust_atlas_alpha_repair(tmp_path: Path) -> None:
    compiler = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM/bin/clang++.exe"
    if not compiler.is_file():
        pytest.skip("Clang C++ compiler unavailable")
    executable = tmp_path / "dust-atlas-alpha-tests.exe"
    build = subprocess.run(
        [str(compiler), "--target=i686-pc-windows-msvc", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
         str(ROOT / "tests/native_dust_atlas_alpha.cpp"), "-o", str(executable)],
        capture_output=True, text=True, timeout=60,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    capture = os.environ.get("MTW_DUST_ATLAS_CAPTURE")
    command = [str(executable)] + ([capture] if capture else [])
    alternate = os.environ.get("MTW_DUST_ATLAS_CAPTURE_ALT")
    if alternate:
        assert capture, "The alternate capture requires the first capture"
        command.append(alternate)
    extra = os.environ.get("MTW_DUST_ATLAS_CAPTURE_EXTRA")
    if extra:
        assert capture, "The extra capture requires the first capture"
        command.append(extra)
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "dust atlas alpha repair passed" in result.stdout
