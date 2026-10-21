"""Reversible key helper across the pinned executable composition states."""
from __future__ import annotations

import hashlib
import importlib.util
import os
from pathlib import Path
import struct

import pytest
import pefile
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc
from unicorn.x86_const import (UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EIP,
                               UC_X86_REG_ESI, UC_X86_REG_ESP)


ROOT = Path(__file__).resolve().parents[1]
STOCK_SHA256 = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def source_states():
    name = os.environ.get("MTW_STOCK_EXE")
    if not name:
        pytest.skip("MTW_STOCK_EXE must point to a verified staged stock EXE")
    stock = Path(name).read_bytes()
    assert hashlib.sha256(stock).hexdigest().upper() == STOCK_SHA256
    generator = load(ROOT / "tools" / "generate-laa-exe-variants.py", "key_variants")
    return generator.build_economy_variants(stock)


def test_key_helper_round_trips_every_prior_exact_state(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_builder")
    assert len(source_states) == 128
    identities = set()
    for (_, _, _, style, _, _), original in source_states.items():
        checksum = struct.unpack_from("<I", original, 0xD8)[0]
        patched = key.add_key(original, style)
        assert key.strip_key(patched, checksum, style) == original
        assert len(patched) == len(original) + key.PAGE
        assert patched[key.HELD_SITE - key.IMAGE_BASE:
                       key.HELD_SITE - key.IMAGE_BASE + 5] == key.jump_bytes(
                           key.HELD_SITE, key.IMAGE_BASE + key.image_size(original))
        assert patched[key.GHOST_SITE - key.IMAGE_BASE:
                       key.GHOST_SITE - key.IMAGE_BASE + 5] == key.jump_bytes(
                           key.GHOST_SITE,
                           key.IMAGE_BASE + key.image_size(original) + key.GHOST_OFFSET)
        assert patched[key.SPECIAL_SITE - key.IMAGE_BASE:
                       key.SPECIAL_SITE - key.IMAGE_BASE + key.SPECIAL_LENGTH] == key.special_bytes()
        identities.add(hashlib.sha256(patched).digest())
    assert len(identities) == 128


def test_backspace_candidate_keeps_original_reassignment_code(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_original_conflicts")
    original = source_states[(0, 0, 0, "base", 0, 0)]
    patched = key.add_key(original, "base")
    assert patched[key.BACKSPACE_FLAG_RAW] == 1
    assert patched[key.COLLISION_SITE - key.IMAGE_BASE:
                   key.COLLISION_SITE - key.IMAGE_BASE + len(key.ORIGINAL_COLLISION)] == key.ORIGINAL_COLLISION


def test_space_assignment_patch_is_reversible_and_rejects_partial_removal(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_space_reversible")
    original = source_states[(0, 0, 0, "base", 0, 0)]
    checksum = struct.unpack_from("<I", original, 0xD8)[0]
    patched = key.add_key(original, "base")
    space_flag = 0x391551
    assert original[space_flag] == 0
    assert patched[space_flag] == 1
    assert key.strip_key(patched, checksum, "base") == original
    incomplete = bytearray(patched)
    incomplete[space_flag] = 0
    with pytest.raises(ValueError):
        key.strip_key(bytes(incomplete), checksum, "base")


def test_expanded_capture_and_punctuation_labels_are_reversible(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_expanded_controls")
    original = source_states[(0, 0, 0, "base", 0, 0)]
    checksum = struct.unpack_from("<I", original, 0xD8)[0]
    patched = key.add_key(original, "base")
    for offset in key.EXTRA_FLAG_RAWS:
        assert original[offset] == 0
        assert patched[offset] == 1
    site = key.LABEL_SITE - key.IMAGE_BASE
    assert original[site:site + len(key.ORIGINAL_LABEL)] == key.ORIGINAL_LABEL
    assert patched[site:site + 6] == (
        key.call_bytes(key.LABEL_SITE,
                       key.IMAGE_BASE + key.image_size(original) + key.LABEL_OFFSET)
        + b"\x90")
    assert key.strip_key(patched, checksum, "base") == original
    for offset in (key.EXTRA_FLAG_RAWS[0], site):
        incomplete = bytearray(patched)
        incomplete[offset] ^= 1
        with pytest.raises(ValueError):
            key.strip_key(bytes(incomplete), checksum, "base")


def test_custom_delete_can_take_the_battle_dispatch_path_without_losing_rollback(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_delete_dispatch")
    original = source_states[(0, 0, 0, "base", 0, 0)]
    checksum = struct.unpack_from("<I", original, 0xD8)[0]
    patched = key.add_key(original, "base")
    # Both native Delete reservations must accept an assignment through Controls.
    for slot in (68, 106):
        assert original[0x39152C + slot - 63] == 0
        assert patched[0x39152C + slot - 63] == 1
    # The battle dispatcher must enter the key section. Its unmodified
    # fixed-first entry would still invoke fixed Delete handlers afterward.
    site = 0x4C8E56 - key.IMAGE_BASE
    target = key.IMAGE_BASE + key.image_size(original) + 0x400
    assert patched[site:site + 5] == key.jump_bytes(0x4C8E56, target)
    assert key.strip_key(patched, checksum, "base") == original
    for slot in (68, 106):
        incomplete = bytearray(patched)
        incomplete[0x39152C + slot - 63] = 0
        with pytest.raises(ValueError):
            key.strip_key(bytes(incomplete), checksum, "base")


def test_battle_dispatch_preserves_each_active_native_control_layout(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_layout_dispatch")
    stock = source_states[(0, 0, 0, "base", 0, 0)]
    patched = key.add_key(stock, "base")
    pe = pefile.PE(data=stock, fast_load=True)
    defaults = struct.unpack("<5I", pe.get_data(0x4957DC, 20))

    def slots(bindings: list[int], event: int, layout: int | None) -> list[int]:
        game = pefile.PE(data=patched, fast_load=True)
        cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        for address, size in ((0x4C0000, 0x10000), (0x890000, 0x10000),
                              (0x950000, 0x10000), (0x960000, 0x10000),
                              (0xE70000, 0x10000), (0x300000, 0x10000),
                              (0xF4C000, 0x1000)):
            cpu.mem_map(address, size)
        cpu.mem_write(0x4C0000, game.get_data(0xC0000, 0x10000))
        cpu.mem_write(0x890000, game.get_data(0x490000, 0x10000))
        cpu.mem_write(0xF4C000, game.get_data(0xB4C000, 0x1000))
        if layout is not None:
            cpu.mem_write(0x95E7C0, struct.pack("<I", 0x960100))
            cpu.mem_write(0x96010C, struct.pack("<I", layout))
        cpu.mem_write(0xE78174, struct.pack("<113I", *bindings))
        cpu.reg_write(UC_X86_REG_EDI, event)
        cpu.reg_write(UC_X86_REG_EBP, 0x300800)
        cpu.reg_write(UC_X86_REG_ESP, 0x300F00)
        seen = []

        def matched(uc, address, size, unused):
            if address == 0x4C8E70:
                seen.append(uc.reg_read(UC_X86_REG_ESI))
                uc.reg_write(UC_X86_REG_EIP, 0x4C8EDC)
            elif address == 0x4C8EF1:
                uc.emu_stop()

        cpu.hook_add(UC_HOOK_CODE, matched)
        cpu.emu_start(0x4C8E56, 0x4C8EF2, count=10000)
        return seen

    layout_three = list(struct.unpack("<113I", pe.get_data(defaults[3] - 0x400000, 452)))
    assert layout_three[39] == layout_three[66] == 0x4F
    assert slots(layout_three, 0x4F, 3) == [66, 39]
    default = list(struct.unpack("<113I", pe.get_data(defaults[0] - 0x400000, 452)))
    custom = default.copy()
    custom[0] = 0x2E
    assert slots(custom, 0x2E, 0) == [0]
    assert slots(custom, 0x2E, None) == [106, 68, 0]


def test_key_helper_rejects_partial_or_mutated_state(source_states):
    key = load(ROOT / "tools" / "build-key-exe-patch.py", "key_builder_mutated")
    original = source_states[(1, 1, 1, "base", 1, 1)]
    checksum = struct.unpack_from("<I", original, 0xD8)[0]
    patched = key.add_key(original, "base")
    for offset in (key.BACKSPACE_FLAG_RAW, key.SPACE_FLAG_RAW,
                   key.EXTRA_FLAG_RAWS[-1], key.LABEL_SITE - key.IMAGE_BASE,
                   key.HELD_SITE - key.IMAGE_BASE,
                   key.GHOST_SITE - key.IMAGE_BASE,
                   key.SPECIAL_SITE - key.IMAGE_BASE,
                   len(patched) - 1, 0xD8):
        changed = bytearray(patched)
        changed[offset] ^= 1
        with pytest.raises(ValueError):
            key.strip_key(bytes(changed), checksum, "base")
