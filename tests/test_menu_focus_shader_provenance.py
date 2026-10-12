"""Keep the exact menu shader signatures tied to their captured/compiled bytes."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "vendor/r185/source/menu_focus_shader_bytecode.h"
HLSL = ROOT / "vendor/r185/source/menu_focus_pixel_aligned_vs.hlsl"
PROBLEM_PS_SHA256 = "F15E8883B596D19C2048A21E1F783988082E15628C182FE1010DC04FAC670063"
ALIGNED_VS_SHA256 = "01C1560A4C5CB79AED2B7D7AF2BAE3C394B79D18380FFCD82DE1321A05A40E2D"


def embedded_shader(name: str, expected_size: int) -> bytes:
    source = HEADER.read_text(encoding="utf-8")
    match = re.search(
        rf"static const unsigned char {re.escape(name)}\[{expected_size}u\] = \{{(.*?)\}};",
        source,
        re.DOTALL,
    )
    assert match is not None, f"Missing or resized {name} array"
    values = re.findall(r"0x([0-9A-Fa-f]{2})\b", match.group(1))
    data = bytes(int(value, 16) for value in values)
    assert len(data) == expected_size
    return data


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def test_embedded_exact_shader_bytecode_hashes() -> None:
    assert digest(embedded_shader("mtw_menu_problem_pixel_shader_dxbc", 800)) == PROBLEM_PS_SHA256
    assert digest(embedded_shader("mtw_menu_pixel_aligned_vertex_shader_dxbc", 896)) == ALIGNED_VS_SHA256


def test_embedded_vertex_shader_matches_hlsl_compilation(tmp_path: Path) -> None:
    fxc = os.environ.get("MTW_FXC") or shutil.which("fxc.exe")
    if not fxc:
        pytest.skip("Set MTW_FXC to the Windows SDK fxc.exe for the source-to-bytecode gate")
    output = tmp_path / "menu_focus_pixel_aligned_vs.dxbc"
    result = subprocess.run(
        [fxc, "/nologo", "/T", "vs_4_0", "/E", "main", "/O3",
         "/Fo", str(output), str(HLSL)],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    compiled = output.read_bytes()
    assert compiled == embedded_shader("mtw_menu_pixel_aligned_vertex_shader_dxbc", 896)
    assert digest(compiled) == ALIGNED_VS_SHA256
