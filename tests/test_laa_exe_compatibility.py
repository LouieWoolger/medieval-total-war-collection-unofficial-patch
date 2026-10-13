"""Verified PE32 LAA-only executable identities; no game EXE is stored here."""

import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "generate-laa-exe-variants.py"
HEADER = ROOT / "include" / "mtw_exe_variants.h"
STOCK_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
REPORTED_LAA_HASH = "9FDA59114FB470C6E2E65AC37019D044BB775EF4B76E00E6585D790B129E3710"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


@pytest.fixture(scope="module")
def stock() -> bytes:
    source = os.environ.get("MTW_STOCK_EXE")
    if not source:
        pytest.skip("MTW_STOCK_EXE must name a verified staged stock EXE")
    data = Path(source).read_bytes()
    assert digest(data) == STOCK_HASH
    return data


@pytest.fixture(scope="module")
def generator():
    spec = importlib.util.spec_from_file_location("mtw_laa_variants", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_executable_identity_generator_is_present():
    assert SCRIPT.is_file(), "The LAA identity generator is missing"


@pytest.fixture(scope="module")
def variants(generator, stock):
    return generator.build_variants(stock)


def test_reported_laa_is_exact_supported_stock_header_change(generator, stock, variants):
    original = variants[(0, 0, "base")]
    reported = variants[(0, 0, "recompute")]
    assert original == stock
    assert digest(reported) == REPORTED_LAA_HASH
    assert len(reported) == len(stock) == 0x4E7000
    assert {i for i, (before, after) in enumerate(zip(stock, reported)) if before != after} == {
        0x96, 0xD8, 0xD9}
    assert struct.unpack_from("<H", reported, 0x96)[0] == 0x012E
    assert struct.unpack_from("<I", reported, 0xD8)[0] == generator.pe_checksum(reported, 0xD8)


def test_all_four_states_have_only_three_pinned_laa_forms(generator, variants):
    assert len(variants) == 16
    assert len({digest(data) for data in variants.values()}) == 16
    for scroll in (0, 1):
        for sprite in (0, 1):
            base = variants[(scroll, sprite, "base")]
            for style in ("retain", "zero", "recompute"):
                data = variants[(scroll, sprite, style)]
                assert len(data) == len(base)
                assert {i for i, (a, b) in enumerate(zip(base, data)) if a != b} <= {
                    0x96, 0xD8, 0xD9, 0xDA, 0xDB}
                assert struct.unpack_from("<H", data, 0x96)[0] == (
                    struct.unpack_from("<H", base, 0x96)[0] | 0x20)
            assert struct.unpack_from("<I", variants[(scroll, sprite, "zero")], 0xD8)[0] == 0
            assert struct.unpack_from("<I", variants[(scroll, sprite, "retain")], 0xD8)[0] == (
                struct.unpack_from("<I", base, 0xD8)[0])
            recomputed = variants[(scroll, sprite, "recompute")]
            assert struct.unpack_from("<I", recomputed, 0xD8)[0] == generator.pe_checksum(
                recomputed, 0xD8)


def test_pinned_header_reproduces_from_verified_stock(generator, stock):
    assert HEADER.is_file(), "The shared native identity table is missing"
    assert HEADER.read_bytes() == generator.emit_header(generator.build_records(stock)).encode("ascii")


def test_native_classifier_accepts_only_pinned_variants(tmp_path, variants):
    compiler = os.environ.get("MTW_CC")
    if not compiler:
        pytest.skip("MTW_CC must name the i686 compiler")
    binary = tmp_path / "classify.exe"
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ["PATH"])
    build = subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests" / "native_exe_variants.c"), "-o", str(binary)],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stderr
    for (scroll, sprite, style), data in variants.items():
        result = subprocess.run([str(binary), digest(data), str(len(data))], capture_output=True,
                                text=True, timeout=10)
        assert result.returncode == 0, (style, result.stderr)
        assert result.stdout.strip() == f"{scroll} {sprite} {style}"
    for bad_hash, bad_length in (("0" * 64, 0x4E7000),
                                 (digest(variants[(0, 0, "recompute")]), 0x4E7001)):
        result = subprocess.run([str(binary), bad_hash, str(bad_length)], capture_output=True,
                                text=True, timeout=10)
        assert result.returncode == 2


def test_header_mutations_cannot_be_classified_as_supported(tmp_path, variants):
    compiler = os.environ.get("MTW_CC")
    if not compiler:
        pytest.skip("MTW_CC must name the i686 compiler")
    binary = tmp_path / "classify.exe"
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ["PATH"])
    build = subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests" / "native_exe_variants.c"), "-o", str(binary)],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stderr
    source = variants[(0, 0, "recompute")]
    for offset in (0x1000, 0x97, 0xD8, 0x200):
        altered = bytearray(source)
        altered[offset] ^= 0x01
        result = subprocess.run([str(binary), digest(altered), str(len(altered))],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 2, offset


@pytest.fixture(scope="module")
def native_transform(tmp_path_factory):
    compiler = os.environ.get("MTW_CC")
    if not compiler:
        pytest.skip("MTW_CC must name the i686 compiler")
    binary = tmp_path_factory.mktemp("native-direct") / "transform.exe"
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ["PATH"])
    build = subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests" / "native_direct_exe.c"), "-o", str(binary),
                            "-ladvapi32"], cwd=ROOT, env=env, capture_output=True, text=True,
                           timeout=60)
    assert build.returncode == 0, build.stderr
    return binary


def test_native_direct_transform_preserves_laa_across_all_states(
        tmp_path, native_transform, variants):
    """The shipping native transform must round-trip every exact LAA family member."""
    for (source_scroll, source_sprite, style), source in variants.items():
        source_path = tmp_path / "source.exe"
        source_path.write_bytes(source)
        for desired_scroll in (0, 1):
            for desired_sprite in (0, 1):
                output_path = tmp_path / "result.exe"
                result = subprocess.run([str(native_transform), str(source_path),
                                         str(desired_scroll), str(desired_sprite),
                                         str(output_path)], capture_output=True, text=True, timeout=20)
                assert result.returncode == 0, (
                    (source_scroll, source_sprite, style),
                    (desired_scroll, desired_sprite), result.stderr)
                expected_style = style if (source_scroll, source_sprite) == (
                    desired_scroll, desired_sprite) else (
                        "recompute" if style != "base" else "base")
                assert output_path.read_bytes() == variants[(desired_scroll, desired_sprite,
                                                               expected_style)]


def test_native_direct_transform_rejects_unpinned_laa_edits(
        tmp_path, native_transform, variants):
    source = variants[(1, 1, "recompute")]
    for offset in (0x97, 0xD8, 0x200, 0x4E0000):
        damaged = bytearray(source)
        damaged[offset] ^= 1
        source_path = tmp_path / "damaged.exe"
        output_path = tmp_path / "result.exe"
        source_path.write_bytes(damaged)
        output_path.unlink(missing_ok=True)
        result = subprocess.run([str(native_transform), str(source_path), "0", "0",
                                 str(output_path)], capture_output=True, text=True, timeout=20)
        assert result.returncode == 6, (offset, result.stderr)
        assert not output_path.exists()


@pytest.mark.parametrize("source", ["vendor/dust-fidelity/d3d9_proxy.c",
                                    "vendor/movie-fix/movie_backing_fix.c",
                                    "vendor/r185/source/campaign_fast_moves.c"])
def test_active_runtime_uses_shared_exact_executable_identities(source):
    code = (ROOT / source).read_text(encoding="utf-8")
    assert "mtw_exe_variants.h" in code
    assert "mtw_exe_variant_from_hash(" in code
    assert "supported_exes[]" not in code
    assert "mtw_supported_exe_hashes[]" not in code
