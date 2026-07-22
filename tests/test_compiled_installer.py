from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import winreg

import pytest


ROOT = Path(__file__).resolve().parents[1]
DIST_INSTALLER = ROOT / "dist" / "Unofficial Medieval Total War Collection Patch.exe"
PAYLOAD = ROOT / "vendor" / "runtime"
SUPPORTED_EXE_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
UNINSTALL_PARENT = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
UNINSTALL_NAME = "Unofficial Medieval Total War Collection Patch"
RUNTIME_NAMES = ("D3D9.dll", "dgVoodoo_D3D9.dll", "ddraw.dll", "D3DImm.dll", "dgVoodoo.conf")


pytestmark = pytest.mark.skipif(
    os.environ.get("MTW_RUN_COMPILED_INSTALLER_TESTS") != "1",
    reason="set MTW_RUN_COMPILED_INSTALLER_TESTS=1 to run compiled installer tests",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def supported_exe_source() -> Path:
    value = os.environ.get("MTW_TEST_GAME_EXE")
    if not value:
        pytest.fail("MTW_TEST_GAME_EXE is required")
    path = Path(value)
    assert sha256(path) == SUPPORTED_EXE_HASH
    return path


def remove_uninstall_key() -> None:
    access = winreg.KEY_WRITE | winreg.KEY_WOW64_32KEY
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_PARENT, 0, access) as parent:
            winreg.DeleteKey(parent, UNINSTALL_NAME)
    except FileNotFoundError:
        pass


@pytest.fixture(autouse=True)
def isolated_uninstall_registration() -> None:
    remove_uninstall_key()
    yield
    remove_uninstall_key()


def new_game(root: Path, name: str) -> Path:
    game = root / name
    game.mkdir(parents=True)
    shutil.copy2(supported_exe_source(), game / "Medieval_TW.exe")
    shutil.copy2(DIST_INSTALLER, game / DIST_INSTALLER.name)
    return game


def snapshot(game: Path) -> dict[str, str | None]:
    names = ["Medieval_TW.exe", *RUNTIME_NAMES, "D3D8.dll"]
    names += [f"{name}.unofficial-patch.bak" for name in RUNTIME_NAMES]
    return {name: sha256(game / name) if (game / name).is_file() else None for name in names}


def expected_runtime() -> dict[str, str]:
    manifest = json.loads((PAYLOAD / "payload-manifest.json").read_text(encoding="utf-8"))
    return {name: record["sha256"] for name, record in manifest["files"].items()}


def assert_runtime(game: Path) -> None:
    for name, expected in expected_runtime().items():
        assert sha256(game / name) == expected


def run_installer(game: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(game / DIST_INSTALLER.name), "/S"],
        cwd=game,
        text=True,
        capture_output=True,
        timeout=180,
    )


def run_uninstaller(game: Path, *, expect_removal: bool = True) -> subprocess.CompletedProcess[str]:
    path = game / ".unofficial-medieval-total-war-patch" / "Uninstall.exe"
    result = subprocess.run([str(path), "/S"], cwd=game, text=True, capture_output=True, timeout=180)
    if expect_removal:
        deadline = time.monotonic() + 10
        while path.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
    return result


def assert_round_trip(game: Path, before: dict[str, str | None]) -> None:
    install = run_installer(game)
    assert install.returncode == 0, (install.stdout, install.stderr)
    assert_runtime(game)
    assert (game / ".unofficial-medieval-total-war-patch" / "install-manifest.json").is_file()
    restore = run_uninstaller(game)
    assert restore.returncode == 0, (restore.stdout, restore.stderr)
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_compiled_clean_install_and_uninstall(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Clean game with spaces")
    assert_round_trip(game, snapshot(game))


def test_compiled_unmanaged_r185_adoption_and_managed_repair(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing R185")
    for name in RUNTIME_NAMES:
        shutil.copy2(PAYLOAD / name, game / name)
    before = snapshot(game)
    first = run_installer(game)
    assert first.returncode == 0
    manifest_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    first_receipt = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    second = run_installer(game)
    assert second.returncode == 0
    second_receipt = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    assert second_receipt["installation_id"] == first_receipt["installation_id"]
    assert second_receipt["repair_count"] == 1
    assert_runtime(game)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_unsupported_executable_is_zero_change(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unsupported executable")
    data = bytearray((game / "Medieval_TW.exe").read_bytes())
    data[-1] ^= 0xFF
    (game / "Medieval_TW.exe").write_bytes(data)
    before = snapshot(game)
    result = run_installer(game)
    assert result.returncode != 0
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_compiled_unknown_wrapper_is_zero_change(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unknown wrapper")
    (game / "D3D9.dll").write_bytes(b"community wrapper with unknown identity")
    before = snapshot(game)
    result = run_installer(game)
    assert result.returncode != 0
    assert snapshot(game) == before


def test_compiled_existing_backup_is_never_overwritten(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing backup")
    sidecar = game / "D3D9.dll.unofficial-patch.bak"
    sidecar.write_bytes(b"older backup remains authoritative to its owner")
    before = snapshot(game)
    assert_round_trip(game, before)


def test_compiled_game_running_refuses_without_changes(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Running game")
    helper_dir = tmp_path / "process helper"
    helper_dir.mkdir()
    helper = helper_dir / "Medieval_TW.exe"
    shutil.copy2(Path(os.environ["SystemRoot"]) / "System32" / "ping.exe", helper)
    before = snapshot(game)
    process = subprocess.Popen(
        [str(helper), "-t", "127.0.0.1"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(0.3)
        result = run_installer(game)
        assert result.returncode != 0
        assert snapshot(game) == before
    finally:
        process.terminate()
        process.wait(timeout=10)


def test_compiled_unicode_path_round_trip(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Mediæval Ünicode 騎士")
    assert_round_trip(game, snapshot(game))


def test_compiled_restore_refuses_user_modified_file(tmp_path: Path) -> None:
    game = new_game(tmp_path, "User modified")
    assert run_installer(game).returncode == 0
    (game / "D3D9.dll").write_bytes(b"user replacement after installation")
    before_restore = snapshot(game)
    refused = run_uninstaller(game, expect_removal=False)
    # A normal NSIS uninstaller self-copies to a temporary child process. Its
    # outer launcher can return zero even when the child deliberately aborts;
    # the authoritative refusal oracle is the untouched file/receipt state.
    assert refused.returncode in (0, 2)
    assert snapshot(game) == before_restore
    assert (game / ".unofficial-medieval-total-war-patch").exists()

    shutil.copy2(PAYLOAD / "D3D9.dll", game / "D3D9.dll")
    assert run_uninstaller(game).returncode == 0
