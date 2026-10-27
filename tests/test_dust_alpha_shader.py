"""Keep the captured dust shader's transparency operation and input layout intact."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "vendor/dust-fidelity/dust_alpha_modulate_ps.h"
EXPECTED_SHA256 = "CF563669D7503C96377AFCD48357133FE083CCF31073520AB4D6E9A771C7EA42"


def shader_bytes() -> bytes:
    source = HEADER.read_text(encoding="utf-8")
    body = source.split("k_dust_alpha_modulate_ps[] = {", 1)[1].split("};", 1)[0]
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))


def test_embedded_shader_is_the_verified_dust_shader() -> None:
    bytecode = shader_bytes()
    assert len(bytecode) == 876
    assert hashlib.sha256(bytecode).hexdigest().upper() == EXPECTED_SHA256


@pytest.mark.skipif(os.name != "nt", reason="Windows SDK shader disassembler")
def test_shader_multiplies_texture_and_vertex_alpha(tmp_path: Path) -> None:
    sdk = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    candidates = sorted((sdk / "Windows Kits/10/bin").glob("*/x64/fxc.exe"))
    if not candidates:
        pytest.skip("Windows SDK fxc.exe unavailable")
    bytecode_path = tmp_path / "dust-alpha.dxbc"
    bytecode_path.write_bytes(shader_bytes())
    result = subprocess.run(
        [str(candidates[-1]), "/dumpbin", str(bytecode_path)],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "dcl_input_ps linear v2.xyzw" in result.stdout
    assert "dcl_input_ps linear v5.xyzw" in result.stdout
    assert "mul_sat r1.xyzw, r0.xyzw, r2.xyzw" in result.stdout
