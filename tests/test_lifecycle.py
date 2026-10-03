"""Real engine lifecycle tests. All game and registry targets are disposable.

These small fixtures use the unchanged supported game executable, not gameplay
data. Packaged/GUI tests independently exercise the generated NSIS uninstaller.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid
import winreg

import pytest
from native_helper import helper_path
from registry_isolation import read_registration, registration_name, remove_test_registration

ROOT = Path(__file__).resolve().parents[1]
ENGINE = helper_path()
PAYLOAD = ROOT / "vendor/runtime"
STATE = ".unofficial-medieval-total-war-patch"
TX = ".unofficial-medieval-total-war-patch-transaction"
UNINSTALL = "Uninstall Unofficial Medieval Patch.exe"
PREFIX = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\UnofficialMedievalPatch-"
PRODUCT = json.loads((ROOT / "config/product.json").read_text(encoding="utf-8"))
NAMES = tuple(json.loads((PAYLOAD / "payload-manifest.json").read_text())['files'])
SUPPORTED = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def registry_key(game: Path) -> str:
    return PREFIX + hashlib.sha256(str(game.resolve()).lower().encode("utf-8")).hexdigest()[:32]


def registry(game: Path) -> dict:
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
            values = {}
            for i in range(winreg.QueryInfoKey(key)[1]):
                name, value, kind = winreg.EnumValue(key, i)
                values[name] = value
            return values
    except FileNotFoundError:
        return {}


@pytest.fixture
def games(tmp_path: Path):
    created = []

    def make(name="Game Folder"):
        source = os.environ.get("MTW_TEST_GAME_EXE")
        if not source:
            pytest.skip("MTW_TEST_GAME_EXE required; supported EXE checks are never replaced")
        assert digest(Path(source)) == SUPPORTED
        game = tmp_path / name
        game.mkdir()
        shutil.copy2(source, game / "Medieval_TW.exe")
        (game / "SaveGames").mkdir()
        (game / "SaveGames/personal.cpg").write_bytes(b"untouched personal save\x00\xff")
        (game / "unrelated-mod.txt").write_bytes(b"unrelated mod")
        assert read_registration(registration_name(game)) is None, "Fixture identity must not overwrite an existing key"
        created.append(game)
        return game

    yield make
    for game in created:
        remove_test_registration(game, tmp_path)


def run(operation: str, game: Path, *, fault: str = "", uninstaller: Path | None = None):
    # This models a supplied package artifact, not a functional NSIS executable.
    # The compiled suite uses WriteUninstaller's actual output instead.
    source = uninstaller or game.parent / "synthetic-uninstaller.bin"
    if not source.exists():
        source.write_bytes(b"synthetic package uninstaller for lifecycle-only tests\x00\xff")
    command = [str(ENGINE),
               "-Operation", operation, "-Target", str(game),
               "-PayloadDirectory", str(PAYLOAD), "-InstallerVersion", PRODUCT["version"],
               "-UninstallerSource", str(source)]
    environment = dict(os.environ)
    if fault:
        environment["MTW_ENABLE_LIFECYCLE_FAULTS"] = "1"
        (game / ".umtwp-test-fixture").write_text("disposable lifecycle test\n")
        command += ["-TestFault", fault]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=90, env=environment)
    lines = [line for line in result.stdout.splitlines() if line.startswith("{")]
    report = json.loads(lines[-1]) if lines else {}
    return result, report


def installed(game: Path):
    result, report = run("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    return report


def relevant(game: Path):
    names = (*NAMES, "Medieval_TW.exe", "SaveGames/personal.cpg", "unrelated-mod.txt")
    return {name: digest(game / name) if (game / name).exists() else None for name in names}


def read_receipt(game: Path):
    return json.loads((game / STATE / "install-manifest.json").read_text(encoding="utf-8-sig"))


def test_installs_root_uninstaller_and_one_owned_entry(games):
    game = games()
    before = relevant(game)
    report = installed(game)
    assert (game / UNINSTALL).is_file()
    assert not (game / STATE / "Uninstall.exe").exists()
    values = registry(game)
    assert values["InstallLocation"] == str(game)
    assert values["UninstallString"] == f'"{game / UNINSTALL}"'
    assert values["QuietUninstallString"] == f'"{game / UNINSTALL}" /S'
    assert values["NoModify"] == values["NoRepair"] == 1
    assert str(game) in values["DisplayName"]
    assert values["InstallationId"] == report["installation_id"]
    assert values["DisplayVersion"] == PRODUCT["version"]
    result, removed = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert removed["restoration"] == "verified"
    assert relevant(game) == before
    assert not registry(game)
    assert not (game / UNINSTALL).exists()
    assert not (game / STATE).exists()
    assert not (game / TX).exists()


def test_two_installs_repair_and_removal_stay_isolated(games):
    first, second = games("GOG copy"), games("Steam copy")
    installed(first)
    installed(second)
    second_files, second_entry = relevant(second), registry(second)
    # Windows Settings truncates long app names at about 40 characters. Keep
    # each path-derived copy identity visible before the descriptive suffix.
    for game in (first, second):
        short_id = registry_key(game).rsplit("-", 1)[-1][:8]
        assert registry(game)["DisplayName"].startswith(f"Unofficial Medieval Patch [{short_id}]")
    assert registry(first)["DisplayName"][:36] != second_entry["DisplayName"][:36]
    first_id = read_receipt(first)["installation_id"]
    installed(first)
    assert read_receipt(first)["installation_id"] == first_id
    result, _ = run("Restore", first)
    assert result.returncode == 0
    assert relevant(second) == second_files
    assert registry(second) == second_entry
    installed(first)
    assert registry(first)


def test_modified_owned_files_are_verified_in_archive_before_removal(games):
    game = games()
    before = relevant(game)
    installed(game)
    replacements = {"D3D9.dll": b"user replacement wrapper\x00\xff",
                    "dgVoodoo.conf": b"; personal display profile\r\n"}
    for name, data in replacements.items():
        (game / name).write_bytes(data)
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant(game) == before
    archive = Path(report["recovery_archive"])
    for name, data in replacements.items():
        assert (archive / name).read_bytes() == data
    assert (archive / "manifest.json").is_file()


def test_unknown_metadata_and_changed_sidecar_are_preserved(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"original config")
    installed(game)
    unknown = game / STATE / "personal-notes.txt"
    unknown.write_bytes(b"must remain")
    sidecar = game / "dgVoodoo.conf.unofficial-patch.bak"
    sidecar.write_bytes(b"edited old backup")
    result, _ = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert unknown.read_bytes() == b"must remain"
    assert sidecar.read_bytes() == b"edited old backup"
    assert (game / "dgVoodoo.conf").read_bytes() == b"original config"
    # A verified retired receipt allows future setup to coexist with preserved
    # unknown metadata, while arbitrary orphaned state still fails closed.
    assert read_receipt(game)["status"] == "restored"
    installed(game)
    assert unknown.read_bytes() == b"must remain"
    assert sidecar.read_bytes() == b"edited old backup"


def test_missing_original_fails_without_touching_runtime_or_entry(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"original config")
    installed(game)
    (game / STATE / "originals/dgVoodoo.conf").unlink()
    before, entry = relevant(game), registry(game)
    result, report = run("Restore", game)
    assert result.returncode != 0
    assert report["code"] == "receipt_invalid"
    assert relevant(game) == before
    assert registry(game) == entry
    assert (game / UNINSTALL).exists()


def test_copied_v2_state_cannot_redirect_removal(games):
    game = games("source copy")
    installed(game)
    copied = game.parent / "copied folder"
    shutil.copytree(game, copied)
    before, entry = relevant(game), registry(game)
    result, report = run("Restore", copied)
    assert result.returncode != 0
    assert report["code"] == "installation_moved"
    assert relevant(game) == before == relevant(copied)
    assert registry(game) == entry


def test_repair_archives_modified_config_and_preserves_first_original(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"prepatch config")
    installed(game)
    (game / "dgVoodoo.conf").write_bytes(b"modified during patch use")
    result, report = run("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert (Path(report["recovery_archive"]) / "dgVoodoo.conf").read_bytes() == b"modified during patch use"
    assert (game / STATE / "originals/dgVoodoo.conf").read_bytes() == b"prepatch config"
    result, _ = run("Restore", game)
    assert result.returncode == 0
    assert (game / "dgVoodoo.conf").read_bytes() == b"prepatch config"


@pytest.mark.parametrize("point", ["after-file-0", "after-file-5", "after-registry", "before-commit"])
def test_failed_upgrade_restores_previous_files_receipt_and_uninstaller(games, point):
    game = games()
    installed(game)
    receipt = (game / STATE / "install-manifest.json").read_bytes()
    prior = relevant(game), registry(game), digest(game / UNINSTALL)
    replacement = game.parent / "new-uninstaller.bin"
    replacement.write_bytes(b"new version synthetic uninstaller")
    result, report = run("Install", game, fault="throw:" + point, uninstaller=replacement)
    assert result.returncode != 0
    assert report["rollback"] == "verified"
    assert (relevant(game), registry(game), digest(game / UNINSTALL)) == prior
    assert (game / STATE / "install-manifest.json").read_bytes() == receipt


@pytest.mark.skipif(os.environ.get("MTW_RUN_LIFECYCLE_FAULTS") != "1",
                    reason="set MTW_RUN_LIFECYCLE_FAULTS=1 for abrupt process termination")
@pytest.mark.parametrize("point", ["after-file-0", "after-file-5", "after-registry", "before-commit"])
def test_crash_recovery_preserves_original_baseline(games, point):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"earliest config")
    before = relevant(game)
    result, _ = run("Install", game, fault="crash:" + point)
    assert result.returncode != 0
    assert (game / TX / "journal.json").is_file()
    installed(game)
    result, _ = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant(game) == before


def test_uninstall_cleanup_failure_leaves_retry_route(games):
    game = games()
    before = relevant(game)
    installed(game)
    result, report = run("Restore", game, fault="throw:cleanup-uninstaller")
    assert result.returncode != 0
    assert report["restoration"] == "verified"
    assert relevant(game) == before
    assert (game / UNINSTALL).exists()
    assert registry(game)
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert not (game / UNINSTALL).exists()
    assert not registry(game)
    assert relevant(game) == before


@pytest.mark.skipif(os.environ.get("MTW_RUN_LIFECYCLE_FAULTS") != "1",
                    reason="set MTW_RUN_LIFECYCLE_FAULTS=1 for cleanup interruptions")
@pytest.mark.parametrize("fault", [
    "crash:cleanup-after-first-backup",
    "throw:after-file-5,crash:rollback-after-restoration",
    "crash:cleanup-after-journal-delete",
])
def test_interrupted_cleanup_cannot_strand_the_next_operation(games, fault):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"first config must survive cleanup interruption")
    before = relevant(game)
    failed, _ = run("Install", game, fault=fault)
    assert failed.returncode != 0
    installed(game)
    result, _ = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant(game) == before


def run_at_wait(operation, game, point, mutation, *, uninstaller=None):
    source = uninstaller or game.parent / "synthetic-uninstaller.bin"
    if not source.exists():
        source.write_bytes(b"synthetic package uninstaller for lifecycle-only tests\x00\xff")
    env = dict(os.environ, MTW_ENABLE_LIFECYCLE_FAULTS="1")
    (game / ".umtwp-test-fixture").write_text("disposable lifecycle test\n")
    command = [str(ENGINE), "-Operation", operation, "-Target", str(game),
               "-PayloadDirectory", str(PAYLOAD), "-InstallerVersion", PRODUCT["version"],
               "-UninstallerSource", str(source), "-TestFault", "wait:" + point]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               encoding="utf-8", errors="replace", env=env)
    signal = game / ".umtwp-fault-waiting"
    deadline = time.monotonic() + 15
    try:
        ready = False
        while process.poll() is None and time.monotonic() < deadline:
            try:
                ready = signal.read_text() == point
                if ready:
                    break
            except (FileNotFoundError, PermissionError):
                # CREATE_NEW publication becomes visible before its exclusive
                # producer handle closes. Wait for the complete ready message.
                pass
            time.sleep(0.03)
        assert ready, "The real engine must reach and publish the named pause before mutation"
        mutation()
        signal.unlink()
        output, error = process.communicate(timeout=30)
        reports = [line for line in output.splitlines() if line.startswith("{")]
        return process.returncode, json.loads(reports[-1]) if reports else {}, output, error
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def test_original_changed_after_validation_is_not_restored(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"real original")
    installed(game)
    before, entry = relevant(game), registry(game)
    original = game / STATE / "originals/dgVoodoo.conf"
    code, report, out, err = run_at_wait("Restore", game, "before-stage-originals",
                                        lambda: original.write_bytes(b"corrupt changed original"))
    assert code != 0, (out, err)
    assert report["code"] == "concurrent_change"
    assert relevant(game) == before
    assert registry(game) == entry


def test_supplied_uninstaller_changed_during_staging_cannot_commit(games):
    game = games()
    installed(game)
    before, entry, root_hash = relevant(game), registry(game), digest(game / UNINSTALL)
    source = game.parent / "next-version.exe"
    source.write_bytes(b"new uninstaller")
    code, report, out, err = run_at_wait("Install", game, "before-stage-uninstaller",
                                        lambda: source.write_bytes(b"changed after expected hash"), uninstaller=source)
    assert code != 0, (out, err)
    assert report["code"] == "invalid_payload"
    assert relevant(game) == before
    assert registry(game) == entry
    assert digest(game / UNINSTALL) == root_hash


def test_changed_uninstaller_at_cleanup_is_archived_and_retry_route_verified(games):
    game = games()
    installed(game)
    root_hash = digest(game / UNINSTALL)
    code, report, out, err = run_at_wait("Restore", game, "cleanup-uninstaller",
                                        lambda: (game / UNINSTALL).write_bytes(b"unexpected replacement during cleanup"))
    assert code != 0, (out, err)
    assert report["restoration"] == "verified"
    assert digest(game / UNINSTALL) == root_hash
    assert registry(game)["UninstallString"] == f'"{game / UNINSTALL}"'
    assert (Path(report["recovery_archive"]) / "conflicting-uninstaller.exe").read_bytes() == b"unexpected replacement during cleanup"
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert not registry(game)


def test_failed_fresh_preparation_does_not_create_unrecoverable_state(games):
    game = games()
    (game / 'dgVoodoo.conf').write_bytes(b'preexisting personal configuration')
    source = game.parent / "synthetic-uninstaller.bin"
    source.write_bytes(b"new removal artifact")
    before = relevant(game)
    code, report, out, err = run_at_wait("Install", game, "before-stage-uninstaller",
                                        source.unlink, uninstaller=source)
    assert code != 0, (out, err)
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not registry(game)
    installed(game)
    assert run("Restore", game)[0].returncode == 0
    assert relevant(game) == before


def test_unknown_registry_value_types_survive_repair_and_removal(games):
    game = games()
    installed(game)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0,
                        winreg.KEY_SET_VALUE | winreg.KEY_WOW64_32KEY) as key:
        winreg.SetValueEx(key, "PersonalOpaque", 0, winreg.REG_NONE, b"personal binary\x00\xff")
    try:
        installed(game)
        result, _ = run("Restore", game)
        assert result.returncode == 0, (result.stdout, result.stderr)
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
            assert winreg.QueryValueEx(key, "PersonalOpaque") == (b"personal binary\x00\xff", winreg.REG_NONE)
            assert winreg.QueryInfoKey(key)[1] == 1
    finally:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0,
                            winreg.KEY_SET_VALUE | winreg.KEY_WOW64_32KEY) as key:
            winreg.DeleteValue(key, "PersonalOpaque")
        # This exact per-test path was empty before setup, and the only retained
        # value was just inspected byte for byte and removed by its creator.
        if not registry(game):
            try:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, registry_key(game), winreg.KEY_WOW64_32KEY)
            except FileNotFoundError:
                pass


def test_archive_readme_cannot_overwrite_an_existing_hardlink(games):
    game = games()
    installed(game)
    (game / "dgVoodoo.conf").write_bytes(b"changed managed config")
    outside = game.parent / "unrelated-outside-file.txt"
    outside.write_bytes(b"outside content must stay byte-identical")
    before, entry = relevant(game), registry(game)

    def insert_link():
        archive = next(game.glob(".medieval-recovery-*"))
        os.link(outside, archive / "README.txt")

    code, report, out, err = run_at_wait("Restore", game, "archive-write", insert_link)
    assert code != 0, (out, err)
    assert outside.read_bytes() == b"outside content must stay byte-identical"
    assert relevant(game) == before
    assert registry(game) == entry


@pytest.mark.parametrize("operation", ["Install", "Restore"])
def test_change_between_archive_and_staging_is_retained(games, operation):
    game = games()
    installed(game)
    changed = game / "dgVoodoo.conf"
    changed.write_bytes(b"first personal config, archived")
    entry = registry(game)
    code, report, out, err = run_at_wait(operation, game, "after-preservation",
                                        lambda: changed.write_bytes(b"newest personal config"))
    assert code != 0, (out, err)
    assert report["code"] == "concurrent_change"
    assert changed.read_bytes() == b"newest personal config"
    assert registry(game) == entry
    assert not (game / TX).exists()
    assert (Path(report["recovery_archive"]) / "dgVoodoo.conf").read_bytes() == b"first personal config, archived"


def test_fresh_baseline_change_before_staging_is_retained(games):
    game = games()
    changed = game / "dgVoodoo.conf"
    changed.write_bytes(b"original configuration A")
    code, report, out, err = run_at_wait("Install", game, "after-preservation",
                                        lambda: changed.write_bytes(b"newest configuration B"))
    assert code != 0, (out, err)
    assert report["code"] == "concurrent_change"
    assert changed.read_bytes() == b"newest configuration B"
    assert not (game / STATE).exists()
    assert not registry(game)


@pytest.mark.parametrize("cleanup_failure", [False, True])
def test_latest_unknown_registry_data_survives_committed_cleanup(games, cleanup_failure):
    game = games()
    installed(game)
    path = registry_key(game)
    access = winreg.KEY_SET_VALUE | winreg.KEY_QUERY_VALUE | winreg.KEY_WOW64_32KEY
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, access) as key:
        winreg.SetValueEx(key, "PersonalNote", 0, winreg.REG_SZ, "earlier value")
    try:
        def change_unknown():
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, access) as key:
                winreg.SetValueEx(key, "PersonalNote", 0, winreg.REG_SZ, "new user value")
            if cleanup_failure:
                (game / UNINSTALL).write_bytes(b"changed during cleanup")
        point = "cleanup-uninstaller" if cleanup_failure else "after-registry"
        code, report, out, err = run_at_wait("Restore", game, point, change_unknown)
        assert code == (3 if cleanup_failure else 0), (out, err)
        assert registry(game)["PersonalNote"] == "new user value"
        if cleanup_failure:
            assert registry(game)["UninstallString"] == f'"{game / UNINSTALL}"'
            result, _ = run("Restore", game)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert registry(game)["PersonalNote"] == "new user value"
    finally:
        if "PersonalNote" in registry(game):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, access) as key:
                winreg.DeleteValue(key, "PersonalNote")
        if not registry(game):
            try:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, path, winreg.KEY_WOW64_32KEY)
            except FileNotFoundError:
                pass


def test_interactive_preflight_accepts_pending_recovery_without_mutation(games):
    game = games()
    before = relevant(game)
    crashed, _ = run("Install", game, fault="crash:after-file-0")
    assert crashed.returncode != 0
    assert not (game / UNINSTALL).exists()
    journal = game / TX / "journal.json"
    interrupted, entry, journal_hash = relevant(game), registry(game), digest(journal)
    checked, report = run("Inspect", game)
    assert checked.returncode == 0, (checked.stdout, checked.stderr)
    assert report["mode"] == "recovery-pending"
    assert report["recovery_pending"] is True
    assert relevant(game) == interrupted
    assert registry(game) == entry
    assert digest(journal) == journal_hash
    installed(game)
    assert run("Restore", game)[0].returncode == 0
    assert relevant(game) == before
