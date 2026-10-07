"""Build and inspect exact, reversible Sprite-only and Scroll+Sprite PE32 states.

Complete game executables are written only to a task-owned output directory.
The repository stores this recipe, assembly, hashes and the small hook include.
"""

import argparse
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
SCROLL_SPEC = importlib.util.spec_from_file_location(
    "scroll_exe_patch", ROOT / "tools" / "build-scroll-exe-patch.py")
SCROLL = importlib.util.module_from_spec(SCROLL_SPEC)
SCROLL_SPEC.loader.exec_module(SCROLL)

STOCK_SHA256 = SCROLL.STOCK_SHA256.upper()
SCROLL_SHA256 = "50829CD084355D81EC94D6F4489D1F60E2EF7FA92983D0EAD07D43832DEEF15B"
BASE = 0x400000
ENTRY = 0x73D47E
BODY = 0xF43389
ENTRY_RAW = ENTRY - BASE
BODY_RAW = 0x4DE389
ENTRY_BYTES = bytes.fromhex("E9 06 5F 80 00")
BODY_BYTES = bytes.fromhex("55 8B EC 83 7D 18 00")
LAYOUTS = {
    0: {"sections": 6, "rva": 0xB4C000, "raw": 0x4E7000, "size": 0x4E7000},
    1: {"sections": 8, "rva": 0xB4E000, "raw": 0x4E9000, "size": 0x4E9000},
}


def sha256(data):
    return hashlib.sha256(data).hexdigest().upper()


def pe_checksum(data, offset):
    return SCROLL._checksum(data, offset)


def _nasm():
    path = os.environ.get("MTW_NASM") or shutil.which("nasm")
    if path:
        return path
    wrapper = Path(r"F:\AI_Projects\Tools\nasm.cmd")
    if wrapper.exists():
        return str(wrapper)
    raise RuntimeError("NASM is required for the exact x86 hook")


def assemble_hook(va):
    if va not in (0xF4C000, 0xF4E000):
        raise ValueError("unsupported hook VA")
    with tempfile.TemporaryDirectory(prefix="mtw-sprite-asm-") as temp:
        target = Path(temp) / "sprite_hook.bin"
        command = [_nasm(), "-f", "bin", f"-DHOOK_VA=0x{va:08X}",
                   str(ROOT / "vendor" / "sprite_exe" / "sprite_hook.asm"),
                   "-o", str(target)]
        run = subprocess.run(command, capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(f"NASM failed: {run.stdout}\n{run.stderr}")
        hook = target.read_bytes()
    if not 20 <= len(hook) <= 0x1000:
        raise ValueError("unexpected hook length")
    return hook


def _pe_header(data, expected_count):
    if data[:2] != b"MZ":
        raise ValueError("not a PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    opt = pe + 24
    table = opt + struct.unpack_from("<H", data, pe + 20)[0]
    if (struct.unpack_from("<H", data, pe + 4)[0] != 0x14C or
            struct.unpack_from("<H", data, pe + 6)[0] != expected_count or
            struct.unpack_from("<H", data, opt)[0] != 0x10B or
            struct.unpack_from("<I", data, opt + 28)[0] != BASE or
            struct.unpack_from("<II", data, opt + 32) != (0x1000, 0x1000) or
            struct.unpack_from("<I", data, opt + 60)[0] != 0x1000 or
            table + (expected_count + 1) * 40 > 0x1000):
        raise ValueError("unsupported PE32 layout")
    return pe, opt, table


def _section(hook, rva, raw):
    return struct.pack("<8sIIIIIIHHI", b".mtwspr\0", len(hook), rva,
                       0x1000, raw, 0, 0, 0, 0, 0x60000020)


def _entry_jump(va):
    return b"\xE9" + struct.pack("<i", va - (ENTRY + 5))


def patch_sprite(base_data, scroll, hook=None):
    layout = LAYOUTS[scroll]
    expected_hash = SCROLL_SHA256 if scroll else STOCK_SHA256
    if len(base_data) != layout["size"] or sha256(base_data) != expected_hash:
        raise ValueError("source is not the exact supported stock/scroll EXE")
    pe, opt, table = _pe_header(base_data, layout["sections"])
    section = table + layout["sections"] * 40
    if (base_data[ENTRY_RAW:ENTRY_RAW + 5] != ENTRY_BYTES or
            base_data[BODY_RAW:BODY_RAW + len(BODY_BYTES)] != BODY_BYTES or
            base_data[section:section + 40] != b"\0" * 40 or
            struct.unpack_from("<I", base_data, opt + 56)[0] != layout["rva"]):
        raise ValueError("blitter entry/body or PE section slot changed")
    va = BASE + layout["rva"]
    hook = assemble_hook(va) if hook is None else hook
    if len(hook) > 0x1000:
        raise ValueError("sprite hook exceeds section")
    patched = bytearray(base_data + hook.ljust(0x1000, b"\0"))
    struct.pack_into("<H", patched, pe + 6, layout["sections"] + 1)
    struct.pack_into("<I", patched, opt + 4,
                     struct.unpack_from("<I", patched, opt + 4)[0] + 0x1000)
    struct.pack_into("<I", patched, opt + 56, layout["rva"] + 0x1000)
    patched[section:section + 40] = _section(hook, layout["rva"], layout["raw"])
    patched[ENTRY_RAW:ENTRY_RAW + 5] = _entry_jump(va)
    struct.pack_into("<I", patched, opt + 64, 0)
    struct.pack_into("<I", patched, opt + 64, pe_checksum(patched, opt + 64))
    return bytes(patched)


def restore_sprite(data, scroll):
    layout = LAYOUTS[scroll]
    hook = assemble_hook(BASE + layout["rva"])
    if (len(data) != layout["size"] + 0x1000 or
            data[layout["raw"]:] != hook.ljust(0x1000, b"\0")):
        raise ValueError("not the complete sprite patch")
    pe, opt, table = _pe_header(data, layout["sections"] + 1)
    section = table + layout["sections"] * 40
    if (data[ENTRY_RAW:ENTRY_RAW + 5] != _entry_jump(BASE + layout["rva"]) or
            data[section:section + 40] != _section(hook, layout["rva"], layout["raw"]) or
            struct.unpack_from("<I", data, opt + 56)[0] != layout["rva"] + 0x1000 or
            struct.unpack_from("<I", data, opt + 64)[0] != pe_checksum(data, opt + 64)):
        raise ValueError("partial/foreign sprite patch")
    restored = bytearray(data[:layout["size"]])
    struct.pack_into("<H", restored, pe + 6, layout["sections"])
    struct.pack_into("<I", restored, opt + 4,
                     struct.unpack_from("<I", restored, opt + 4)[0] - 0x1000)
    struct.pack_into("<I", restored, opt + 56, layout["rva"])
    restored[section:section + 40] = b"\0" * 40
    restored[ENTRY_RAW:ENTRY_RAW + 5] = ENTRY_BYTES
    struct.pack_into("<I", restored, opt + 64, SCROLL.STOCK_CHECKSUM if not scroll else 0)
    if scroll:
        struct.pack_into("<I", restored, opt + 64, pe_checksum(restored, opt + 64))
    expected_hash = SCROLL_SHA256 if scroll else STOCK_SHA256
    if sha256(restored) != expected_hash:
        raise ValueError("sprite patch did not restore an exact supported base")
    return bytes(restored)


def _scroll_hook():
    return SCROLL.assemble_hook(ROOT / "vendor" / "scroll_exe" / "scroll_hook.asm")


def build_variants(stock):
    if len(stock) != SCROLL.STOCK_SIZE or sha256(stock) != STOCK_SHA256:
        raise ValueError("stock EXE hash does not match")
    scroll = SCROLL.patch_exe(stock, _scroll_hook())
    if sha256(scroll) != SCROLL_SHA256:
        raise ValueError("scroll-only identity changed")
    variants = {(0, 0): stock, (1, 0): scroll,
                (0, 1): patch_sprite(stock, 0),
                (1, 1): patch_sprite(scroll, 1)}
    for key, data in variants.items():
        if key[1] and restore_sprite(data, key[0]) != variants[(key[0], 0)]:
            raise AssertionError("sprite roundtrip mismatch")
    return variants


def transform(source, scroll, sprite):
    if scroll not in (0, 1) or sprite not in (0, 1):
        raise ValueError("selection must be Boolean")
    digest = sha256(source)
    if digest == STOCK_SHA256 and len(source) == SCROLL.STOCK_SIZE:
        stock = source
    elif digest == SCROLL_SHA256 and len(source) == SCROLL.NEW_SIZE:
        stock = SCROLL.restore_exe(source, _scroll_hook())
    else:
        stock = None
        for current_scroll in (0, 1):
            try:
                base = restore_sprite(source, current_scroll)
                stock = (SCROLL.restore_exe(base, _scroll_hook())
                         if current_scroll else base)
                break
            except ValueError:
                pass
        if stock is None:
            raise ValueError("unknown or damaged direct EXE state")
    return build_variants(stock)[(scroll, sprite)]


def _include(variants):
    hooks = {0: assemble_hook(0xF4C000), 1: assemble_hook(0xF4E000)}
    lines = ["/* Generated from sprite_hook.asm by build-sprite-exe-patch.py. */",
             "#ifndef MTW_SPRITE_HOOK_INC", "#define MTW_SPRITE_HOOK_INC",
             f'#define MTW_SPRITE_ONLY_SHA256 "{sha256(variants[(0, 1)])}"',
             f'#define MTW_SCROLL_SPRITE_SHA256 "{sha256(variants[(1, 1)])}"']
    for scroll, label in ((0, "only"), (1, "combined")):
        hook = hooks[scroll]
        lines += [f"#define MTW_SPRITE_{label.upper()}_HOOK_LENGTH {len(hook)}u",
                  f"static const unsigned char mtw_sprite_{label}_hook_bytes[] = {{"]
        for start in range(0, len(hook), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in hook[start:start + 16]) + ",")
        lines.append("};")
    lines += ["#endif", ""]
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--stock", type=Path, required=True)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--inc", type=Path, required=True)
    args = p.parse_args()
    variants = build_variants(args.stock.read_bytes())
    if args.output_dir.resolve().drive.upper() != "G:":
        raise ValueError("complete game executables may be generated only on G:")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    names = {(0, 0): "stock", (1, 0): "scroll-only",
             (0, 1): "sprite-only", (1, 1): "scroll-and-sprite"}
    for state, name in names.items():
        (args.output_dir / f"{name}.exe").write_bytes(variants[state])
    manifest = {"schema": "mtw-direct-sprite-exe-v1",
                "source_sha256": STOCK_SHA256,
                "entry_va": f"0x{ENTRY:08X}", "original_body_va": f"0x{BODY:08X}",
                "variants": {names[key]: {"sha256": sha256(data), "size": len(data)}
                             for key, data in variants.items()},
                "hooks": {name: {"sha256": sha256(assemble_hook(BASE + LAYOUTS[scroll]["rva"])),
                                  "size": len(assemble_hook(BASE + LAYOUTS[scroll]["rva"])),
                                  "va": f"0x{BASE + LAYOUTS[scroll]['rva']:08X}"}
                          for scroll, name in ((0, "sprite-only"), (1, "combined"))}}
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    args.inc.parent.mkdir(parents=True, exist_ok=True)
    args.inc.write_text(_include(variants), encoding="ascii")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
