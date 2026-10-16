"""Build the independent, exact-state character parchment PE32 transform.

The four small x86 hooks are assembled from vendor/ui_exe/ui_hook.asm. Complete
game executables are emitted only to an explicitly selected G: review path.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
IMAGE_BASE = 0x400000
SECTION_NAME = b".mtwui\0\0"
SECTION_FLAGS = 0x60000020
PAGE = 0x1000
IMAGE_SIZES = (0xB4C000, 0xB4D000, 0xB4E000, 0xB4F000)
HOOKS = (
    (0x58EBD8, bytes.fromhex("8B 8E 2C 01 00 00")),
    (0x591245, bytes.fromhex("57 68 00 00 00 01")),
    (0x58EC82, bytes.fromhex("83 C4 0C 89 15 48 02 8D 00")),
    (0x58ECBF, bytes.fromhex("8B 45 84 8B 0D 48 02 8D 00")),
)


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def pe_checksum(data: bytes | bytearray, offset: int = 0xD8) -> int:
    total = 0
    for index in range(0, len(data), 4):
        if index == offset:
            continue
        total += int.from_bytes(data[index:index + 4].ljust(4, b"\0"), "little")
        total = (total & 0xFFFFFFFF) + (total >> 32)
    total = (total & 0xFFFF) + (total >> 16)
    total += total >> 16
    return (total & 0xFFFF) + len(data)


def pe_layout(data: bytes) -> tuple[int, int, int, int]:
    if len(data) < 0x1000 or data[:2] != b"MZ":
        raise ValueError("not a supported PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe != 0x80 or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("unexpected PE header")
    count, opt_size = struct.unpack_from("<HH", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    table = opt + opt_size
    image_size = struct.unpack_from("<I", data, opt + 56)[0]
    if (struct.unpack_from("<H", data, pe + 4)[0] != 0x14C or
            struct.unpack_from("<H", data, opt)[0] != 0x10B or
            struct.unpack_from("<I", data, opt + 28)[0] != IMAGE_BASE or
            struct.unpack_from("<II", data, opt + 32) != (PAGE, PAGE) or
            struct.unpack_from("<I", data, opt + 60)[0] != PAGE or
            table + (count + 1) * 40 > PAGE):
        raise ValueError("unsupported PE32/i386 section layout")
    return pe, opt, table, image_size


def _nasm() -> str:
    path = os.environ.get("MTW_NASM") or shutil.which("nasm")
    if path:
        return path
    wrapper = Path(r"F:\AI_Projects\Tools\nasm.cmd")
    if wrapper.is_file():
        return str(wrapper)
    raise RuntimeError("NASM is required for the exact PE32 UI hook")


@lru_cache(maxsize=4)
def assemble_hook(image_size: int) -> bytes:
    if image_size not in IMAGE_SIZES:
        raise ValueError("unsupported UI code section RVA")
    with tempfile.TemporaryDirectory(prefix="mtw-ui-asm-") as scratch:
        output = Path(scratch) / "ui_hook.bin"
        command = [_nasm(), "-f", "bin", f"-DUI_CODE_VA=0x{IMAGE_BASE + image_size:08X}",
                   str(ROOT / "vendor" / "ui_exe" / "ui_hook.asm"), "-o", str(output)]
        run = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if run.returncode:
            raise RuntimeError(f"NASM failed: {run.stdout}\n{run.stderr}")
        hook = output.read_bytes()
    if not 0x100 <= len(hook) <= PAGE:
        raise ValueError("unexpected UI hook size")
    offsets = struct.unpack_from("<4I", hook)
    if len(set(offsets)) != 4 or not all(16 <= off < len(hook) for off in offsets):
        raise ValueError("invalid UI hook entry table")
    return hook


def hook_bytes(image_size: int) -> tuple[bytes, ...]:
    hook = assemble_hook(image_size)
    offsets = struct.unpack_from("<4I", hook)
    result = []
    for (site, original), offset in zip(HOOKS, offsets):
        displacement = IMAGE_BASE + image_size + offset - (site + 5)
        replacement = b"\xE9" + struct.pack("<i", displacement)
        replacement += b"\x90" * (len(original) - len(replacement))
        result.append(replacement)
    return tuple(result)


def section_bytes(hook: bytes, image_size: int, raw_offset: int) -> bytes:
    return struct.pack("<8sIIIIIIHHI", SECTION_NAME, len(hook), image_size,
                       PAGE, raw_offset, 0, 0, 0, 0, SECTION_FLAGS)


def add_ui(base: bytes, checksum_style: str = "base") -> bytes:
    pe, opt, table, image_size = pe_layout(base)
    section_count = struct.unpack_from("<H", base, pe + 6)[0]
    if (image_size not in IMAGE_SIZES or len(base) % PAGE or
            len(base) not in (0x4E7000, 0x4E8000, 0x4E9000, 0x4EA000) or
            not 6 <= section_count <= 9 or
            base[table + section_count * 40:table + (section_count + 1) * 40] != bytes(40) or
            any(base[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] != original
                for site, original in HOOKS)):
        raise ValueError("unsupported or already-patched character parchment source")
    if checksum_style not in ("base", "retain", "zero", "recompute"):
        raise ValueError("unsupported PE checksum style")
    hook = assemble_hook(image_size)
    result = bytearray(base + hook.ljust(PAGE, b"\0"))
    struct.pack_into("<H", result, pe + 6, section_count + 1)
    struct.pack_into("<I", result, opt + 4, struct.unpack_from("<I", base, opt + 4)[0] + PAGE)
    struct.pack_into("<I", result, opt + 56, image_size + PAGE)
    result[table + section_count * 40:table + (section_count + 1) * 40] = section_bytes(hook, image_size, len(base))
    for (site, original), replacement in zip(HOOKS, hook_bytes(image_size)):
        assert len(replacement) == len(original)
        result[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] = replacement
    if checksum_style in ("base", "recompute"):
        struct.pack_into("<I", result, opt + 64, 0)
        struct.pack_into("<I", result, opt + 64, pe_checksum(result, opt + 64))
    elif checksum_style == "zero":
        struct.pack_into("<I", result, opt + 64, 0)
    # A prior LAA-only "retain" input intentionally keeps its former checksum.
    return bytes(result)


def strip_ui(patched: bytes, original_checksum: int, checksum_style: str = "base") -> bytes:
    pe, opt, table, new_image_size = pe_layout(patched)
    image_size = new_image_size - PAGE
    count = struct.unpack_from("<H", patched, pe + 6)[0]
    raw = len(patched) - PAGE
    hook = assemble_hook(image_size)
    expected_checksum = (pe_checksum(patched, opt + 64) if checksum_style in ("base", "recompute")
                         else original_checksum if checksum_style == "retain" else 0)
    if (checksum_style not in ("base", "retain", "zero", "recompute") or
            image_size not in IMAGE_SIZES or count not in (7, 8, 9, 10) or
            raw % PAGE or patched[raw:] != hook.ljust(PAGE, b"\0") or
            patched[table + (count - 1) * 40:table + count * 40] !=
                section_bytes(hook, image_size, raw) or
            struct.unpack_from("<I", patched, opt + 64)[0] != expected_checksum or
            any(patched[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] != replacement
                for (site, original), replacement in zip(HOOKS, hook_bytes(image_size)))):
        raise ValueError("not a complete pinned UI executable state")
    result = bytearray(patched[:raw])
    struct.pack_into("<H", result, pe + 6, count - 1)
    struct.pack_into("<I", result, opt + 4, struct.unpack_from("<I", result, opt + 4)[0] - PAGE)
    struct.pack_into("<I", result, opt + 56, image_size)
    result[table + (count - 1) * 40:table + count * 40] = bytes(40)
    for site, original in HOOKS:
        result[site - IMAGE_BASE:site - IMAGE_BASE + len(original)] = original
    struct.pack_into("<I", result, opt + 64, original_checksum)
    return bytes(result)


def emit_include() -> str:
    lines = ["/* Generated by tools/build-ui-exe-patch.py from ui_hook.asm. */",
             "#ifndef MTW_UI_HOOK_INC", "#define MTW_UI_HOOK_INC"]
    for image_size in IMAGE_SIZES:
        hook = assemble_hook(image_size)
        label = f"b4{image_size >> 12 & 0xF:x}000"
        lines += [f"#define MTW_UI_HOOK_{label.upper()}_LENGTH {len(hook)}u",
                  f"static const unsigned char mtw_ui_hook_{label}[] = {{"]
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
    target = add_ui(source, args.checksum_style)
    checksum = struct.unpack_from("<I", source, 0xD8)[0]
    if strip_ui(target, checksum, args.checksum_style) != source:
        raise AssertionError("UI executable transform is not reversible")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(target)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({"schema": "mtw-ui-exe-v1",
                                         "source_sha256": sha256(source),
                                         "patched_sha256": sha256(target),
                                         "source_length": len(source),
                                         "patched_length": len(target),
                                         "section": ".mtwui"}, indent=2) + "\n", encoding="utf-8")
    args.inc.parent.mkdir(parents=True, exist_ok=True)
    args.inc.write_text(emit_include(), encoding="ascii")
    print(sha256(target))


if __name__ == "__main__":
    main()
