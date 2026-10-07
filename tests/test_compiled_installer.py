from __future__ import annotations

import hashlib
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import re
import uuid
import sys

import pytest
from registry_isolation import (LEGACY_NAMES, read_registration, registration_name,
                                remove_test_registration, assert_empty_registration)


ROOT = Path(__file__).resolve().parents[1]
DIST_INSTALLER = Path(os.environ.get("MTW_TEST_INSTALLER", str(ROOT / "dist" / "Unofficial Medieval Total War Collection Patch.exe")))
PAYLOAD = ROOT / "vendor" / "runtime"
SUPPORTED_EXE_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
SCROLL_EXE_HASH = "50829CD084355D81EC94D6F4489D1F60E2EF7FA92983D0EAD07D43832DEEF15B"
SPRITE_EXE_HASH = "72A42C3635ED808E87CF85BAF24D39601376E4B0143B109B0A63771541B662D0"
COMBINED_EXE_HASH = "982921CEFDED31C298F249742B8A001BB1A823F77FB3965E01892A34ED12E627"
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


@pytest.fixture(autouse=True)
def isolated_uninstall_registration(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    legacy_before = {name: read_registration(name) for name in LEGACY_NAMES}
    games = []
    create_game = new_game

    def tracked_game(root: Path, name: str) -> Path:
        game = create_game(root, name)
        assert read_registration(registration_name(game)) is None
        games.append(game)
        return game

    monkeypatch.setattr(sys.modules[__name__], "new_game", tracked_game)
    try:
        yield
    finally:
        for game in games:
            remove_test_registration(game, tmp_path)
        assert {name: read_registration(name) for name in LEGACY_NAMES} == legacy_before


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
    manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
    return {name: record["sha256"] for name, record in manifest["files"].items()}


def assert_runtime(game: Path) -> None:
    for name, expected in expected_runtime().items():
        assert sha256(game / name) == expected


def assert_registration(game: Path) -> None:
    values = read_registration(registration_name(game))
    assert values is not None
    assert Path(values["InstallLocation"][0]).resolve() == game.resolve()
    assert values["UninstallString"][0] == f'"{game / "Uninstall Unofficial Medieval Patch.exe"}"'


def run_installer(game: Path, *, terrain_fix: bool | None = None, scroll_fix: bool | None = None,
                  sprite_fix: bool | None = None) -> subprocess.CompletedProcess[str]:
    logs = game.parent / "diagnostics" / uuid.uuid4().hex
    logs.mkdir(parents=True)
    command = [str(game / DIST_INSTALLER.name), "/S", f"/LOGDIR={logs}"]
    if terrain_fix is not None:
        command.append(f"/TERRAINFIX={int(terrain_fix)}")
    if scroll_fix is not None:
        command.append(f"/SCROLLFIX={int(scroll_fix)}")
    if sprite_fix is not None:
        command.append(f"/SPRITEFIX={int(sprite_fix)}")
    result = subprocess.run(
        command,
        cwd=game,
        text=True,
        capture_output=True,
        timeout=180,
    )
    for log in logs.rglob("*.log"):
        encoding = "utf-16" if log.name == "installer.log" else "utf-8-sig"
        result.stdout += f"\n{log}\n" + log.read_text(encoding=encoding, errors="replace")
    return result


def test_compiled_scroll_choice_changes_exe_and_can_toggle(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Scroll choice")
    before = snapshot(game)
    proxy = expected_runtime()["D3D9.dll"]

    disabled = run_installer(game, scroll_fix=False, sprite_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert sha256(game / "D3D9.dll") == proxy
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    disabled_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert disabled_receipt["files"]["D3D9.dll"]["installed_sha256"] == proxy
    for name in RUNTIME_NAMES[1:]:
        assert sha256(game / name) == expected_runtime()[name]

    enabled = run_installer(game, scroll_fix=True, sprite_fix=False)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    assert sha256(game / "D3D9.dll") == proxy
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    enabled_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert enabled_receipt["installation_id"] == disabled_receipt["installation_id"]
    assert enabled_receipt["files"]["D3D9.dll"]["installed_sha256"] == proxy
    assert enabled_receipt["files"]["Medieval_TW.exe"]["installed_sha256"] == SCROLL_EXE_HASH

    disabled_again = run_installer(game, scroll_fix=False, sprite_fix=False)
    assert disabled_again.returncode == 0, (disabled_again.stdout, disabled_again.stderr)
    assert sha256(game / "D3D9.dll") == proxy
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_all_seven_independent_fix_combinations(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Independent fixes")
    before = snapshot(game)
    choices = ((False, False, True), (False, True, False), (False, True, True),
               (True, False, False), (True, False, True),
               (True, True, False), (True, True, True))
    installation_id = None
    for terrain, scroll, sprite in choices:
        manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
        result = run_installer(game, terrain_fix=terrain, scroll_fix=scroll, sprite_fix=sprite)
        assert result.returncode == 0, (result.stdout, result.stderr)
        expected_hash = manifest["files"]["D3D9.dll"]["sha256"]
        if terrain:
            assert sha256(game / "D3D9.dll") == expected_hash
        else:
            assert all(not (game / name).exists() for name in RUNTIME_NAMES)
        exe_hash = {(False, False): SUPPORTED_EXE_HASH, (True, False): SCROLL_EXE_HASH,
                    (False, True): SPRITE_EXE_HASH, (True, True): COMBINED_EXE_HASH}[(scroll, sprite)]
        assert sha256(game / "Medieval_TW.exe") == exe_hash
        assert manifest["scroll_fix_enabled"] is False
        assert manifest["sprite_fix_enabled"] is False
        receipt = json.loads((game / ".unofficial-medieval-total-war-patch" / "install-manifest.json").read_text(encoding="utf-8-sig"))
        assert receipt["files"]["D3D9.dll"]["installed_sha256"] == (expected_hash if terrain else None)
        assert receipt["terrain_fix_enabled"] is terrain
        assert receipt["scroll_fix_enabled"] is scroll
        assert receipt["sprite_fix_enabled"] is sprite
        assert receipt["sprite_delivery"] == "direct-exe"
        assert receipt["schema"] == "unofficial-medieval-total-war-patch-install-v4"
        if installation_id is None:
            installation_id = receipt["installation_id"]
        assert receipt["installation_id"] == installation_id
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_repairs_stock_exe_restored_by_storefront_verification(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Storefront EXE restoration")
    before = snapshot(game)
    assert run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False).returncode == 0
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    original_id = json.loads(receipt_path.read_text(encoding="utf-8-sig"))["installation_id"]
    shutil.copy2(supported_exe_source(), game / "Medieval_TW.exe")
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    repaired = run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["installation_id"] == original_id
    assert receipt["repair_count"] == 1
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def process_has_exited(pid: int) -> bool:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        error = ctypes.get_last_error()
        assert error == 87, f"Cannot inspect diagnostic PID {pid}: Win32 {error}"
        return True
    try:
        return kernel.WaitForSingleObject(handle, 0) == 0
    finally:
        kernel.CloseHandle(handle)


def run_uninstaller(game: Path, *, expect_removal: bool = True, cwd: Path | None = None,
                    registered: bool = False) -> subprocess.CompletedProcess[str]:
    path = game / "Uninstall Unofficial Medieval Patch.exe"
    logs = game.parent / "diagnostics" / uuid.uuid4().hex
    logs.mkdir(parents=True)
    command = [str(path), "/S", f"/LOGDIR={logs}"]
    if registered:
        values = read_registration(registration_name(game))
        assert values is not None, "The scoped fixture has no uninstall registration"
        assert Path(values["InstallLocation"][0]).resolve() == game.resolve()
        quiet = values["QuietUninstallString"][0]
        assert quiet == f'"{path}" /S', "Refusing an unexpected registered command"
        # Execute the actual registered string after validating this fixture's
        # target, with only the test-owned diagnostic directory appended.
        command = quiet + " " + subprocess.list2cmdline([f"/LOGDIR={logs}"])
    result = subprocess.run(command, cwd=cwd or game, text=True, capture_output=True, timeout=180)
    # Normal NSIS uninstall self-copies. The outer launcher's return is not the
    # result of the actual uninstaller: await its durable terminal record and PID.
    deadline = time.monotonic() + 180
    diagnostic = ""
    while time.monotonic() < deadline:
        candidates = list(logs.rglob("installer.log"))
        for candidate in candidates:
            try:
                diagnostic = candidate.read_text(encoding="utf-16")
            except (OSError, UnicodeError):
                continue
            pids = re.findall(r"installer_pid=(\d+)", diagnostic)
            children = re.findall(r"child_pid=(\d+)", diagnostic)
            terminal = any(value in diagnostic for value in (
                "result=success operation=uninstall", "result=failure", "result=completed_with_warning"))
            if terminal and pids and all(process_has_exited(int(pid)) for pid in pids + children):
                break
        else:
            time.sleep(0.1)
            continue
        break
    else:
        pytest.fail(f"No completed uninstaller child within 180s. Diagnostics: {logs}\n{diagnostic}")
    if expect_removal:
        assert "result=success operation=uninstall" in diagnostic, diagnostic
        assert "child_exit=0" in diagnostic, diagnostic
        deadline = time.monotonic() + 10
        while path.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
    result.stdout += diagnostic
    return result


def assert_round_trip(game: Path, before: dict[str, str | None]) -> None:
    install = run_installer(game)
    assert install.returncode == 0, (install.stdout, install.stderr)
    assert_runtime(game)
    assert_registration(game)
    assert (game / ".unofficial-medieval-total-war-patch" / "install-manifest.json").is_file()
    restore = run_uninstaller(game)
    assert restore.returncode == 0, (restore.stdout, restore.stderr)
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    assert_empty_registration(registration_name(game))


def test_compiled_clean_install_and_uninstall(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Clean game with spaces")
    assert_round_trip(game, snapshot(game))


def test_compiled_registered_quiet_uninstall_from_another_directory(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Registered game")
    before = snapshot(game)
    assert run_installer(game).returncode == 0
    caller = tmp_path / "Other working directory"
    caller.mkdir()
    sentinel = caller / "personal.txt"
    sentinel.write_bytes(b"unrelated working directory")
    result = run_uninstaller(game, registered=True, cwd=caller)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert snapshot(game) == before
    assert_empty_registration(registration_name(game))
    assert not (game / "Uninstall Unofficial Medieval Patch.exe").exists()
    assert sentinel.read_bytes() == b"unrelated working directory"


def test_compiled_pre_win7_component_refusal_precedes_helper_launch(tmp_path: Path, monkeypatch) -> None:
    """Exercise the real NSIS refusal branch; this is not execution on Windows XP."""
    game = new_game(tmp_path, "Unsupported component OS")
    (game / ".umtwp-test-fixture").write_text("disposable package capability fixture")
    before = {str(p.relative_to(game)): sha256(p) for p in game.rglob("*") if p.is_file()}
    monkeypatch.setenv("MTW_ENABLE_LIFECYCLE_FAULTS", "1")
    monkeypatch.setenv("MTW_TEST_COMPONENT_OS", "pre-win7")
    result = run_installer(game)
    assert result.returncode == 2, result.stdout
    assert "error=unsupported_component_os" in result.stdout
    assert "no_selection=1" in result.stdout
    assert "child_pid=" not in result.stdout
    assert "phase=prepare-uninstaller" not in result.stdout
    assert {str(p.relative_to(game)): sha256(p) for p in game.rglob("*") if p.is_file()} == before
    assert read_registration(registration_name(game)) is None


def test_compiled_scrolling_only_bypasses_terrain_platform_gate(tmp_path: Path, monkeypatch) -> None:
    """A synthetic pre-Win7 probe checks selection routing, not OS compatibility."""
    game = new_game(tmp_path, "Scrolling-only platform selection")
    (game / ".umtwp-test-fixture").write_text("disposable package capability fixture")
    before = snapshot(game)
    monkeypatch.setenv("MTW_ENABLE_LIFECYCLE_FAULTS", "1")
    monkeypatch.setenv("MTW_TEST_COMPONENT_OS", "pre-win7")
    result = run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    assert all(not (game / name).exists() for name in RUNTIME_NAMES)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_invalid_component_combinations_leave_game_untouched(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Invalid component combinations")
    before = snapshot(game)
    for terrain, scroll, sprite in ((False, False, False),):
        result = run_installer(game, terrain_fix=terrain, scroll_fix=scroll, sprite_fix=sprite)
        assert result.returncode == 2, (result.stdout, result.stderr)
        assert snapshot(game) == before
        assert read_registration(registration_name(game)) is None


def test_compiled_custom_config_is_replaced_and_restored(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing custom config")
    config = game / "dgVoodoo.conf"
    config.write_text(
        "; user-maintained dgVoodoo profile\n"
        "[GeneralExt]\n"
        "DesktopResolution = 1920x1080\n",
        encoding="utf-8",
    )
    before = snapshot(game)
    original_hash = sha256(config)

    install = run_installer(game)
    assert install.returncode == 0, (install.stdout, install.stderr)
    assert_runtime(game)
    assert sha256(game / "dgVoodoo.conf.unofficial-patch.bak") == original_hash

    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_managed_repair_replaces_modified_config(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Managed custom config")
    before_install = snapshot(game)
    assert run_installer(game).returncode == 0

    (game / "dgVoodoo.conf").write_text(
        "; changed after the previous patch installation\n"
        "[GeneralExt]\n"
        "DesktopResolution = 2560x1440\n",
        encoding="utf-8",
    )
    repaired = run_installer(game)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert_runtime(game)

    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before_install


def test_compiled_unmanaged_r186_adoption_and_managed_repair(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing R186")
    clean = snapshot(game)
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
    installed_manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
    for name, expected in expected_runtime().items():
        assert second_receipt["files"][name]["installed_sha256"] == expected
        assert second_receipt["files"][name]["installed_length"] == installed_manifest["files"][name]["length"]
    assert second_receipt["installer_sha256"] == sha256(game / DIST_INSTALLER.name)
    assert_runtime(game)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == clean
    archives = list(game.glob(".medieval-recovery-*"))
    assert len(archives) == 1
    for name in RUNTIME_NAMES:
        assert sha256(archives[0] / name) == before[name]


def test_compiled_two_folders_have_independent_registrations(tmp_path: Path) -> None:
    first = new_game(tmp_path, "First game")
    second = new_game(tmp_path, "Second game")
    for game in (first, second):
        assert run_installer(game).returncode == 0
        assert_registration(game)
    second_registration = read_registration(registration_name(second))
    assert registration_name(first) != registration_name(second)
    assert run_uninstaller(first, cwd=second).returncode == 0
    assert_empty_registration(registration_name(first))
    assert read_registration(registration_name(second)) == second_registration
    assert_runtime(second)
    assert run_uninstaller(second).returncode == 0
    assert_empty_registration(registration_name(second))


def test_compiled_unsupported_executable_is_zero_change(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unsupported executable")
    data = bytearray((game / "Medieval_TW.exe").read_bytes())
    data[-1] ^= 0xFF
    (game / "Medieval_TW.exe").write_bytes(data)
    before = snapshot(game)
    result = run_installer(game)
    assert result.returncode != 0
    # Guarded initialization refuses this identity before opening helper.log.
    # The captured Human console must still explain the real refusal.
    assert 'This Medieval_TW.exe build is unsupported (SHA-256 ' in result.stdout
    assert 'No files were changed.' in result.stdout
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_compiled_unknown_wrapper_is_zero_change(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unknown wrapper")
    (game / "D3D9.dll").write_bytes(b"community wrapper with unknown identity")
    before = snapshot(game)
    result = run_installer(game)
    assert result.returncode != 0
    assert '"code":"wrapper_conflict"' in result.stdout
    assert snapshot(game) == before


def test_compiled_existing_backup_is_never_overwritten(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing backup")
    sidecar = game / "D3D9.dll.unofficial-patch.bak"
    sidecar.write_bytes(b"older backup remains authoritative to its owner")
    before = snapshot(game)
    assert_round_trip(game, before)


def test_compiled_game_running_refuses_without_changes(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Running game")
    before = snapshot(game)
    # Hold the actual supported target image without executing game code.
    process = subprocess.Popen(
        [str(game / "Medieval_TW.exe")],
        cwd=game, creationflags=0x00000004,  # CREATE_SUSPENDED
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        assert process.poll() is None
        result = run_installer(game)
        assert result.returncode != 0
        assert '"code":"game_running"' in result.stdout
        assert snapshot(game) == before
    finally:
        process.terminate()
        process.wait(timeout=10)


def test_compiled_unrelated_same_basename_process_does_not_block_target(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Independent game")
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
        assert_round_trip(game, before)
    finally:
        process.terminate()
        process.wait(timeout=10)


def test_compiled_short_temp_alias_round_trip(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    temp = Path(os.environ["LOCALAPPDATA"]) / "Temp"
    assert temp.is_dir()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetShortPathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    kernel.GetShortPathNameW.restype = wintypes.DWORD
    buffer = ctypes.create_unicode_buffer(32768)
    length = kernel.GetShortPathNameW(str(temp), buffer, len(buffer))
    assert 0 < length < len(buffer), ctypes.get_last_error()
    if "~" not in buffer.value:
        pytest.skip("This user's temporary directory has no 8.3 alias")
    monkeypatch.setenv("TEMP", buffer.value)
    monkeypatch.setenv("TMP", buffer.value)
    game = new_game(tmp_path, "Short temporary path")
    assert_round_trip(game, snapshot(game))


def test_compiled_unicode_path_round_trip(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Mediæval Ünicode 騎士")
    assert_round_trip(game, snapshot(game))


def test_compiled_restore_archives_user_modified_file(tmp_path: Path) -> None:
    game = new_game(tmp_path, "User modified")
    before_install = snapshot(game)
    assert run_installer(game).returncode == 0
    changed = b"user replacement after installation"
    (game / "D3D9.dll").write_bytes(changed)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before_install
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    archives = list(game.glob(".medieval-recovery-*/D3D9.dll"))
    assert len(archives) == 1
    assert archives[0].read_bytes() == changed
    assert_empty_registration(registration_name(game))


def test_compiled_elevation_handoff_rejects_another_account(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Account-bound handoff ü")
    before = snapshot(game)
    logs = tmp_path / "handoff diagnostics"
    # NSIS /D= consumes the remaining, unquoted command line. Mirror the
    # elevation launcher rather than Python's generic argv quoting rules.
    command = f'"{game / DIST_INSTALLER.name}" /S /REQUIREOWNER=S-1-5-18 /LOGDIR="{logs}" /D={game}'
    result = subprocess.run(command, cwd=tmp_path,
                            capture_output=True, timeout=60)
    assert result.returncode == 2
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    assert read_registration(registration_name(game)) is None
    console = "\n".join(p.read_text(encoding="utf-8") for p in logs.rglob("helper-console.log"))
    assert "same Windows account" in console
