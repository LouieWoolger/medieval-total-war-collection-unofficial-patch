"""Build the optional, reversible Huge-unit economy PE32 section.

The builder takes an explicitly hashed game executable and emits the complete
candidate only under G:. The repository receives generated hook bytes and
hashes, never an executable or proprietary game data.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
IMAGE_BASE = 0x400000
PAGE = 0x1000
SECTION_NAME = b".mtwbal\0"
SECTION_FLAGS = 0x60000020
IMAGE_SIZES = (0xB4C000, 0xB4D000, 0xB4E000, 0xB4F000, 0xB50000)
SITES = (
    (0x550ADB, "unit_support", bytes.fromhex("E8 E0 37 ED FF"), "call"),
    (0x57BC2C, "detail_support", bytes.fromhex("E8 8F 86 EA FF"), "call"),
    (0x632FE3, "info_support", bytes.fromhex("E8 D8 12 DF FF"), "call"),
    (0x7305BD, "time_scalar", bytes.fromhex("8B 0D F8 65 BE 00"), "call"),
    (0x73060C, "cost_scalar", bytes.fromhex("0F AF 35 F8 65 BE 00"), "call"),
    (0x61D1A8, "ai_scalar", bytes.fromhex("0F AF 0D F8 65 BE 00"), "call"),
    (0x61D268, "ai_scalar", bytes.fromhex("0F AF 0D F8 65 BE 00"), "call"),
    (0x61D527, "ai_scalar", bytes.fromhex("0F AF 0D F8 65 BE 00"), "call"),
    (0x62B098, "retrain_discounted", bytes.fromhex("8B 97 44 03 00 00"), "call"),
    (0x62B0F9, "retrain_regular", bytes.fromhex("8B BF 44 03 00 00"), "call"),
    (0x57A841, "overview_support", bytes.fromhex("50 8D 4C 24 60"), "jump"),
)
ENTRIES = ("unit_support", "detail_support", "info_support", "time_scalar",
           "cost_scalar", "ai_scalar", "retrain_discounted", "retrain_regular",
           "overview_support")


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def _ui_module():
    path = ROOT / "tools" / "build-ui-exe-patch.py"
    spec = importlib.util.spec_from_file_location("mtw_balance_ui", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


UI = _ui_module()


def _nasm() -> str:
    path = os.environ.get("MTW_NASM") or shutil.which("nasm")
    if path:
        return path
    wrapper = Path(r"F:\AI_Projects\Tools\nasm.cmd")
    if wrapper.is_file():
        return str(wrapper)
    raise RuntimeError("NASM is required for the exact PE32 economy hook")


@lru_cache(maxsize=5)
def assemble_hook(image_size: int) -> bytes:
    if image_size not in IMAGE_SIZES:
        raise ValueError("unsupported economy code section RVA")
    with tempfile.TemporaryDirectory(prefix="mtw-balance-asm-") as scratch:
        output = Path(scratch) / "economy_hook.bin"
        command = [_nasm(), "-f", "bin",
                   f"-DBALANCE_CODE_VA=0x{IMAGE_BASE + image_size:08X}",
                   str(ROOT / "vendor" / "economy_exe" / "economy_hook.asm"),
                   "-o", str(output)]
        run = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if run.returncode:
            raise RuntimeError(f"NASM failed: {run.stdout}\n{run.stderr}")
        hook = output.read_bytes()
    if not 0x100 <= len(hook) <= PAGE:
        raise ValueError("unexpected economy hook size")
    offsets = struct.unpack_from("<9I", hook)
    if len(set(offsets)) != len(offsets) or not all(36 <= off < len(hook) for off in offsets):
        raise ValueError("invalid economy hook entry table")
    return hook


def hook_bytes(image_size: int) -> tuple[bytes, ...]:
    offsets = dict(zip(ENTRIES, struct.unpack_from("<9I", assemble_hook(image_size))))
    result = []
    for site, name, original, kind in SITES:
        destination = IMAGE_BASE + image_size + offsets[name]
        opcode = b"\xe8" if kind == "call" else b"\xe9"
        replacement = opcode + struct.pack("<i", destination - (site + 5))
        result.append(replacement + b"\x90" * (len(original) - 5))
    return tuple(result)


def section_bytes(hook: bytes, image_size: int, raw_offset: int) -> bytes:
    return struct.pack("<8sIIIIIIHHI", SECTION_NAME, len(hook), image_size,
                       PAGE, raw_offset, 0, 0, 0, 0, SECTION_FLAGS)


def _layout(data: bytes) -> tuple[int, int, int, int]:
    pe, opt, table, image_size = UI.pe_layout(data)
    if image_size not in IMAGE_SIZES or len(data) % PAGE:
        raise ValueError("not a supported economy input")
    return pe, opt, table, image_size


def add_economy(base: bytes, checksum_style: str = "base") -> bytes:
    pe, opt, table, image_size = _layout(base)
    count = struct.unpack_from("<H", base, pe + 6)[0]
    if (checksum_style not in ("base", "retain", "zero", "recompute") or
            not 6 <= count <= 10 or
            len(base) not in tuple(range(0x4E7000, 0x4EB001, PAGE)) or
            base[table + count * 40:table + (count + 1) * 40] != bytes(40) or
            any(base[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] != original
                for site, _, original, _ in SITES)):
        raise ValueError("unsupported or already balanced executable")
    hook = assemble_hook(image_size)
    result = bytearray(base + hook.ljust(PAGE, b"\0"))
    struct.pack_into("<H", result, pe + 6, count + 1)
    struct.pack_into("<I", result, opt + 4,
                     struct.unpack_from("<I", base, opt + 4)[0] + PAGE)
    struct.pack_into("<I", result, opt + 56, image_size + PAGE)
    result[table + count * 40:table + (count + 1) * 40] = section_bytes(hook, image_size, len(base))
    for (site, _, original, _), replacement in zip(SITES, hook_bytes(image_size)):
        result[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] = replacement
    if checksum_style in ("base", "recompute"):
        struct.pack_into("<I", result, opt + 64, 0)
        struct.pack_into("<I", result, opt + 64, UI.pe_checksum(result, opt + 64))
    elif checksum_style == "zero":
        struct.pack_into("<I", result, opt + 64, 0)
    return bytes(result)


def strip_economy(patched: bytes, original_checksum: int,
                  checksum_style: str = "base") -> bytes:
    pe, opt, table, new_image_size = UI.pe_layout(patched)
    image_size = new_image_size - PAGE
    if image_size not in IMAGE_SIZES or len(patched) < PAGE:
        raise ValueError("not a supported balanced executable")
    count = struct.unpack_from("<H", patched, pe + 6)[0]
    raw = len(patched) - PAGE
    hook = assemble_hook(image_size)
    expected_checksum = (UI.pe_checksum(patched, opt + 64)
                         if checksum_style in ("base", "recompute")
                         else original_checksum if checksum_style == "retain" else 0)
    if (checksum_style not in ("base", "retain", "zero", "recompute") or
            not 7 <= count <= 11 or raw % PAGE or
            patched[raw:] != hook.ljust(PAGE, b"\0") or
            patched[table + (count - 1) * 40:table + count * 40] !=
                section_bytes(hook, image_size, raw) or
            struct.unpack_from("<I", patched, opt + 64)[0] != expected_checksum or
            any(patched[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] != replacement
                for (site, _, original, _), replacement in zip(SITES, hook_bytes(image_size)))):
        raise ValueError("not a complete balanced executable state")
    result = bytearray(patched[:raw])
    struct.pack_into("<H", result, pe + 6, count - 1)
    struct.pack_into("<I", result, opt + 4,
                     struct.unpack_from("<I", result, opt + 4)[0] - PAGE)
    struct.pack_into("<I", result, opt + 56, image_size)
    result[table + (count - 1) * 40:table + count * 40] = bytes(40)
    for site, _, original, _ in SITES:
        result[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] = original
    struct.pack_into("<I", result, opt + 64, original_checksum)
    return bytes(result)


def emit_include() -> str:
    lines = ["/* Generated by tools/build-economy-exe-patch.py from economy_hook.asm. */",
             "#ifndef MTW_ECONOMY_HOOK_INC", "#define MTW_ECONOMY_HOOK_INC"]
    for image_size in IMAGE_SIZES:
        hook = assemble_hook(image_size)
        label = f"{image_size:06x}"
        lines += [f"#define MTW_ECONOMY_HOOK_{label.upper()}_LENGTH {len(hook)}u",
                  f"static const unsigned char mtw_economy_hook_{label}[] = {{"]
        for start in range(0, len(hook), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in hook[start:start + 16]) + ",")
        lines.append("};")
    lines += ["#endif", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--checksum-style", choices=("base", "retain", "zero", "recompute"),
                        default="base")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--inc", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_bytes()
    if sha256(source) != args.source_sha256.upper():
        raise ValueError("source is not the requested exact executable identity")
    if args.output.resolve().drive.upper() != "G:":
        raise ValueError("complete game executable output must be on G:")
    target = add_economy(source, args.checksum_style)
    if strip_economy(target, struct.unpack_from("<I", source, 0xD8)[0],
                     args.checksum_style) != source:
        raise AssertionError("economy executable transform is not reversible")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(target)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({"schema": "mtw-economy-exe-v1",
                                         "source_sha256": sha256(source),
                                         "patched_sha256": sha256(target),
                                         "source_length": len(source),
                                         "patched_length": len(target),
                                         "section": ".mtwbal"}, indent=2) + "\n",
                             encoding="utf-8")
    args.inc.parent.mkdir(parents=True, exist_ok=True)
    args.inc.write_text(emit_include(), encoding="ascii")
    print(sha256(target))


if __name__ == "__main__":
    main()
