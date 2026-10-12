"""Exercise the shipping campaign-text transform against the exact game asset."""
from __future__ import annotations

import os
import hashlib
from pathlib import Path
import shutil
import subprocess

import pytest

from game_fixture_assets import STOCK_ENGLISH_SHA256


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def test_campaign_option_locale_transform_roundtrip_and_rejects_modified_input(tmp_path: Path) -> None:
    source_exe = os.environ.get("MTW_TEST_GAME_EXE")
    if not source_exe:
        pytest.skip("MTW_TEST_GAME_EXE is required for the original campaign text")
    locale = Path(source_exe).parent / "Loc" / "Eng" / "CHANGES.TXT"
    assert sha256(locale) == STOCK_ENGLISH_SHA256
    compiler = Path(os.environ["MTW_CC"])
    assert compiler.is_file()
    executable = tmp_path / "loc.exe"
    command = [str(compiler), "-std=c99", "-D_WIN32_WINNT=0x0501", "-Os",
               "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
               str(ROOT / "tests" / "native_campaign_locale_patch.c"),
               "-ladvapi32", "-lrpcrt4", "-lversion", "-o", str(executable)]
    env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ["PATH"])
    built = subprocess.run(command, capture_output=True, text=True, env=env, timeout=120)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(executable), str(locale)], capture_output=True,
                             text=True, timeout=30)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert checked.stdout.strip() == "campaign locale transform: PASS"
    altered = tmp_path / "CHANGES.TXT"
    shutil.copy2(locale, altered)
    data = bytearray(altered.read_bytes())
    data[100] ^= 1
    altered.write_bytes(data)
    rejected = subprocess.run([str(executable), str(altered)], capture_output=True,
                              text=True, timeout=30)
    assert rejected.returncode != 0
    assert "unsupported identity" in rejected.stdout
