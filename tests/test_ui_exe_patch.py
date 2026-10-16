"""R-06 production EXE section, native transform, and exact identity gates."""
from __future__ import annotations

import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
STOCK_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
GENERATOR_PATH = ROOT / "tools" / "generate-laa-exe-variants.py"
UI_PATH = ROOT / "tools" / "build-ui-exe-patch.py"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def load_script(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def generated():
    stock_path = os.environ.get("MTW_STOCK_EXE")
    if not stock_path:
        pytest.skip("MTW_STOCK_EXE must name a verified staged stock EXE")
    stock = Path(stock_path).read_bytes()
    assert digest(stock) == STOCK_HASH
    generator = load_script(GENERATOR_PATH, "ui_identity_generator")
    return generator, generator.build_ui_variants(stock)


@pytest.fixture(scope="module")
def native_transform(tmp_path_factory):
    compiler = os.environ.get("MTW_CC")
    if not compiler:
        pytest.skip("MTW_CC must name the i686 compiler")
    target = tmp_path_factory.mktemp("native-ui") / "transform.exe"
    environment = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ["PATH"])
    build = subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests" / "native_direct_exe.c"), "-o", str(target),
                            "-ladvapi32"], cwd=ROOT, env=environment,
                           capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stderr
    return target


def run_transform(binary: Path, source: bytes, selections: tuple[int, int, int, int],
                  folder: Path) -> tuple[subprocess.CompletedProcess, bytes | None]:
    source_path = folder / "source.exe"
    output_path = folder / "output.exe"
    source_path.write_bytes(source)
    output_path.unlink(missing_ok=True)
    result = subprocess.run([str(binary), str(source_path),
                             *(str(bit) for bit in selections), str(output_path)],
                            capture_output=True, text=True, timeout=20)
    return result, output_path.read_bytes() if output_path.exists() else None


def test_ui_generator_adds_independent_section_and_preserves_checksum_styles(generated):
    _, variants = generated
    ui = load_script(UI_PATH, "ui_patch_builder")
    assert len(variants) == len({digest(data) for data in variants.values()}) == 64
    for scroll in (0, 1):
        for sprite in (0, 1):
            for tutorial in (0, 1):
                for style in ("base", "retain", "zero", "recompute"):
                    original = variants[(scroll, sprite, tutorial, style, 0)]
                    corrected = variants[(scroll, sprite, tutorial, style, 1)]
                    pe, opt, table, image_size = ui.pe_layout(original)
                    count = struct.unpack_from("<H", original, pe + 6)[0]
                    payload = ui.assemble_hook(image_size)
                    assert corrected[:2] == b"MZ"
                    assert len(corrected) == len(original) + 0x1000
                    assert struct.unpack_from("<H", corrected, pe + 6)[0] == count + 1
                    assert struct.unpack_from("<I", corrected, opt + 56)[0] == image_size + 0x1000
                    assert corrected[table + count * 40:table + (count + 1) * 40] == \
                        ui.section_bytes(payload, image_size, len(original))
                    assert corrected[len(original):] == payload.ljust(0x1000, b"\0")
                    for (site, _), replacement in zip(ui.HOOKS, ui.hook_bytes(image_size)):
                        assert corrected[site - ui.IMAGE_BASE:site - ui.IMAGE_BASE + len(replacement)] == replacement
                    old_checksum = struct.unpack_from("<I", original, 0xD8)[0]
                    checksum = struct.unpack_from("<I", corrected, 0xD8)[0]
                    expected = (ui.pe_checksum(corrected) if style in ("base", "recompute")
                                else old_checksum if style == "retain" else 0)
                    assert checksum == expected
                    assert ui.strip_ui(corrected, old_checksum, style) == original


def test_native_transform_toggles_ui_for_every_supported_input_state(
        generated, native_transform, tmp_path):
    _, variants = generated
    for (scroll, sprite, tutorial, style, ui), source in variants.items():
        for wanted_ui in (0, 1):
            result, output = run_transform(native_transform, source,
                                           (scroll, sprite, tutorial, wanted_ui), tmp_path)
            assert result.returncode == 0, ((scroll, sprite, tutorial, style, ui),
                                            wanted_ui, result.stderr)
            assert output == variants[(scroll, sprite, tutorial, style, wanted_ui)]


def test_native_transform_can_change_other_options_while_ui_is_on(
        generated, native_transform, tmp_path):
    _, variants = generated
    for style in ("base", "retain", "zero", "recompute"):
        source = variants[(1, 1, 1, style, 1)]
        for wanted in ((0, 0, 0, 1), (1, 0, 1, 1), (0, 1, 0, 0)):
            result, output = run_transform(native_transform, source, wanted, tmp_path)
            assert result.returncode == 0, (style, wanted, result.stderr)
            output_style = "base" if style == "base" else "recompute"
            assert output == variants[(*wanted[:3], output_style, wanted[3])]


def test_native_transform_rejects_modified_ui_bytes(
        generated, native_transform, tmp_path):
    _, variants = generated
    source = variants[(1, 1, 1, "base", 1)]
    for offset in (0x86, 0x9C, 0xD0, 0xD8, 0x18EBD8, 0x191245,
                   len(source) - 0x1000 + 16, len(source) - 1):
        damaged = bytearray(source)
        damaged[offset] ^= 1
        result, output = run_transform(native_transform, bytes(damaged), (1, 1, 1, 0), tmp_path)
        assert result.returncode == 6, (offset, result.stderr)
        assert output is None
