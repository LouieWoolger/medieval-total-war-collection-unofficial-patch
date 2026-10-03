"""Finite adversarial lifecycle regressions on disposable, supported game copies.

Set MTW_TEST_GAME_EXE to the supported executable. Set MTW_RUN_LIFECYCLE_FAULTS=1
to include child-process crash tests. All registrations use the imported fixture's
unique root-derived keys and verified exact-key cleanup.
"""
from __future__ import annotations

from contextlib import contextmanager
import copy
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import uuid
import winreg

import pytest

from test_lifecycle import (
    ENGINE, NAMES, STATE, TX, UNINSTALL, digest, games, installed, read_receipt,
    registry, registry_key, relevant, run, run_at_wait,
)


def ps_literal(value: str | Path) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def checked_json(path: Path, value: dict) -> None:
    """Reproduce the independently validated legacy checksum, not native validation."""
    from native_helper import seal
    for name in ("removal_receipt", "install_receipt"):
        if value.get(name):
            seal(value[name])
    seal(value)
    path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")


def tree_snapshot(root: Path) -> dict:
    """Do not traverse the very junctions/symlinks these tests are rejecting."""
    snapshot = {}

    def visit(directory: Path) -> None:
        for entry in os.scandir(directory):
            path = Path(entry.path)
            relative = str(path.relative_to(root))
            info = entry.stat(follow_symlinks=False)
            if info.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                snapshot[relative] = ("reparse", os.readlink(path))
            elif stat.S_ISDIR(info.st_mode):
                snapshot[relative] = ("directory",)
                visit(path)
            else:
                snapshot[relative] = ("file", info.st_size, digest(path))

    visit(root)
    return snapshot


def assert_refused_without_changes(game: Path, operation: str = "Restore") -> dict:
    before, entry = tree_snapshot(game), registry(game)
    result, report = run(operation, game)
    assert result.returncode != 0, (result.stdout, result.stderr)
    assert report.get("status") == "error", report
    assert report.get("code") in {
        "receipt_invalid", "unsafe_path", "installation_moved", "wrong_account",
        "registry_identity_conflict",
    }, report
    assert tree_snapshot(game) == before, report
    assert registry(game) == entry, report
    return report


RECEIPT_ATTACKS = [
    "snapshot_traversal", "missing_file", "invalid_guid",
    "negative_original_length", "string_boolean",
]


@pytest.mark.parametrize("attack", RECEIPT_ATTACKS)
def test_valid_checksum_receipt_rejects_unsafe_structure(games, attack):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"earliest private original")
    installed(game)
    receipt = read_receipt(game)
    if attack == "snapshot_traversal":
        receipt["files"]["dgVoodoo.conf"]["snapshot_relative"] = "../unrelated-mod.txt"
    elif attack == "missing_file":
        del receipt["files"]["D3D9.dll"]
    elif attack == "invalid_guid":
        receipt["installation_id"] = "not-an-installation-guid"
    elif attack == "negative_original_length":
        receipt["files"]["dgVoodoo.conf"]["original_length"] = -1
    elif attack == "string_boolean":
        receipt["files"]["dgVoodoo.conf"]["existed"] = "false"
    checked_json(game / STATE / "install-manifest.json", receipt)
    assert_refused_without_changes(game)


JOURNAL_ATTACKS = [
    "missing_runtime", "missing_registry", "missing_uninstaller", "missing_receipt",
    "duplicate_registry", "invalid_guid", "started_short", "bad_exists",
    "negative_length", "traversal", "removal_guid", "removal_target", "empty_actions",
]


@pytest.mark.parametrize("attack", JOURNAL_ATTACKS)
def test_valid_checksum_committed_journal_requires_complete_consistent_actions(games, attack):
    game = games()
    installed(game)
    result, report = run("Restore", game, fault="throw:cleanup-uninstaller")
    assert result.returncode != 0 and report["restoration"] == "verified", report
    journal_path = game / TX / "journal.json"
    journal = json.loads(journal_path.read_text(encoding="utf-8-sig"))
    assert journal["phase"] == "committed"
    receipt_relative = STATE + "/install-manifest.json"
    removal_selectors = {
        "missing_runtime": lambda a: a["kind"] == "file" and a["relative"] == NAMES[0],
        "missing_registry": lambda a: a["kind"] == "registry",
        "missing_uninstaller": lambda a: a["kind"] == "file" and a["relative"] == UNINSTALL,
        "missing_receipt": lambda a: a["kind"] == "file" and a["relative"] == receipt_relative,
    }
    if attack in removal_selectors:
        selected = removal_selectors[attack]
        assert sum(bool(selected(a)) for a in journal["actions"]) == 1
        journal["actions"] = [a for a in journal["actions"] if not selected(a)]
        journal["started"] = len(journal["actions"])
    elif attack == "duplicate_registry":
        action = next(a for a in journal["actions"] if a["kind"] == "registry")
        journal["actions"].append(copy.deepcopy(action))
        journal["started"] = len(journal["actions"])
    elif attack == "invalid_guid":
        journal["installation_id"] = "not-an-installation-guid"
    elif attack == "started_short":
        journal["started"] = len(journal["actions"]) - 1
    elif attack == "bad_exists":
        journal["actions"][0]["after"]["exists"] = "false"
    elif attack == "negative_length":
        journal["actions"][0]["after"]["length"] = -1
    elif attack == "traversal":
        journal["actions"][0]["relative"] = "../unrelated-mod.txt"
    elif attack == "removal_guid":
        journal["removal_receipt"]["installation_id"] = str(uuid.uuid4())
    elif attack == "removal_target":
        journal["removal_receipt"]["target_directory"] = str(game.parent / "Different Game")
    elif attack == "empty_actions":
        journal["actions"] = []
        journal["started"] = 0
    checked_json(journal_path, journal)
    assert_refused_without_changes(game)


def make_junction(link: Path, target: Path) -> None:
    command = (
        "$ErrorActionPreference='Stop'; New-Item -ItemType Junction "
        f"-Path {ps_literal(link)} -Target {ps_literal(target)} | Out-Null"
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, (result.stdout, result.stderr)


@pytest.mark.parametrize("location", ["runtime", "original"])
@pytest.mark.parametrize("link_type", ["hardlink", "symlink"])
def test_runtime_and_original_links_cannot_redirect_removal(games, location, link_type):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"untouched original config")
    installed(game)
    leaf = game / "dgVoodoo.conf" if location == "runtime" else game / STATE / "originals/dgVoodoo.conf"
    expected = leaf.read_bytes()
    outside = game.parent / "outside-file.bin"
    outside.write_bytes(expected)
    leaf.unlink()
    if link_type == "hardlink":
        os.link(outside, leaf)
    else:
        try:
            leaf.symlink_to(outside)
        except OSError as exc:
            if exc.winerror == 1314:
                pytest.skip("file symlink creation requires Developer Mode or symlink privilege")
            raise
    assert_refused_without_changes(game)
    assert outside.read_bytes() == expected


@pytest.mark.parametrize("location", ["state", "transaction"])
def test_junction_state_or_transaction_cannot_redirect_recovery(games, location):
    game = games()
    installed(game)
    if location == "transaction":
        result, report = run("Restore", game, fault="throw:cleanup-uninstaller")
        assert result.returncode != 0 and report["restoration"] == "verified", report
    folder = game / (STATE if location == "state" else TX)
    outside = game.parent / "outside-directory"
    folder.rename(outside)
    outside_before = tree_snapshot(outside)
    make_junction(folder, outside)
    try:
        assert_refused_without_changes(game)
        assert tree_snapshot(outside) == outside_before
    finally:
        # Removing this known junction does not recurse into its target.
        assert folder.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT
        os.rmdir(folder)
        outside.rename(folder)


@contextmanager
def no_delete_handle(path: Path):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield
    finally:
        assert kernel.CloseHandle(handle)


def test_locked_runtime_file_rolls_back_then_removal_retries(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"prepatch config")
    baseline = relevant(game)
    installed(game)
    before, entry, receipt = relevant(game), registry(game), read_receipt(game)
    with no_delete_handle(game / "D3D9.dll"):
        result, report = run("Restore", game)
        assert result.returncode != 0, (result.stdout, result.stderr)
        assert report["rollback"] == "verified", report
        assert relevant(game) == before
        assert registry(game) == entry
        assert read_receipt(game) == receipt
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant(game) == baseline
    assert not registry(game)


def test_second_operation_cannot_mutate_a_folder_with_active_lifecycle_mutex(games):
    game = games()
    installed(game)
    second = {}

    def competing_restore():
        before, entry = relevant(game), registry(game)
        result, report = run("Restore", game)
        second.update(code=result.returncode, report=report)
        assert result.returncode != 0 and report["code"] == "operation_in_progress", report
        assert relevant(game) == before
        assert registry(game) == entry

    code, report, out, err = run_at_wait("Install", game, "before-stage-uninstaller", competing_restore)
    assert second, "The real engine never reached the contention check"
    assert code == 0, (report, out, err)
    assert run("Verify", game)[0].returncode == 0


def test_missing_registration_can_be_repaired_without_changing_originals(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"old original")
    installed(game)
    receipt = read_receipt(game)
    original_hash = digest(game / STATE / "originals/dgVoodoo.conf")
    values = registry(game)
    assert values["InstallLocation"] == str(game)
    assert values["UninstallString"] == f'"{game / UNINSTALL}"'
    winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, registry_key(game), winreg.KEY_WOW64_32KEY)
    before = tree_snapshot(game)
    result, report = run("Verify", game)
    assert result.returncode != 0 and report["code"] == "verification_failed", report
    assert tree_snapshot(game) == before
    installed(game)
    assert registry(game)["InstallationId"] == receipt["installation_id"]
    assert digest(game / STATE / "originals/dgVoodoo.conf") == original_hash


def test_stale_registration_identity_is_preserved_by_install_and_removal(games):
    game = games()
    installed(game)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0,
                       winreg.KEY_SET_VALUE | winreg.KEY_WOW64_32KEY) as key:
        winreg.SetValueEx(key, "InstallationId", 0, winreg.REG_SZ, str(uuid.uuid4()))
    assert_refused_without_changes(game, "Install")
    assert_refused_without_changes(game, "Restore")


def test_unknown_state_survives_removal_and_reinstallation(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"old user config")
    installed(game)
    note = game / STATE / "personal-notes.txt"
    note.write_bytes(b"unrelated notes must survive two lifecycle generations")
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert note.read_bytes() == b"unrelated notes must survive two lifecycle generations"
    installed(game)
    assert note.read_bytes() == b"unrelated notes must survive two lifecycle generations"
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert note.read_bytes() == b"unrelated notes must survive two lifecycle generations"
    assert (game / "dgVoodoo.conf").read_bytes() == b"old user config"


def test_retained_unknown_original_path_cannot_be_adopted_by_reinstallation(games):
    game = games()
    installed(game)
    assert read_receipt(game)["files"]["dgVoodoo.conf"]["existed"] is False
    unknown = game / STATE / "originals/dgVoodoo.conf"
    unknown.parent.mkdir(parents=True, exist_ok=True)
    unknown_bytes = b"unknown private-directory content from another tool"
    unknown.write_bytes(unknown_bytes)
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert unknown.read_bytes() == unknown_bytes
    # The new generation needs this same path for a different original.
    new_baseline = b"new live user configuration"
    (game / "dgVoodoo.conf").write_bytes(new_baseline)
    before = tree_snapshot(game)
    result, report = run("Install", game)
    if result.returncode != 0:
        assert tree_snapshot(game) == before
        assert not registry(game)
        return
    assert report["recovery_archive"], "Successful reuse must first preserve the unknown destination"
    archive = Path(report["recovery_archive"])
    preserved = archive / "previous-state-dgVoodoo.conf"
    assert preserved.read_bytes() == unknown_bytes
    manifest = json.loads((archive / "manifest.json").read_text(encoding="utf-8-sig"))
    descriptor = next(record for record in manifest["files"] if record["file"] == preserved.name)
    assert descriptor["sha256"] == hashlib.sha256(unknown_bytes).hexdigest().upper()
    assert descriptor["length"] == len(unknown_bytes)
    assert unknown.read_bytes() == new_baseline
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert (game / "dgVoodoo.conf").read_bytes() == new_baseline
    assert preserved.read_bytes() == unknown_bytes


@pytest.mark.skipif(os.environ.get("MTW_RUN_LIFECYCLE_FAULTS") != "1",
                    reason="set MTW_RUN_LIFECYCLE_FAULTS=1 for bounded child-process crashes")
@pytest.mark.parametrize("point", ["after-file-0", "after-registry", "before-commit"])
def test_removal_crash_recovers_exact_baseline_on_next_removal(games, point):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"exact original config before any patch")
    baseline = relevant(game)
    installed(game)
    failed, _ = run("Restore", game, fault="crash:" + point)
    assert failed.returncode != 0
    assert (game / TX / "journal.json").is_file()
    result, report = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant(game) == baseline
    assert not registry(game)
    assert not (game / UNINSTALL).exists()
    assert not (game / TX).exists()
