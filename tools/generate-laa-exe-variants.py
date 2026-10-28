"""Generate pinned LAA-only identities from an explicitly supplied stock EXE.

The output contains hashes and small header metadata, never game executable bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]
SPRITE_SPEC = importlib.util.spec_from_file_location(
    "mtw_sprite_exe_patch", ROOT / "tools" / "build-sprite-exe-patch.py")
SPRITE = importlib.util.module_from_spec(SPRITE_SPEC)
SPRITE_SPEC.loader.exec_module(SPRITE)
UI_SPEC = importlib.util.spec_from_file_location(
    "mtw_ui_exe_patch", ROOT / "tools" / "build-ui-exe-patch.py")
UI = importlib.util.module_from_spec(UI_SPEC)
UI_SPEC.loader.exec_module(UI)
ECONOMY_SPEC = importlib.util.spec_from_file_location(
    "mtw_economy_exe_patch", ROOT / "tools" / "build-economy-exe-patch.py")
ECONOMY = importlib.util.module_from_spec(ECONOMY_SPEC)
ECONOMY_SPEC.loader.exec_module(ECONOMY)
KEY_SPEC = importlib.util.spec_from_file_location(
    "mtw_key_exe_patch", ROOT / "tools" / "build-key-exe-patch.py")
KEY = importlib.util.module_from_spec(KEY_SPEC)
KEY_SPEC.loader.exec_module(KEY)
pe_checksum = SPRITE.pe_checksum
STYLES = ("base", "retain", "zero", "recompute")
STATES = ((0, 0), (1, 0), (0, 1), (1, 1))
TUTORIAL_RESET_OFFSET = 0x306A5C
TUTORIAL_RESET_ORIGINAL = bytes.fromhex("89 1D F0 D1 E7 00")
TUTORIAL_RESET_SKIP = bytes.fromhex("E9 78 00 00 00 90")


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def pe_fields(data: bytes) -> tuple[int, int, int, int]:
    if len(data) < 0x200 or data[:2] != b"MZ":
        raise ValueError("not the supported PE32 executable")
    nt = struct.unpack_from("<I", data, 0x3C)[0]
    if nt != 0x80 or data[nt:nt + 4] != b"PE\0\0":
        raise ValueError("unexpected PE header position or signature")
    if struct.unpack_from("<H", data, nt + 4)[0] != 0x14C or \
            struct.unpack_from("<H", data, nt + 24)[0] != 0x10B or \
            struct.unpack_from("<I", data, nt + 24 + 28)[0] != 0x400000:
        raise ValueError("unexpected PE32/i386 image identity")
    flags_offset, checksum_offset = nt + 22, nt + 24 + 64
    if flags_offset != 0x96 or checksum_offset != 0xD8:
        raise ValueError("unexpected LAA or checksum offset")
    return flags_offset, checksum_offset, struct.unpack_from("<I", data, nt + 24 + 56)[0], nt


def build_variants(stock: bytes) -> dict[tuple[int, int, int, str], bytes]:
    bases = SPRITE.build_variants(stock)
    result = {}
    for tutorial in (0, 1):
        for scroll, sprite in STATES:
            base = bases[(scroll, sprite)]
            flags_offset, checksum_offset, _, _ = pe_fields(base)
            flags = struct.unpack_from("<H", base, flags_offset)[0]
            if flags & 0x20:
                raise ValueError("canonical EXE unexpectedly has LAA")
            if base[TUTORIAL_RESET_OFFSET:TUTORIAL_RESET_OFFSET + 6] != TUTORIAL_RESET_ORIGINAL:
                raise ValueError("tutorial reset instruction is not the verified original")
            if tutorial:
                patched = bytearray(base)
                patched[TUTORIAL_RESET_OFFSET:TUTORIAL_RESET_OFFSET + 6] = TUTORIAL_RESET_SKIP
                struct.pack_into("<I", patched, checksum_offset, 0)
                struct.pack_into("<I", patched, checksum_offset,
                                 SPRITE.pe_checksum(patched, checksum_offset))
                base = bytes(patched)
            for style in STYLES:
                changed = bytearray(base)
                if style != "base":
                    struct.pack_into("<H", changed, flags_offset, flags | 0x20)
                    if style == "zero":
                        struct.pack_into("<I", changed, checksum_offset, 0)
                    elif style == "recompute":
                        struct.pack_into("<I", changed, checksum_offset,
                                         SPRITE.pe_checksum(changed, checksum_offset))
                    if {i for i, (a, b) in enumerate(zip(base, changed)) if a != b} - {
                            flags_offset, checksum_offset, checksum_offset + 1,
                            checksum_offset + 2, checksum_offset + 3}:
                        raise AssertionError("LAA variant changed more than the permitted PE header")
                result[(scroll, sprite, tutorial, style)] = bytes(changed)
    if len({sha256(data) for data in result.values()}) != 32:
        raise AssertionError("EXE variants did not have unique exact identities")
    return result


def build_records(stock: bytes) -> list[dict]:
    variants = build_key_variants(stock)
    result = []
    for tutorial in (0, 1):
        for scroll, sprite in STATES:
            for ui in (0, 1):
                for economy in (0, 1):
                    for key in (0, 1):
                        base = variants[(scroll, sprite, tutorial, "base", ui, economy, key)]
                        _, checksum_offset, _, _ = pe_fields(base)
                        canonical_checksum = struct.unpack_from("<I", base, checksum_offset)[0]
                        for style in STYLES:
                            data = variants[(scroll, sprite, tutorial, style, ui, economy, key)]
                            _, _, image_size, _ = pe_fields(data)
                            result.append({
                                "scroll": scroll, "sprite": sprite, "tutorial": tutorial,
                                "ui": ui, "economy": economy, "key": key, "style": style,
                                "key_previous": 0,
                                "laa": int(style != "base"), "length": len(data),
                                "image_size": image_size, "canonical_checksum": canonical_checksum,
                                "checksum": struct.unpack_from("<I", data, checksum_offset)[0],
                                "sha256": sha256(data),
                            })
    # Preserve every exact key-enabled identity shipped before the text-input
    # repair. They are accepted as inputs for repair/removal, never selected as
    # outputs of a new install.
    previous = build_key_variants(stock, previous=True)
    for tutorial in (0, 1):
        for scroll, sprite in STATES:
            for ui in (0, 1):
                for economy in (0, 1):
                    for style in STYLES:
                        data = previous[(scroll, sprite, tutorial, style, ui, economy, 1)]
                        _, checksum_offset, image_size, _ = pe_fields(data)
                        base = previous[(scroll, sprite, tutorial, "base", ui, economy, 1)]
                        result.append({
                            "scroll": scroll, "sprite": sprite, "tutorial": tutorial,
                            "ui": ui, "economy": economy, "key": 1, "style": style,
                            "key_previous": 1,
                            "laa": int(style != "base"), "length": len(data),
                            "image_size": image_size,
                            "canonical_checksum": struct.unpack_from("<I", base, checksum_offset)[0],
                            "checksum": struct.unpack_from("<I", data, checksum_offset)[0],
                            "sha256": sha256(data),
                        })
    if len(result) != 384 or len({rec["sha256"] for rec in result}) != 384:
        raise AssertionError("current and previous executable identities overlap")
    return result


def build_ui_variants(stock: bytes) -> dict[tuple[int, int, int, str, int], bytes]:
    """Extend all 32 legacy exact states with independently selectable R-06."""
    prior = build_variants(stock)
    result = {}
    for (scroll, sprite, tutorial, style), original in prior.items():
        corrected = UI.add_ui(original, style)
        if UI.strip_ui(corrected, struct.unpack_from("<I", original, 0xD8)[0], style) != original:
            raise AssertionError("UI transform did not restore its original exact state")
        result[(scroll, sprite, tutorial, style, 0)] = original
        result[(scroll, sprite, tutorial, style, 1)] = corrected
    if len({sha256(data) for data in result.values()}) != 64:
        raise AssertionError("UI executable variants did not have unique exact identities")
    return result


def build_economy_variants(stock: bytes) -> dict[tuple[int, int, int, str, int, int], bytes]:
    """Extend all 64 exact UI states with independently selectable E-02."""
    prior = build_ui_variants(stock)
    result = {}
    for (scroll, sprite, tutorial, style, ui), original in prior.items():
        corrected = ECONOMY.add_economy(original, style)
        if ECONOMY.strip_economy(corrected, struct.unpack_from("<I", original, 0xD8)[0],
                                 style) != original:
            raise AssertionError("economy transform did not restore its exact input")
        result[(scroll, sprite, tutorial, style, ui, 0)] = original
        result[(scroll, sprite, tutorial, style, ui, 1)] = corrected
    if len({sha256(data) for data in result.values()}) != 128:
        raise AssertionError("economy executable variants did not have unique identities")
    return result


def build_key_variants(stock: bytes, previous: bool = False) -> dict[tuple[int, int, int, str, int, int, int], bytes]:
    """Append optional K-01 after the existing 128 economy compositions."""
    prior = build_economy_variants(stock)
    result = {}
    for (*state, style, ui, economy), original in prior.items():
        corrected = KEY.add_key(original, style, previous=previous)
        if KEY.strip_key(corrected, struct.unpack_from("<I", original, 0xD8)[0],
                         style, previous=previous) != original:
            raise AssertionError("key transform did not restore its exact input")
        result[(*state, style, ui, economy, 0)] = original
        result[(*state, style, ui, economy, 1)] = corrected
    if len({sha256(data) for data in result.values()}) != 256:
        raise AssertionError("key executable variants did not have unique identities")
    return result


def emit_header(records: list[dict]) -> str:
    lines = [
        "/* Generated by tools/generate-laa-exe-variants.py from a verified stock EXE.",
        "   Full game executables are never included in this header. */",
        "#ifndef MTW_EXE_VARIANTS_H",
        "#define MTW_EXE_VARIANTS_H",
        "#include <stddef.h>",
        "#include <string.h>",
        "typedef struct MtwExeVariant {",
        "    const char *sha256;",
        "    size_t file_length;",
        "    unsigned int image_size;",
        "    unsigned int canonical_checksum;",
        "    unsigned char scroll;",
        "    unsigned char sprite;",
        "    unsigned char tutorial;",
        "    unsigned char ui;",
        "    unsigned char economy;",
        "    unsigned char key;",
        "    unsigned char key_previous;",
        "    unsigned char laa;",
        "    const char *checksum_style;",
        "} MtwExeVariant;",
        "static const MtwExeVariant mtw_exe_variants[] = {",
    ]
    for rec in records:
        lines.append(
            f'    {{"{rec["sha256"]}", {rec["length"]}u, '
            f'0x{rec["image_size"]:08X}u, 0x{rec["canonical_checksum"]:08X}u, '
            f'{rec["scroll"]}u, {rec["sprite"]}u, {rec["tutorial"]}u, '
            f'{rec["ui"]}u, {rec["economy"]}u, {rec["key"]}u, '
            f'{rec["key_previous"]}u, {rec["laa"]}u, "{rec["style"]}"}},')
    lines += [
        "};",
        "static inline int mtw_exe_variant_from_hash(const char *sha256_hex,",
        "                                             size_t file_length,",
        "                                             MtwExeVariant *out) {",
        "    size_t i;",
        "    if (!sha256_hex || !out || strlen(sha256_hex) != 64u) return 0;",
        "    for (i = 0u; i < sizeof(mtw_exe_variants) / sizeof(mtw_exe_variants[0]); ++i) {",
        "        const MtwExeVariant *v = &mtw_exe_variants[i];",
        "        if (v->file_length == file_length && !strcmp(v->sha256, sha256_hex)) {",
        "            *out = *v;",
        "            return 1;",
        "        }",
        "    }",
        "    return 0;",
        "}",
        "static inline const MtwExeVariant *mtw_exe_variant_for_options(",
        "    unsigned int scroll, unsigned int sprite, unsigned int tutorial,",
        "    unsigned int laa, const char *style) {",
        "    size_t i;",
        "    if (!style) return NULL;",
        "    for (i = 0u; i < sizeof(mtw_exe_variants) / sizeof(mtw_exe_variants[0]); ++i) {",
        "        const MtwExeVariant *v = &mtw_exe_variants[i];",
        "        if (v->scroll == scroll && v->sprite == sprite &&",
        "            v->tutorial == tutorial && !v->ui && !v->economy && !v->key && v->laa == laa &&",
        "            !strcmp(v->checksum_style, style)) return v;",
        "    }",
        "    return NULL;",
        "}",
        "static inline const MtwExeVariant *mtw_exe_variant_for_state(",
        "    unsigned int scroll, unsigned int sprite, unsigned int laa, const char *style) {",
        "    return mtw_exe_variant_for_options(scroll, sprite, 0u, laa, style);",
        "}",
        "static inline const MtwExeVariant *mtw_exe_variant_for_options_ui(",
        "    unsigned int scroll, unsigned int sprite, unsigned int tutorial,",
        "    unsigned int laa, const char *style, unsigned int ui) {",
        "    size_t i;",
        "    if (!style || ui > 1u) return NULL;",
        "    for (i = 0u; i < sizeof(mtw_exe_variants) / sizeof(mtw_exe_variants[0]); ++i) {",
        "        const MtwExeVariant *v = &mtw_exe_variants[i];",
        "        if (v->scroll == scroll && v->sprite == sprite &&",
        "            v->tutorial == tutorial && v->laa == laa && v->ui == ui && !v->economy && !v->key &&",
        "            !strcmp(v->checksum_style, style)) return v;",
        "    }",
        "    return NULL;",
        "}",
        "static inline const MtwExeVariant *mtw_exe_variant_for_options_economy(",
        "    unsigned int scroll, unsigned int sprite, unsigned int tutorial,",
        "    unsigned int laa, const char *style, unsigned int ui, unsigned int economy) {",
        "    size_t i;",
        "    if (!style || ui > 1u || economy > 1u) return NULL;",
        "    for (i = 0u; i < sizeof(mtw_exe_variants) / sizeof(mtw_exe_variants[0]); ++i) {",
        "        const MtwExeVariant *v = &mtw_exe_variants[i];",
        "        if (v->scroll == scroll && v->sprite == sprite &&",
        "            v->tutorial == tutorial && v->laa == laa && v->ui == ui &&",
        "            v->economy == economy && !v->key && !strcmp(v->checksum_style, style)) return v;",
        "    }",
        "    return NULL;",
        "}",
        "static inline const MtwExeVariant *mtw_exe_variant_for_options_key(",
        "    unsigned int scroll, unsigned int sprite, unsigned int tutorial,",
        "    unsigned int laa, const char *style, unsigned int ui, unsigned int economy, unsigned int key) {",
        "    size_t i;",
        "    if (!style || ui > 1u || economy > 1u || key > 1u) return NULL;",
        "    for (i = 0u; i < sizeof(mtw_exe_variants) / sizeof(mtw_exe_variants[0]); ++i) {",
        "        const MtwExeVariant *v = &mtw_exe_variants[i];",
        "        if (v->scroll == scroll && v->sprite == sprite &&",
        "            v->tutorial == tutorial && v->laa == laa && v->ui == ui &&",
        "            v->economy == economy && v->key == key && !v->key_previous &&",
        "            !strcmp(v->checksum_style, style)) return v;",
        "    }",
        "    return NULL;",
        "}",
        "#endif",
    ]
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    stock = args.stock.read_bytes()
    records = build_records(stock)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(emit_header(records), encoding="ascii", newline="\n")
    args.manifest.write_text(json.dumps({"schema": "mtw-laa-ui-economy-key-exe-variants-v5",
                                         "stock_sha256": sha256(stock),
                                         "variants": records}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
