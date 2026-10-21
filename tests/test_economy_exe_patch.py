"""Exact PE32 economy section, reversibility, and native option composition."""
from __future__ import annotations

import faulthandler
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess

import pytest
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI,
                               UC_X86_REG_ESI, UC_X86_REG_ESP)


ROOT = Path(__file__).resolve().parents[1]
STOCK_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def generated():
    source = os.environ.get("MTW_STOCK_EXE")
    if not source:
        pytest.skip("MTW_STOCK_EXE must name a verified staged stock EXE")
    stock = Path(source).read_bytes()
    assert digest(stock) == STOCK_HASH
    generator = load(ROOT / "tools" / "generate-laa-exe-variants.py", "economy_identity_generator")
    economy = load(ROOT / "tools" / "build-economy-exe-patch.py", "economy_section_builder")
    return stock, generator.build_ui_variants(stock), economy


@pytest.fixture(scope="module")
def native_transform(tmp_path_factory):
    compiler = os.environ.get("MTW_CC")
    if not compiler:
        pytest.skip("MTW_CC must name the i686 compiler")
    target = tmp_path_factory.mktemp("native-economy") / "transform.exe"
    compiler_bin = Path(compiler).parent
    env = dict(os.environ, PATH=str(compiler_bin) + os.pathsep + os.environ["PATH"])
    build = subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests" / "native_economy_exe.c"), "-o", str(target),
                            "-ladvapi32"], cwd=ROOT, env=env,
                           capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stderr
    return target


def transform(binary: Path, source: bytes, options: tuple[int, int, int, int, int],
              folder: Path) -> tuple[subprocess.CompletedProcess, bytes | None]:
    original = folder / "source.exe"
    output = folder / "output.exe"
    original.write_bytes(source)
    output.unlink(missing_ok=True)
    run = subprocess.run([str(binary), str(original), *(str(bit) for bit in options),
                          str(output)], capture_output=True, text=True, timeout=20)
    return run, output.read_bytes() if output.exists() else None


def test_builder_restores_every_existing_exact_state(generated):
    _, prior, economy = generated
    assert len(prior) == 64
    for (scroll, sprite, tutorial, style, ui), original in prior.items():
        corrected = economy.add_economy(original, style)
        checksum = struct.unpack_from("<I", original, 0xD8)[0]
        assert len(corrected) == len(original) + 0x1000
        assert economy.strip_economy(corrected, checksum, style) == original
        for site, _, before, _ in economy.SITES:
            at = site - economy.IMAGE_BASE
            assert corrected[at:at + len(before)] != before


def test_native_balance_composes_with_other_direct_exe_options(
        generated, native_transform, tmp_path):
    _, prior, economy = generated
    for style in ("base", "retain", "zero", "recompute"):
        source = economy.add_economy(prior[(1, 1, 1, style, 1)], style)
        for wanted in ((1, 1, 1, 1, 0), (1, 1, 1, 1, 1),
                       (0, 0, 0, 0, 1), (0, 1, 1, 0, 0)):
            run, output = transform(native_transform, source, wanted, tmp_path)
            assert run.returncode == 0, (style, wanted, run.stderr)
            target_style = style if wanted[:4] == (1, 1, 1, 1) else \
                "base" if style == "base" else "recompute"
            target = prior[(*wanted[:3], target_style, wanted[3])]
            expected = economy.add_economy(target, target_style) if wanted[4] else target
            assert output == expected


def test_native_transform_rejects_mutated_balance_state(
        generated, native_transform, tmp_path):
    _, prior, economy = generated
    source = economy.add_economy(prior[(1, 1, 0, "base", 0)])
    for offset in (0x86, 0x9C, 0xD0, 0xD8, 0x150ADB, len(source) - 0x1000 + 40,
                   len(source) - 1):
        damaged = bytearray(source)
        damaged[offset] ^= 1
        run, output = transform(native_transform, bytes(damaged), (1, 1, 0, 0, 0), tmp_path)
        assert run.returncode == 6, (offset, run.stderr)
        assert output is None


def _write_u32(machine: Uc, address: int, value: int) -> None:
    machine.mem_write(address, struct.pack("<I", value & 0xffffffff))


def _execution_fixture(exe: bytes, *, scalar: int, scalable: bool = True,
                       men: int | None = None, support: int = 5,
                       recruit_cost: int = 225, years: int = 1,
                       base_men: int = 60, mounted: bool = False) -> Uc:
    """Map actual PE32 sections and supply a small, constructed campaign state."""
    troop, unit_type, types = 0x10002000, 0x10001000, 0x10003000
    pe = struct.unpack_from("<I", exe, 0x3c)[0]
    section = pe + 24 + struct.unpack_from("<H", exe, pe + 20)[0]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    # The PE has a large unused virtual gap between its two code/data groups.
    # Map the occupied ranges and the two global-data pages used here.
    machine.mem_map(0x400000, 0x4e0000)
    machine.mem_map(0xbe6000, 0x1000)
    machine.mem_map(0xf32000, 0x1000)
    machine.mem_map(0xf3c000, 0x11000)
    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x20000)
    for index in range(struct.unpack_from("<H", exe, pe + 6)[0]):
        virtual, length, raw = struct.unpack_from("<III", exe, section + index * 40 + 12)
        if length:
            machine.mem_write(0x400000 + virtual, exe[raw:raw + length])
    _write_u32(machine, 0xbe65f8, scalar)
    _write_u32(machine, 0x8d6794, troop)
    _write_u32(machine, 0xf32140, types)
    _write_u32(machine, types, unit_type)
    machine.mem_write(0xf32148, b"\x01")
    machine.mem_write(troop + 0x0b, b"\x00")
    machine.mem_write(troop + 0x0c, bytes((scalar if men is None else men,)))
    machine.mem_write(troop + 0x15, bytes((int(mounted),)))
    machine.mem_write(unit_type + 0x31c, bytes((int(scalable),)))
    for offset, value in ((0x338, support), (0x158, recruit_cost),
                          (0x7c, years), (0x344, base_men), (0x44c, 999)):
        _write_u32(machine, unit_type + offset, value)
    return machine


def _execute(machine: Uc, address: int, args: tuple[int, ...] = (),
             registers: dict[int, int] | None = None) -> int:
    sentinel, stack = 0x30000000, 0x20018000
    pointer = stack - 4 * (1 + len(args))
    _write_u32(machine, pointer, sentinel)
    for index, value in enumerate(args):
        _write_u32(machine, pointer + 4 + index * 4, value)
    machine.reg_write(UC_X86_REG_ESP, pointer)
    for register, value in (registers or {}).items():
        machine.reg_write(register, value)
    machine.emu_start(address, sentinel, count=100000)
    return machine.reg_read(UC_X86_REG_EAX)


@pytest.fixture
def unicorn_windows_seh():
    # Unicorn handles access violations internally while mapping/starting its
    # Windows JIT. Pytest's faulthandler prints a false fatal traceback for
    # each handled SEH event; a real crash still terminates the test process.
    was_enabled = faulthandler.is_enabled()
    if os.name == "nt" and was_enabled:
        faulthandler.disable()
    try:
        yield
    finally:
        if os.name == "nt" and was_enabled:
            faulthandler.enable()


def test_emulated_game_economy_matches_small_reference_at_huge(generated, unicorn_windows_seh):
    stock, _, economy = generated
    balanced = economy.add_economy(stock)
    unit_type = 0x10001000
    expected = {
        60: ((225, 1, 37), (225, 1, 37)),
        80: ((300, 1, 50), (300, 1, 50)),
        100: ((375, 1, 62), (375, 1, 62)),
        120: ((450, 2, 75), (225, 1, 37)),
    }
    for scalar, (original, candidate) in expected.items():
        for exe, wanted in ((stock, original), (balanced, candidate)):
            actual = (
                _execute(_execution_fixture(exe, scalar=scalar), 0x7305f0,
                         (123,), {UC_X86_REG_ECX: unit_type}),
                _execute(_execution_fixture(exe, scalar=scalar), 0x7305a0,
                         registers={UC_X86_REG_ECX: unit_type}),
                _execute(_execution_fixture(exe, scalar=scalar), 0x550a90, (0,)),
            )
            assert actual == wanted, (scalar, exe is balanced, actual, wanted)

    for exe in (stock, balanced):
        common = dict(scalar=120, scalable=False, men=8, support=10,
                      recruit_cost=175, years=2, base_men=8)
        assert _execute(_execution_fixture(exe, **common), 0x7305f0,
                        (123,), {UC_X86_REG_ECX: unit_type}) == 175
        assert _execute(_execution_fixture(exe, **common), 0x7305a0,
                        registers={UC_X86_REG_ECX: unit_type}) == 2
        assert _execute(_execution_fixture(exe, **common), 0x550a90, (0,)) == 10

    # Troop upkeep doubles for the game's mounted flag after the balance
    # adjustment, while the same full formation still contains 120 men.
    assert _execute(_execution_fixture(balanced, scalar=120, mounted=True),
                    0x550a90, (0,)) == 74
    assert _execute(_execution_fixture(balanced, scalar=120, men=59),
                    0x550a90, (0,)) == 18

    for exe, wanted_cost, wanted_years in ((stock, 338, 4),
                                           (balanced, 169, 2)):
        machine = _execution_fixture(exe, scalar=120)
        _write_u32(machine, unit_type + 0x44c, 123)
        assert _execute(machine, 0x7305f0, (123,),
                        {UC_X86_REG_ECX: unit_type}) == wanted_cost
        assert _execute(_execution_fixture(exe, scalar=120, years=2),
                        0x7305a0, registers={UC_X86_REG_ECX: unit_type}) == wanted_years

    for exe, wanted in ((stock, 225), (balanced, 112)):
        machine = _execution_fixture(exe, scalar=120, men=60)
        machine.mem_write(0x568c60, bytes.fromhex("31c0c20400"))
        assert _execute(machine, 0x62aff0, (0, unit_type)) == wanted
    for exe, wanted in ((stock, 168), (balanced, 84)):
        machine = _execution_fixture(exe, scalar=120, men=60)
        machine.mem_write(0x568c60, bytes.fromhex("b801000000c20400"))
        assert _execute(machine, 0x62aff0, (0, unit_type)) == wanted

    image_size = economy.UI.pe_layout(stock)[3]
    entries = dict(zip(economy.ENTRIES,
                       struct.unpack_from("<9I", economy.assemble_hook(image_size))))
    ai_scalar = 0x400000 + image_size + entries["ai_scalar"]
    for scalar, wanted in ((60, 3600), (120, 3600)):
        machine = _execution_fixture(balanced, scalar=scalar)
        _execute(machine, ai_scalar, registers={UC_X86_REG_ECX: 60})
        assert machine.reg_read(UC_X86_REG_ECX) == wanted
    info_support = 0x400000 + image_size + entries["info_support"]
    for scalar, wanted in ((60, 75), (120, 37)):
        machine = _execution_fixture(balanced, scalar=scalar)
        _execute(machine, info_support, (0,),
                 {UC_X86_REG_EBX: unit_type, UC_X86_REG_EDI: 75})
        assert machine.reg_read(UC_X86_REG_EDI) == wanted
