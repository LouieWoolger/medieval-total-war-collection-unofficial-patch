"""Native migration and preservation regressions on disposable game fixtures."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import winreg

import pytest

from native_helper import canonical_json, seal
from test_lifecycle import (
    ENGINE, PAYLOAD, PRODUCT, STATE, TX, UNINSTALL, FIXED_NAMES, digest, games, installed,
    read_receipt, registry, registry_key, relevant, run, run_at_wait,
)
from test_lifecycle_adversarial import tree_snapshot


def test_scroll_only_fault_before_exe_stage_restores_original(games):
    game = games()
    before = relevant(game)
    result, report = run("Install", game, fault="throw:before-stage-exe",
                         terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert result.returncode != 0, (result.stdout, result.stderr)
    assert report["rollback"] in {"not-needed", "verified"}, report
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not registry(game)


def test_scroll_only_fault_after_exe_publication_rolls_back(games):
    game = games()
    before = relevant(game)
    # File actions are original EXE snapshot, five absent runtime paths, then
    # the EXE replacement. This interruption occurs after the replacement.
    result, report = run("Install", game, fault="throw:after-file-6",
                         terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert result.returncode != 0, (result.stdout, result.stderr)
    assert report["rollback"] == "verified", report
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not registry(game)


@pytest.mark.parametrize("operation", ["Install", "Restore"])
def test_concurrent_executable_replacement_is_detected_and_preserved(games, operation):
    game = games()
    if operation == "Restore":
        installed(game)
    before = relevant(game)
    executable = game / "Medieval_TW.exe"
    replacement = b"unsupported concurrent replacement"

    def try_update():
        executable.write_bytes(replacement)

    code, report, output, error = run_at_wait(operation, game, "after-preservation", try_update)
    assert code != 0, (report, output, error)
    assert report["code"] in {"concurrent_change", "unsupported_executable"}
    assert executable.read_bytes() == replacement
    after = relevant(game)
    assert {k: v for k, v in after.items() if k != "Medieval_TW.exe"} == {
        k: v for k, v in before.items() if k != "Medieval_TW.exe"}


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


def test_changed_unpublished_copy_is_retained_for_review(games):
    game = games("Changed preparation")
    before = relevant(game)
    altered = []
    source = game.parent / "synthetic-uninstaller.bin"
    source.write_bytes(b"synthetic uninstaller")

    def change_staged_copy():
        preparing = list(game.glob(".unofficial-medieval-patch-preparing-*"))
        assert len(preparing) == 1
        copies = [path for path in (preparing[0] / "after").glob("*.bin")
                  if digest(path) == FIXED_NAMES]
        assert len(copies) == 1
        copies[0].write_bytes(b"foreign staged content")
        altered.append(copies[0])
        # Fail before publication so this specifically exercises retirement
        # of an unpublished preparation with a changed transaction copy.
        source.unlink()

    code, report, output, error = run_at_wait("Install", game, "before-stage-uninstaller",
                                              change_staged_copy, uninstaller=source)
    assert code != 0, (output, error)
    assert report["rollback"] == "incomplete", report
    assert altered[0].read_bytes() == b"foreign staged content"
    assert relevant(game) == before
    assert not registry(game)


def test_changed_staged_copy_detected_before_transaction_publication(games, tmp_path):
    # Keep the preparation directory beyond legacy MAX_PATH even when pytest's
    # temporary root is short; cleanup must inspect this exact retained copy.
    prefix = "Changed-copy-before-publish-"
    game = games(prefix + "x" * max(0, 190 - len(str(tmp_path)) - 1 - len(prefix)))
    assert len(str(game)) >= 190
    before = relevant(game)
    altered = []

    def change_staged_copy():
        preparing = list(game.glob(".unofficial-medieval-patch-preparing-*"))
        assert len(preparing) == 1
        copies = [path for path in (preparing[0] / "after").glob("*.bin")
                  if digest(path) == FIXED_NAMES]
        assert len(copies) == 1
        copies[0].write_bytes(b"foreign staged content")
        altered.append(copies[0])

    code, report, output, error = run_at_wait("Install", game, "before-stage-uninstaller",
                                              change_staged_copy)
    assert code != 0, (output, error)
    assert report["rollback"] == "incomplete", report
    assert altered[0].read_bytes() == b"foreign staged content"
    assert relevant(game) == before
    assert not registry(game)


def test_v1_uncertain_wrapper_original_is_kept_for_restoration(games):
    """Synthetic uncertain v1 receipt: archive is not authority to discard an original."""
    game = games()
    installed(game)
    state = game / STATE
    original = b"unknown historical wrapper that must be preserved"
    (state / "originals").mkdir(exist_ok=True)
    (state / "originals/ddraw.dll").write_bytes(original)
    receipt = read_receipt(game)
    # A v1 installation predates both direct EXE fixes. Restore its stock
    # executable before synthesizing that historical receipt.
    shutil.copy2(state / "originals/Medieval_TW.exe", game / "Medieval_TW.exe")
    data_originals = {}
    for relative, snapshot in (
        ("campmap/names/DEFAULT_HEROES.TXT", "DEFAULT_HEROES.TXT"),
        ("Loc/Eng/NAMES.TXT", "NAMES.TXT"),
        ("Loc/Eng/DEFAULT_REGION_SPECIFIC.TXT", "DEFAULT_REGION_SPECIFIC.TXT"),
    ):
        shutil.copy2(state / "originals" / snapshot, game / relative)
        data_originals[relative] = digest(game / relative)
        receipt["files"].pop(relative)
    receipt["schema"] = "unofficial-medieval-total-war-patch-install-v1"
    receipt["files"].pop("Medieval_TW.exe")
    receipt["files"].pop("Loc/Eng/CHANGES.TXT")
    receipt.pop("fast_moves_fix_enabled", None)
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
    assert {relative: digest(game / relative) for relative in data_originals} == data_originals


def test_actual_v3_receipt_uninstalls_directly_with_new_helper(games):
    """A v3 owner can remove the patch without first upgrading to v4."""
    source_name = os.environ.get("MTW_LEGACY_V3_INSTALLER")
    if not source_name:
        pytest.skip("MTW_LEGACY_V3_INSTALLER is required for actual v3 package proof")
    old_installer = Path(source_name)
    assert digest(old_installer) == "1E0F95073D65FD2B9A74FEC9489352DACC5F18AE9E4138CA81D7E930055FA9D3"
    game = games("Direct v3 removal")
    before = relevant(game)
    local_installer = game / old_installer.name
    shutil.copy2(old_installer, local_installer)
    old_result = subprocess.run(
        [str(local_installer), "/S", "/TERRAINFIX=1", "/SCROLLFIX=1", "/SPRITEFIX=1"],
        cwd=game, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
    )
    assert old_result.returncode == 0, (old_result.stdout, old_result.stderr)
    assert read_receipt(game)["schema"] == "unofficial-medieval-total-war-patch-install-v3"
    assert digest(game / "Medieval_TW.exe") == "50829CD084355D81EC94D6F4489D1F60E2EF7FA92983D0EAD07D43832DEEF15B"
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr, report)
    assert relevant(game) == before
    assert not (game / UNINSTALL).exists()
    assert not (game / STATE / "install-manifest.json").exists()
    assert not registry(game)


@pytest.fixture
def legacy_v5_installer() -> Path:
    source_name = os.environ.get("MTW_LEGACY_V5_INSTALLER")
    if not source_name:
        pytest.skip("MTW_LEGACY_V5_INSTALLER is required for published v5 package proof")
    installer = Path(source_name)
    assert digest(installer) == "0DED51E9CEA667801E9B8656610F4EFF843C70CBCA49276D5FD59D3A0D1FE33E"
    return installer


def install_published_v5(game: Path, installer: Path) -> None:
    local = game / installer.name
    shutil.copy2(installer, local)
    result = subprocess.run(
        [str(local), "/S", "/TERRAINFIX=0", "/SCROLLFIX=1", "/SPRITEFIX=1", "/FASTMOVESFIX=0"],
        cwd=game, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
    )
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert read_receipt(game)["schema"] == "unofficial-medieval-total-war-patch-install-v5"


def test_published_v5_receipt_uninstalls_directly_with_new_helper(games, legacy_v5_installer):
    game = games("Direct v5 removal")
    before = relevant(game)
    install_published_v5(game, legacy_v5_installer)
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr, report)
    assert relevant(game) == before
    assert not (game / STATE / "install-manifest.json").exists()
    assert not registry(game)


def test_published_v5_upgrade_preserves_partial_names_baseline(games, legacy_v5_installer):
    game = games("Partial v5 upgrade")
    names = game / "Loc/Eng/NAMES.TXT"
    names.write_bytes(names.read_bytes().replace(b'{"Kilij Arslan}', b'{"Kilij Arslan"}'))
    before = relevant(game)
    install_published_v5(game, legacy_v5_installer)
    result, report = run("Install", game, terrain_fix=False, scroll_fix=True, sprite_fix=True)
    assert result.returncode == 0, (result.stdout, result.stderr, report)
    receipt = read_receipt(game)
    assert receipt["schema"] == "unofficial-medieval-total-war-patch-install-v8"
    assert receipt["files"]["Loc/Eng/NAMES.TXT"]["original_sha256"] == before["Loc/Eng/NAMES.TXT"]
    assert digest(names) == "3421E5FE033FF3CD9D710FFA38F465B970060C4095C56B590C22AEDFEF4A6F15"
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr, report)
    assert relevant(game) == before


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
