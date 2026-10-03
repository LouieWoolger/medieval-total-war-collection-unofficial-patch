"""Native migration and preservation regressions on disposable game fixtures."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import winreg

import pytest

from native_helper import canonical_json, seal
from test_lifecycle import (
    ENGINE, PAYLOAD, PRODUCT, STATE, TX, UNINSTALL, digest, games, installed,
    read_receipt, registry, registry_key, relevant, run, run_at_wait,
)
from test_lifecycle_adversarial import tree_snapshot


@pytest.mark.parametrize("operation", ["Install", "Restore"])
def test_supported_executable_stays_pinned_until_the_operation_finishes(games, operation):
    game = games()
    before = relevant(game)
    if operation == "Restore":
        installed(game)
    executable = game / "Medieval_TW.exe"
    original = digest(executable)

    def try_update():
        with pytest.raises(PermissionError):
            executable.write_bytes(b"unsupported concurrent replacement")
        with pytest.raises(PermissionError):
            executable.rename(game / "replaced-executable.exe")
        assert digest(executable) == original

    code, report, output, error = run_at_wait(operation, game, "after-preservation", try_update)
    assert code == 0, (report, output, error)
    with executable.open("r+b"):
        pass  # The operation must release its pin when its process exits.
    if operation == "Install":
        result, report = run("Restore", game)
        assert result.returncode == 0, report
    assert relevant(game) == before


@pytest.mark.parametrize("point", ["cleanup-after-first-backup", "cleanup-after-journal-delete",
                                  "before-result-log"])
def test_completed_removal_reports_residue_without_inventing_a_retry_route(games, point):
    game = games()
    before = relevant(game)
    installed(game)
    result, report = run("Restore", game, fault="throw:" + point)
    assert result.returncode == 4, report
    assert report["restoration"] == "verified"
    assert report["removal_completed"] is True
    assert relevant(game) == before
    assert not registry(game)
    assert not (game / UNINSTALL).exists()
    assert not (game / TX).exists()
    residue = list(game.glob(".medieval-cleanup-*"))
    assert bool(residue) == (point != "before-result-log")
    # Retired scratch state does not block a new usable installation/removal.
    installed(game)
    result, report = run("Restore", game)
    assert result.returncode == 0, report
    assert relevant(game) == before


def test_incomplete_removal_still_retains_a_verified_retry_route(games):
    game = games()
    installed(game)
    result, report = run("Restore", game, fault="throw:cleanup-uninstaller")
    assert result.returncode == 3, report
    assert report["removal_completed"] is False
    assert (game / UNINSTALL).is_file()
    assert registry(game)["UninstallString"] == f'"{game / UNINSTALL}"'
    result, report = run("Restore", game)
    assert result.returncode == 0, report


@pytest.mark.parametrize("relative", ["Medieval_TW.exe", "dgVoodoo.conf", UNINSTALL,
                                     STATE + "/install-manifest.json",
                                     STATE + "/originals/dgVoodoo.conf", TX + "/journal.json"])
def test_log_cannot_alias_game_or_recovery_input(games, relative):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"personal original")
    installed(game)
    before, registration = tree_snapshot(game), registry(game)
    result = subprocess.run([str(ENGINE), "--operation", "Restore", "--target", str(game),
                             "--payload", str(PAYLOAD), "--version", PRODUCT["version"],
                             "--log", str(game / relative)], capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert result.returncode == 2
    assert json.loads(result.stdout)["code"] == "invalid_request", result.stdout
    assert tree_snapshot(game) == before
    assert registry(game) == registration


def test_log_cannot_alias_payload(games):
    game = games()
    before = {p.name: digest(p) for p in PAYLOAD.iterdir() if p.is_file()}
    result = subprocess.run([str(ENGINE), "--operation", "Inspect", "--target", str(game),
                             "--payload", str(PAYLOAD), "--version", PRODUCT["version"],
                             "--log", str(PAYLOAD / "dgVoodoo.conf")], capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert result.returncode == 2
    assert json.loads(result.stdout)["code"] == "invalid_request", result.stdout
    assert {p.name: digest(p) for p in PAYLOAD.iterdir() if p.is_file()} == before


def test_case_changed_registry_names_do_not_bypass_ownership(games):
    game = games()
    installed(game)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0, winreg.KEY_ALL_ACCESS) as key:
        original = [winreg.EnumValue(key, i) for i in range(winreg.QueryInfoKey(key)[1])]
        for name, _, _ in original:
            winreg.DeleteValue(key, name)
        for name, value, kind in original:
            winreg.SetValueEx(key, name.lower(), 0, kind, "foreign-owner" if name == "OwnerSid" else value)
    before, registration = tree_snapshot(game), registry(game)
    try:
        result, report = run("Install", game)
        assert result.returncode == 2
        assert report["code"] == "registry_identity_conflict", report
        assert tree_snapshot(game) == before
        assert registry(game) == registration
    finally:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0, winreg.KEY_ALL_ACCESS) as key:
            for name in list(registry(game)):
                winreg.DeleteValue(key, name)
            for name, value, kind in original:
                winreg.SetValueEx(key, name, 0, kind, value)


def test_unknown_default_registry_value_survives_round_trip(games):
    game = games()
    installed(game)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0, winreg.KEY_ALL_ACCESS) as key:
        winreg.SetValueEx(key, "", 0, winreg.REG_BINARY, b"personal default value")
    result, report = run("Restore", game)
    assert result.returncode == 0, report
    assert registry(game) == {"": b"personal default value"}
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0, winreg.KEY_ALL_ACCESS) as key:
        winreg.DeleteValue(key, "")
    winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, registry_key(game), winreg.KEY_WOW64_32KEY)


def test_foreign_journal_is_not_adopted_when_preparation_publishes(games):
    game = games()
    before = relevant(game)
    foreign = b"unrelated file placed at the future journal path"
    replaced = []

    def replace():
        directories = list(game.glob(".unofficial-medieval-patch-preparing-*"))
        assert len(directories) == 1
        path = directories[0] / "journal.json"
        assert not path.exists()
        path.write_bytes(foreign)
        replaced.append(path)

    code, report, output, error = run_at_wait("Install", game, "before-journal-write", replace)
    assert code == 2, (output, error)
    assert report["code"] == "file_changed", report
    assert replaced[0].read_bytes() == foreign
    assert relevant(game) == before
    assert not registry(game)


def test_v1_uncertain_wrapper_original_is_kept_for_restoration(games):
    """Synthetic uncertain v1 receipt: archive is not authority to discard an original."""
    game = games()
    installed(game)
    state = game / STATE
    original = b"unknown historical wrapper that must be preserved"
    (state / "originals").mkdir()
    (state / "originals/ddraw.dll").write_bytes(original)
    receipt = read_receipt(game)
    receipt["schema"] = "unofficial-medieval-total-war-patch-install-v1"
    receipt["preinstall_mode"] = "r185"
    receipt["files"]["ddraw.dll"].update(existed=True, original_sha256=hashlib.sha256(original).hexdigest().upper(),
                                           original_length=len(original), snapshot_relative="originals/ddraw.dll",
                                           sidecar_created=False, sidecar_relative="ddraw.dll.unofficial-patch.bak")
    (state / "install-manifest.json").write_text(json.dumps(receipt), encoding="utf-8-sig")
    (game / UNINSTALL).unlink()  # Model the legacy layout, which had no root uninstaller.
    result, report = run("Install", game)
    assert result.returncode == 0, report
    assert read_receipt(game)["files"]["ddraw.dll"]["existed"] is True
    assert (state / "originals/ddraw.dll").read_bytes() == original
    result, report = run("Restore", game)
    assert result.returncode == 0, report
    assert (game / "ddraw.dll").read_bytes() == original


@pytest.fixture
def legacy_v2():
    value = os.environ.get("MTW_LEGACY_V2_SOURCE")
    if not value:
        pytest.skip("MTW_LEGACY_V2_SOURCE must point to the saved, hash-bound PowerShell v2 source")
    root = Path(value)
    proof = json.loads(root.with_suffix(".json").read_text(encoding="utf-8-sig"))
    for name in ("install-engine.ps1", "lifecycle.ps1", "lifecycle-transaction.ps1", "lifecycle-registry.ps1", "lifecycle-native.cs"):
        assert digest(root / "src" / name) == proof["src/" + name]["sha256"]
    return root / "src/install-engine.ps1"


def old_run(engine: Path, operation: str, game: Path, fault=""):
    source = game.parent / "synthetic-old-v2-uninstaller.bin"
    source.write_bytes(b"old v2 removal package fixture")
    command = [str(Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"),
               "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(engine),
               "-Operation", operation, "-Target", str(game), "-PayloadDirectory", str(PAYLOAD),
               "-InstallerVersion", PRODUCT["version"], "-UninstallerSource", str(source)]
    environment = dict(os.environ)
    if fault:
        environment["MTW_ENABLE_LIFECYCLE_FAULTS"] = "1"
        (game / ".umtwp-test-fixture").write_text("disposable old v2 interruption fixture")
        command += ["-TestFault", fault]
    return subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90, env=environment)


def test_actual_powershell_v2_receipt_upgrades_and_restores_earliest_original(games, legacy_v2):
    game = games("Old v2 héros & 城")
    original = b"actual original before the PowerShell installation"
    (game / "dgVoodoo.conf").write_bytes(original)
    before = relevant(game)
    result = old_run(legacy_v2, "Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    old = read_receipt(game)
    # Exercise legitimate UTF-8 BOM loading while retaining the exact checksum.
    path = game / STATE / "install-manifest.json"
    path.write_text(path.read_text(encoding="utf-8-sig"), encoding="utf-8-sig")
    result, report = run("Install", game)
    assert result.returncode == 0, report
    new = read_receipt(game)
    assert new["installation_id"] == old["installation_id"]
    assert new["repair_count"] == old["repair_count"] + 1
    assert new["files"] == old["files"]
    result, report = run("Restore", game)
    assert result.returncode == 0, report
    assert relevant(game) == before


@pytest.mark.skipif(os.environ.get("MTW_RUN_LIFECYCLE_FAULTS") != "1", reason="enable lifecycle process-interruption tests")
@pytest.mark.parametrize("operation,point", [("Install", "after-file-0"), ("Install", "after-registry"),
                                               ("Restore", "after-file-0"), ("Restore", "after-registry")])
def test_native_recovers_actual_powershell_v2_journal(games, legacy_v2, operation, point):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"earliest personal configuration")
    before = relevant(game)
    if operation == "Restore":
        result = old_run(legacy_v2, "Install", game)
        assert result.returncode == 0, (result.stdout, result.stderr)
    result = old_run(legacy_v2, operation, game, "crash:" + point)
    assert result.returncode != 0
    assert (game / TX / "journal.json").is_file()
    result, report = run("Install", game)
    assert result.returncode == 0, report
    result, report = run("Restore", game)
    assert result.returncode == 0, report
    assert relevant(game) == before
