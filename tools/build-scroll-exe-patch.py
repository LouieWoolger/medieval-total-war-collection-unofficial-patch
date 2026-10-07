"""Build/verify the reversible direct camera patch for one pinned MTW PE32 EXE.

The repository contains patch code and offsets, never a complete game EXE.
Provide a task-owned stock executable with --stock and put --output on G:.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


STOCK_SHA256 = "23724b034f8c97094cecd5560f053864a475a88adad077c046b2beb79331ace5"
STOCK_SIZE = 0x4E7000
STOCK_CHECKSUM = 0x4F65E7
IMAGE_BASE = 0x400000
PAN_ENTRY = 0x627DD0
PAN_PROLOGUE = bytes.fromhex("55 8B EC 83 EC 08")
HOOK_VA = 0xF4C000
CODE_RVA = 0xB4C000
DATA_RVA = 0xB4D000
CODE_RAW = STOCK_SIZE
DATA_RAW = STOCK_SIZE + 0x1000
NEW_SIZE = STOCK_SIZE + 0x2000
CALLSITES = (
    0x6269F2, 0x626A15, 0x626A31, 0x626A54,
    0x626BC1, 0x626BD8, 0x626BEF, 0x626C06,
    0x62E9F0, 0x62E9FF, 0x62EA1C, 0x62EA2F,
)
CALL_BYTES = (
    "e8d9130000", "e8b6130000", "e89a130000", "e877130000",
    "e80a120000", "e8f3110000", "e8dc110000", "e8c5110000",
    "e8db93ffff", "e8cc93ffff", "e8af93ffff", "e89c93ffff",
)


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def _layout(data, expected_sections):
    if data[:2] != b"MZ":
        raise ValueError("not a DOS/PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe+4] != b"PE\0\0":
        raise ValueError("not a PE image")
    sections, machine = struct.unpack_from("<HH", data, pe+6)[0], struct.unpack_from("<H", data, pe+4)[0]
    opt_size = struct.unpack_from("<H", data, pe+20)[0]
    opt = pe + 24
    table = opt + opt_size
    if (sections != expected_sections or machine != 0x14C or
            struct.unpack_from("<H", data, opt)[0] != 0x10B or
            struct.unpack_from("<I", data, opt+28)[0] != IMAGE_BASE or
            struct.unpack_from("<II", data, opt+32) != (0x1000, 0x1000) or
            struct.unpack_from("<I", data, opt+60)[0] != 0x1000 or
            table + 8*40 > 0x1000):
        raise ValueError("unsupported PE32 layout")
    return pe, opt, table


def _section(name, vsize, rva, raw):
    return struct.pack("<8sIIIIIIHHI", name, vsize, rva, 0x1000, raw,
                       0, 0, 0, 0,
                       0x60000020 if rva == CODE_RVA else 0xC0000040)


def _checksum(data, offset):
    total = 0
    for i in range(0, len(data), 4):
        if i == offset:
            continue
        word = int.from_bytes(data[i:i+4].ljust(4, b"\0"), "little")
        total += word
        total = (total & 0xFFFFFFFF) + (total >> 32)
    total = (total & 0xFFFF) + (total >> 16)
    total += total >> 16
    return (total & 0xFFFF) + len(data)


def _nasm_path():
    explicit = os.environ.get("MTW_NASM")
    if explicit:
        return explicit
    found = shutil.which("nasm")
    if found:
        return found
    local = Path(r"F:\AI_Projects\Tools\nasm.cmd")
    if local.exists():
        return str(local)
    raise RuntimeError("NASM missing; set MTW_NASM to the verified x86 assembler route")


def assemble_hook(source):
    with tempfile.TemporaryDirectory(prefix="mtw-pan-asm-") as directory:
        output = Path(directory) / "scroll_hook.bin"
        result = subprocess.run([_nasm_path(), "-f", "bin", str(source),
                                 "-o", str(output)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"NASM failed: {result.stdout}\n{result.stderr}")
        blob = output.read_bytes()
    if not 100 <= len(blob) <= 0x1000:
        raise ValueError("unexpected hook size")
    return blob


def _entry_jump():
    return b"\xe9" + struct.pack("<i", HOOK_VA - (PAN_ENTRY + 5)) + b"\x90"


def _mutate_header(data, opt, table, hook, forward):
    if forward:
        struct.pack_into("<I", data, opt+4,
                         struct.unpack_from("<I", data, opt+4)[0] + 0x1000)
        struct.pack_into("<I", data, opt+8,
                         struct.unpack_from("<I", data, opt+8)[0] + 0x1000)
        struct.pack_into("<I", data, opt+56, 0xB4E000)
        data[table+6*40:table+7*40] = _section(b".mtwpan", len(hook), CODE_RVA, CODE_RAW)
        data[table+7*40:table+8*40] = _section(b".mtwdat", 0x1000, DATA_RVA, DATA_RAW)
    else:
        struct.pack_into("<I", data, opt+4,
                         struct.unpack_from("<I", data, opt+4)[0] - 0x1000)
        struct.pack_into("<I", data, opt+8,
                         struct.unpack_from("<I", data, opt+8)[0] - 0x1000)
        struct.pack_into("<I", data, opt+56, 0xB4C000)
        data[table+6*40:table+8*40] = b"\0" * 80


def patch_exe(stock, hook):
    if len(stock) != STOCK_SIZE or _hash(stock) != STOCK_SHA256:
        raise ValueError("EXE does not match the one supported stock SHA-256")
    pe, opt, table = _layout(stock, 6)
    if (stock[PAN_ENTRY-IMAGE_BASE:PAN_ENTRY-IMAGE_BASE+6] != PAN_PROLOGUE or
            stock[table+6*40:table+8*40] != b"\0"*80 or
            struct.unpack_from("<I", stock, opt+56)[0] != 0xB4C000 or
            struct.unpack_from("<I", stock, opt+64)[0] != STOCK_CHECKSUM or
            any(stock[va-IMAGE_BASE:va-IMAGE_BASE+5] != bytes.fromhex(want)
                for va, want in zip(CALLSITES, CALL_BYTES))):
        raise ValueError("pinned pan instructions or PE layout changed")
    if len(hook) > 0x1000:
        raise ValueError("hook exceeds code section")
    patched = bytearray(stock + hook.ljust(0x1000, b"\0") + b"\0"*0x1000)
    struct.pack_into("<H", patched, pe+6, 8)
    _mutate_header(patched, opt, table, hook, True)
    patched[PAN_ENTRY-IMAGE_BASE:PAN_ENTRY-IMAGE_BASE+6] = _entry_jump()
    struct.pack_into("<I", patched, opt+64, 0)
    struct.pack_into("<I", patched, opt+64, _checksum(patched, opt+64))
    return bytes(patched)


def restore_exe(patched, hook):
    if len(patched) != NEW_SIZE or patched[CODE_RAW:CODE_RAW+0x1000] != hook.ljust(0x1000, b"\0") or patched[DATA_RAW:] != b"\0"*0x1000:
        raise ValueError("not the complete pinned camera patch")
    pe, opt, table = _layout(patched, 8)
    if (patched[PAN_ENTRY-IMAGE_BASE:PAN_ENTRY-IMAGE_BASE+6] != _entry_jump() or
            patched[table+6*40:table+7*40] != _section(b".mtwpan", len(hook), CODE_RVA, CODE_RAW) or
            patched[table+7*40:table+8*40] != _section(b".mtwdat", 0x1000, DATA_RVA, DATA_RAW) or
            struct.unpack_from("<I", patched, opt+56)[0] != 0xB4E000 or
            struct.unpack_from("<I", patched, opt+64)[0] != _checksum(patched, opt+64)):
        raise ValueError("partial or foreign camera patch")
    stock = bytearray(patched[:STOCK_SIZE])
    struct.pack_into("<H", stock, pe+6, 6)
    _mutate_header(stock, opt, table, hook, False)
    stock[PAN_ENTRY-IMAGE_BASE:PAN_ENTRY-IMAGE_BASE+6] = PAN_PROLOGUE
    struct.pack_into("<I", stock, opt+64, STOCK_CHECKSUM)
    if _hash(stock) != STOCK_SHA256:
        raise ValueError("patched image does not restore to the pinned stock EXE")
    return bytes(stock)


def verify_patched(patched, hook):
    try:
        restore_exe(patched, hook)
        return True
    except ValueError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--inc", type=Path,
                        help="Emit the small assembled hook as a reviewable C byte include")
    args = parser.parse_args()
    hook = assemble_hook(Path(__file__).resolve().parents[1] / "vendor" / "scroll_exe" / "scroll_hook.asm")
    stock = args.stock.read_bytes()
    patched = patch_exe(stock, hook)
    if restore_exe(patched, hook) != stock:
        raise AssertionError("roundtrip mismatch")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(patched)
    manifest = {"source_sha256": STOCK_SHA256, "patched_sha256": _hash(patched),
                "hook_sha256": _hash(hook), "hook_length": len(hook),
                "source_size": len(stock), "patched_size": len(patched),
                "entry_va": hex(PAN_ENTRY), "hook_va": hex(HOOK_VA),
                "code_section_rva": hex(CODE_RVA), "data_section_rva": hex(DATA_RVA),
                "callsite_count": len(CALLSITES)}
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    if args.inc:
        lines = ["/* Generated by tools/build-scroll-exe-patch.py from scroll_hook.asm. */",
                 "#ifndef MTW_SCROLL_HOOK_INC", "#define MTW_SCROLL_HOOK_INC",
                 f"#define MTW_SCROLL_HOOK_LENGTH {len(hook)}u",
                 f"#define MTW_SCROLL_PATCHED_SHA256 \"{_hash(patched).upper()}\"",
                 "static const unsigned char mtw_scroll_hook_bytes[] = {"]
        for start in range(0, len(hook), 16):
            lines.append("    " + ", ".join(f"0x{value:02X}" for value in hook[start:start+16]) + ",")
        lines += ["};", "#endif"]
        args.inc.parent.mkdir(parents=True, exist_ok=True)
        args.inc.write_text("\n".join(lines)+"\n", encoding="ascii")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
