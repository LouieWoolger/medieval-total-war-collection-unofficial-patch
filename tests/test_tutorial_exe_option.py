"""Exact standalone campaign-tutorial EXE option, including LAA inputs."""

import hashlib
import importlib.util
import os
from pathlib import Path
import struct

import pytest


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate-laa-exe-variants.py"
STOCK_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
RESET_OFFSET = 0x306A5C
ORIGINAL = bytes.fromhex("89 1D F0 D1 E7 00")
SKIP_RESET = bytes.fromhex("E9 78 00 00 00 90")


def test_tutorial_option_generates_exact_reversible_identities():
    stock_path = os.environ.get("MTW_STOCK_EXE")
    if not stock_path:
        pytest.skip("MTW_STOCK_EXE must name a verified staged stock EXE")
    stock = Path(stock_path).read_bytes()
    assert hashlib.sha256(stock).hexdigest().upper() == STOCK_HASH
    spec = importlib.util.spec_from_file_location("mtw_laa_variants", GENERATOR)
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    variants = generator.build_variants(stock)
    assert len(variants) == len({hashlib.sha256(data).digest() for data in variants.values()}) == 32
    for scroll in (0, 1):
        for sprite in (0, 1):
            for style in ("base", "retain", "zero", "recompute"):
                before = variants[(scroll, sprite, 0, style)]
                after = variants[(scroll, sprite, 1, style)]
                assert before[RESET_OFFSET:RESET_OFFSET + 6] == ORIGINAL
                assert after[RESET_OFFSET:RESET_OFFSET + 6] == SKIP_RESET
                changed = {i for i, (a, b) in enumerate(zip(before, after)) if a != b}
                assert changed <= set(range(RESET_OFFSET, RESET_OFFSET + 6)) | set(range(0xD8, 0xDC))
                assert struct.unpack_from("<H", before, 0x96)[0] == struct.unpack_from("<H", after, 0x96)[0]
