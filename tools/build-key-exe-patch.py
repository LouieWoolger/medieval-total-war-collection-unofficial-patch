"""Build a reversible expanded Controls capture, naming and held-key repair.

Complete game executables may be written only under the task artifact root on
G:. The original configurable-owner collision loop remains untouched.
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
SECTION_NAME = b".mtwkey\0"
SECTION_FLAGS = 0x60000020
IMAGE_SIZES = (0xB4C000, 0xB4D000, 0xB4E000, 0xB4F000, 0xB50000, 0xB51000)
BACKSPACE_FLAG_RAW = 0x391552
SPACE_FLAG_RAW = 0x391551
DELETE_FLAG_RAWS = (0x391531, 0x391557)
EXTRA_FIXED_SLOTS = (74, 75, *range(78, 98), 103, 104, 105, 107, 108, 109, 110)
EXTRA_FLAG_RAWS = tuple(0x39152C + slot - 63 for slot in EXTRA_FIXED_SLOTS)
COLLISION_SITE = 0x7116F0
ORIGINAL_COLLISION = bytes.fromhex("8B 14 8D 38 94 C7 00")
HELD_SITE = 0x4D5D80
ORIGINAL_HELD = bytes.fromhex("A1 B0 29 B4 00")
GHOST_SITE = 0x4CC040
ORIGINAL_GHOST = bytes.fromhex("8A 41 04 84 C0")
SPECIAL_SITE = 0x4D585C
ORIGINAL_SPECIAL = bytes.fromhex(
    "A1 88 81 E7 00 25 FF FF 00 00 50 FF 15 C0 42 78 00 "
    "66 8B D8 C1 EB 0F 80 E3 01")
SPECIAL_LENGTH = len(ORIGINAL_SPECIAL)
GHOST_OFFSET = 0x200
DISPATCH_OFFSET = 0x400
LABEL_OFFSET = 0x700
DISPATCH_SITE = 0x4C8E56
ORIGINAL_DISPATCH = bytes.fromhex("BE 70 00 00 00 C7 45 14 34 83 E7 00")
LABEL_SITE = 0x71238E
ORIGINAL_LABEL = bytes.fromhex("FF 15 E0 42 78 00")


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def _ui_module():
    path = ROOT / "tools" / "build-ui-exe-patch.py"
    spec = importlib.util.spec_from_file_location("mtw_key_ui", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


UI = _ui_module()


def image_size(data: bytes) -> int:
    return UI.pe_layout(data)[3]


def _nasm() -> str:
    path = os.environ.get("MTW_NASM") or shutil.which("nasm")
    if path:
        return path
    wrapper = Path(r"F:\AI_Projects\Tools\nasm.cmd")
    if wrapper.is_file():
        return str(wrapper)
    raise RuntimeError("NASM is required for the PE32 key helper")


@lru_cache(maxsize=6)
def assemble_hook(size: int) -> bytes:
    if size not in IMAGE_SIZES:
        raise ValueError("unsupported key section RVA")
    with tempfile.TemporaryDirectory(prefix="mtw-key-asm-") as scratch:
        output = Path(scratch) / "key_hook.bin"
        command = [_nasm(), "-f", "bin",
                   f"-DKEY_CODE_VA=0x{IMAGE_BASE + size:08X}",
                   str(ROOT / "vendor" / "key_exe" / "key_hook.asm"),
                   "-o", str(output)]
        run = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if run.returncode:
            raise RuntimeError(f"NASM failed: {run.stdout}\n{run.stderr}")
        hook = output.read_bytes()
    if not DISPATCH_OFFSET < len(hook) <= PAGE:
        raise ValueError("unexpected key helper size")
    return hook


def jump_bytes(site: int, destination: int) -> bytes:
    return b"\xE9" + struct.pack("<i", destination - (site + 5))


def call_bytes(site: int, destination: int) -> bytes:
    return b"\xE8" + struct.pack("<i", destination - (site + 5))


def special_bytes() -> bytes:
    # push 5; call modifier-aware helper; discard argument; mov ebx,eax.
    prefix = (b"\x6A\x05\xE8" +
              struct.pack("<i", HELD_SITE - (SPECIAL_SITE + 7)) +
              b"\x83\xC4\x04\x89\xC3")
    return prefix.ljust(SPECIAL_LENGTH, b"\x90")


def section_bytes(hook: bytes, size: int, raw: int) -> bytes:
    return struct.pack("<8sIIIIIIHHI", SECTION_NAME, len(hook), size,
                       PAGE, raw, 0, 0, 0, 0, SECTION_FLAGS)


def _input_layout(data: bytes) -> tuple[int, int, int, int, int]:
    pe, opt, table, size = UI.pe_layout(data)
    count = struct.unpack_from("<H", data, pe + 6)[0]
    if (size not in IMAGE_SIZES or len(data) % PAGE or
            not 6 <= count <= 11 or
            not 0x4E7000 <= len(data) <= 0x4EC000 or
            data[table + count*40:table + (count+1)*40] != bytes(40) or
            data[BACKSPACE_FLAG_RAW] != 0 or
            data[SPACE_FLAG_RAW] != 0 or
            any(data[flag] != 0 for flag in DELETE_FLAG_RAWS) or
            any(data[flag] != 0 for flag in EXTRA_FLAG_RAWS) or
            data[LABEL_SITE-IMAGE_BASE:
                 LABEL_SITE-IMAGE_BASE+len(ORIGINAL_LABEL)] != ORIGINAL_LABEL or
            data[DISPATCH_SITE-IMAGE_BASE:
                 DISPATCH_SITE-IMAGE_BASE+len(ORIGINAL_DISPATCH)] != ORIGINAL_DISPATCH or
            data[COLLISION_SITE-IMAGE_BASE:
                 COLLISION_SITE-IMAGE_BASE+len(ORIGINAL_COLLISION)] != ORIGINAL_COLLISION or
            data[HELD_SITE-IMAGE_BASE:HELD_SITE-IMAGE_BASE+5] != ORIGINAL_HELD or
            data[GHOST_SITE-IMAGE_BASE:GHOST_SITE-IMAGE_BASE+5] != ORIGINAL_GHOST or
            data[SPECIAL_SITE-IMAGE_BASE:
                 SPECIAL_SITE-IMAGE_BASE+SPECIAL_LENGTH] != ORIGINAL_SPECIAL):
        raise ValueError("unsupported or partially patched key executable")
    return pe, opt, table, size, count


def add_key(base: bytes, checksum_style: str = "base") -> bytes:
    if checksum_style not in ("base", "retain", "zero", "recompute"):
        raise ValueError("unknown checksum style")
    pe, opt, table, size, count = _input_layout(base)
    hook = assemble_hook(size)
    result = bytearray(base + hook.ljust(PAGE, b"\0"))
    struct.pack_into("<H", result, pe + 6, count + 1)
    struct.pack_into("<I", result, opt + 4,
                     struct.unpack_from("<I", base, opt + 4)[0] + PAGE)
    struct.pack_into("<I", result, opt + 56, size + PAGE)
    result[table+count*40:table+(count+1)*40] = section_bytes(hook, size, len(base))
    result[BACKSPACE_FLAG_RAW] = 1
    result[SPACE_FLAG_RAW] = 1
    for flag in DELETE_FLAG_RAWS:
        result[flag] = 1
    for flag in EXTRA_FLAG_RAWS:
        result[flag] = 1
    result[LABEL_SITE-IMAGE_BASE:
           LABEL_SITE-IMAGE_BASE+len(ORIGINAL_LABEL)] = (
        call_bytes(LABEL_SITE, IMAGE_BASE + size + LABEL_OFFSET) + b"\x90")
    result[DISPATCH_SITE-IMAGE_BASE:
           DISPATCH_SITE-IMAGE_BASE+len(ORIGINAL_DISPATCH)] = (
        jump_bytes(DISPATCH_SITE, IMAGE_BASE + size + DISPATCH_OFFSET)
        .ljust(len(ORIGINAL_DISPATCH), b"\x90"))
    result[HELD_SITE-IMAGE_BASE:HELD_SITE-IMAGE_BASE+5] = jump_bytes(
        HELD_SITE, IMAGE_BASE + size)
    result[GHOST_SITE-IMAGE_BASE:GHOST_SITE-IMAGE_BASE+5] = jump_bytes(
        GHOST_SITE, IMAGE_BASE + size + GHOST_OFFSET)
    result[SPECIAL_SITE-IMAGE_BASE:SPECIAL_SITE-IMAGE_BASE+SPECIAL_LENGTH] = special_bytes()
    if checksum_style in ("base", "recompute"):
        struct.pack_into("<I", result, opt + 64, 0)
        struct.pack_into("<I", result, opt + 64, UI.pe_checksum(result, opt + 64))
    elif checksum_style == "zero":
        struct.pack_into("<I", result, opt + 64, 0)
    return bytes(result)


def strip_key(patched: bytes, original_checksum: int,
              checksum_style: str = "base") -> bytes:
    if checksum_style not in ("base", "retain", "zero", "recompute"):
        raise ValueError("unknown checksum style")
    pe, opt, table, new_size = UI.pe_layout(patched)
    size = new_size - PAGE
    count = struct.unpack_from("<H", patched, pe + 6)[0]
    raw = len(patched) - PAGE
    if size not in IMAGE_SIZES or not 7 <= count <= 12 or raw % PAGE:
        raise ValueError("not a supported key-patched executable")
    hook = assemble_hook(size)
    expected_checksum = (UI.pe_checksum(patched, opt + 64)
                         if checksum_style in ("base", "recompute")
                         else original_checksum if checksum_style == "retain" else 0)
    if (patched[raw:] != hook.ljust(PAGE, b"\0") or
            patched[table+(count-1)*40:table+count*40] !=
                section_bytes(hook, size, raw) or
            patched[BACKSPACE_FLAG_RAW] != 1 or
            patched[SPACE_FLAG_RAW] != 1 or
            any(patched[flag] != 1 for flag in DELETE_FLAG_RAWS) or
            any(patched[flag] != 1 for flag in EXTRA_FLAG_RAWS) or
            patched[LABEL_SITE-IMAGE_BASE:
                    LABEL_SITE-IMAGE_BASE+len(ORIGINAL_LABEL)] !=
                (call_bytes(LABEL_SITE, IMAGE_BASE + size + LABEL_OFFSET) + b"\x90") or
            patched[DISPATCH_SITE-IMAGE_BASE:
                    DISPATCH_SITE-IMAGE_BASE+len(ORIGINAL_DISPATCH)] !=
                jump_bytes(DISPATCH_SITE, IMAGE_BASE + size + DISPATCH_OFFSET)
                .ljust(len(ORIGINAL_DISPATCH), b"\x90") or
            patched[COLLISION_SITE-IMAGE_BASE:
                    COLLISION_SITE-IMAGE_BASE+len(ORIGINAL_COLLISION)] != ORIGINAL_COLLISION or
            patched[HELD_SITE-IMAGE_BASE:HELD_SITE-IMAGE_BASE+5] !=
                jump_bytes(HELD_SITE, IMAGE_BASE + size) or
            patched[GHOST_SITE-IMAGE_BASE:GHOST_SITE-IMAGE_BASE+5] !=
                jump_bytes(GHOST_SITE, IMAGE_BASE + size + GHOST_OFFSET) or
            patched[SPECIAL_SITE-IMAGE_BASE:
                    SPECIAL_SITE-IMAGE_BASE+SPECIAL_LENGTH] != special_bytes() or
            struct.unpack_from("<I", patched, opt + 64)[0] != expected_checksum):
        raise ValueError("not a complete key-patched executable state")
    result = bytearray(patched[:raw])
    struct.pack_into("<H", result, pe + 6, count - 1)
    struct.pack_into("<I", result, opt + 4,
                     struct.unpack_from("<I", result, opt + 4)[0] - PAGE)
    struct.pack_into("<I", result, opt + 56, size)
    result[table+(count-1)*40:table+count*40] = bytes(40)
    result[BACKSPACE_FLAG_RAW] = 0
    result[SPACE_FLAG_RAW] = 0
    for flag in DELETE_FLAG_RAWS:
        result[flag] = 0
    for flag in EXTRA_FLAG_RAWS:
        result[flag] = 0
    result[LABEL_SITE-IMAGE_BASE:
           LABEL_SITE-IMAGE_BASE+len(ORIGINAL_LABEL)] = ORIGINAL_LABEL
    result[DISPATCH_SITE-IMAGE_BASE:
           DISPATCH_SITE-IMAGE_BASE+len(ORIGINAL_DISPATCH)] = ORIGINAL_DISPATCH
    result[HELD_SITE-IMAGE_BASE:HELD_SITE-IMAGE_BASE+5] = ORIGINAL_HELD
    result[GHOST_SITE-IMAGE_BASE:GHOST_SITE-IMAGE_BASE+5] = ORIGINAL_GHOST
    result[SPECIAL_SITE-IMAGE_BASE:
           SPECIAL_SITE-IMAGE_BASE+SPECIAL_LENGTH] = ORIGINAL_SPECIAL
    struct.pack_into("<I", result, opt + 64, original_checksum)
    return bytes(result)


def emit_include() -> str:
    lines = ["/* Generated from vendor/key_exe/key_hook.asm. */",
             "#ifndef MTW_KEY_HOOK_INC", "#define MTW_KEY_HOOK_INC"]
    for size in IMAGE_SIZES:
        hook = assemble_hook(size)
        label = f"{size:06x}"
        lines += [f"#define MTW_KEY_HOOK_{label.upper()}_LENGTH {len(hook)}u",
                  f"static const unsigned char mtw_key_hook_{label}[] = {{"]
        for start in range(0, len(hook), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in hook[start:start+16]) + ",")
        lines.append("};")
    lines += ["#endif", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--checksum-style",
                        choices=("base", "retain", "zero", "recompute"),
                        default="base")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--inc", type=Path)
    args = parser.parse_args()
    source = args.source.read_bytes()
    if sha256(source) != args.source_sha256.upper():
        raise ValueError("source is not the requested exact executable identity")
    if args.output.resolve().drive.upper() != "G:":
        raise ValueError("complete game executable output must be on G:")
    target = add_key(source, args.checksum_style)
    if strip_key(target, struct.unpack_from("<I", source, 0xD8)[0],
                 args.checksum_style) != source:
        raise AssertionError("key transform is not reversible")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(target)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({
        "schema": "mtw-keybinding-expanded-controls-v4",
        "source_sha256": sha256(source),
        "patched_sha256": sha256(target),
        "source_length": len(source),
        "patched_length": len(target),
        "section": ".mtwkey",
        "reservation_flags_changed": ["plain Space", "plain Backspace", "plain Delete",
                                       "navigation", "Ctrl/Shift+Ctrl digits", "OEM minus/equals"],
        "original_collision_handler_preserved": True,
        "punctuation_label_site": hex(LABEL_SITE),
        "held_sites": [hex(HELD_SITE), hex(SPECIAL_SITE), hex(GHOST_SITE)],
        "battle_dispatch_site": hex(DISPATCH_SITE),
    }, indent=2) + "\n", encoding="utf-8")
    if args.inc:
        args.inc.parent.mkdir(parents=True, exist_ok=True)
        args.inc.write_text(emit_include(), encoding="ascii")
    print(sha256(target))


if __name__ == "__main__":
    main()
