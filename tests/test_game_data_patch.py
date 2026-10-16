"""Exact real-helper transforms for supported Medieval character data files."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
HEROES_STOCK = "62B181D0804E48ECB527CAC1DC38D5DBE6CC79A22B9C576530F6F1C842D6E6DF"
HEROES_FIXED = "B9BB6F6000BB426736F2E113A0D05331A014C8351AB0D8C1B3FD2FE10705ADDB"
NAMES_STOCK = "BDFB6F894C4B21C147A523A1FAD84B1A8F3CF8D809E98C69363266D4B17A7A76"
NAMES_CYD = "A4CC2AF6ECA25792E5E0FC64055706B4C22763C4D4A51080F774B0A8420E080E"
NAMES_KILIJ = "F2847372150BED92E36F87F629A955344D2DEF533D6FD5ED6A1C25EDECF532FA"
NAMES_FIXED = "3421E5FE033FF3CD9D710FFA38F465B970060C4095C56B590C22AEDFEF4A6F15"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def source_file(relative: str, expected: str) -> bytes:
    exe = os.environ.get("MTW_TEST_GAME_EXE")
    if not exe:
        pytest.skip("MTW_TEST_GAME_EXE required for supported game data")
    source = Path(exe).parent / relative
    data = source.read_bytes()
    assert sha(data) == expected, f"Unsupported game data fixture: {source}"
    return data


@pytest.fixture(scope="module")
def native_transform(tmp_path_factory: pytest.TempPathFactory) -> tuple[Path, subprocess.CompletedProcess[str]]:
    compiler_name = os.environ.get("MTW_CC")
    if not compiler_name:
        pytest.skip("MTW_CC required for native transform")
    compiler = Path(compiler_name)
    assert compiler.is_file()
    output = tmp_path_factory.mktemp("game-data-native") / "game-data.exe"
    command = [str(compiler), "-std=c99", "-D_WIN32_WINNT=0x0501", "-Os", "-Wall",
               "-Wextra", "-Werror", "-I", str(ROOT / "src"),
               str(ROOT / "tests/native_game_data_patch.c"),
               "-ladvapi32", "-lrpcrt4", "-lversion", "-o", str(output)]
    env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ["PATH"])
    built = subprocess.run(command, capture_output=True, text=True, env=env, timeout=120)
    return output, built


def transformed(native_transform: Path, tmp_path: Path, kind: str, data: bytes) -> subprocess.CompletedProcess[str]:
    source, output = tmp_path / "input.bin", tmp_path / "output.bin"
    source.write_bytes(data)
    result = subprocess.run([str(native_transform), kind, str(source), str(output)],
                            capture_output=True, text=True, timeout=30)
    result.output_file = output
    return result


def test_sicilian_transform_changes_only_one_list_operand(
    native_transform: tuple[Path, subprocess.CompletedProcess[str]], tmp_path: Path,
) -> None:
    executable, built = native_transform
    assert built.returncode == 0, built.stdout + built.stderr
    original = source_file("campmap/names/DEFAULT_HEROES.TXT", HEROES_STOCK)
    before = b"SET_FORENAMES::\tFN_SICILIAN\t14\t8"
    after = b"SET_FORENAMES::\tFN_SICILIAN\t4\t8"
    assert original.count(before) == 1
    result = transformed(executable, tmp_path, "heroes", original)
    assert result.returncode == 0, result.stderr
    output = result.output_file.read_bytes()
    assert output == original.replace(before, after)
    assert sha(output) == HEROES_FIXED


@pytest.mark.parametrize("cyd,kilij,digest", [
    (False, False, NAMES_STOCK),
    (True, False, NAMES_CYD),
    (False, True, NAMES_KILIJ),
    (True, True, NAMES_FIXED),
])
def test_names_transform_completes_both_entries_from_each_supported_state(
    native_transform: tuple[Path, subprocess.CompletedProcess[str]],
    tmp_path: Path, cyd: bool, kilij: bool, digest: str,
) -> None:
    executable, built = native_transform
    assert built.returncode == 0, built.stdout + built.stderr
    original = source_file("Loc/Eng/NAMES.TXT", NAMES_STOCK)
    cyd_before, cyd_after = b'{"Cydwladwr" (Compatriot)}', b'{"Cydwladwr (Compatriot)"}'
    kilij_before, kilij_after = b'{"Kilij Arslan}', b'{"Kilij Arslan"}'
    assert original.count(cyd_before) == original.count(kilij_before) == 1
    source = original.replace(cyd_before, cyd_after) if cyd else original
    source = source.replace(kilij_before, kilij_after) if kilij else source
    assert sha(source) == digest
    result = transformed(executable, tmp_path, "names", source)
    assert result.returncode == 0, result.stderr
    expected = original.replace(cyd_before, cyd_after).replace(kilij_before, kilij_after)
    assert result.output_file.read_bytes() == expected
    assert sha(expected) == NAMES_FIXED


def test_data_transform_refuses_unrecognized_user_edit(
    native_transform: tuple[Path, subprocess.CompletedProcess[str]], tmp_path: Path,
) -> None:
    executable, built = native_transform
    assert built.returncode == 0, built.stdout + built.stderr
    original = bytearray(source_file("Loc/Eng/NAMES.TXT", NAMES_STOCK))
    original[100] ^= 1
    result = transformed(executable, tmp_path, "names", bytes(original))
    assert result.returncode != 0
    assert "unsupported" in result.stderr.lower()
    assert not result.output_file.exists()
