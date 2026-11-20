"""Real Windows ACL regressions; every target and registry key is disposable."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import uuid

import pytest

from test_lifecycle import ENGINE, STATE, TX, UNINSTALL, games, installed, registry, relevant, run
from test_lifecycle_adversarial import tree_snapshot
from windows_acl import deny_directory_right


def assert_creation_denied(root: Path) -> None:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    probe = root / ("acl-probe-" + uuid.uuid4().hex)
    handle = kernel.CreateFileW(str(probe), 0x40000000, 0, None, 1, 0x80, None)
    if handle != ctypes.c_void_p(-1).value:
        kernel.CloseHandle(handle)
        probe.unlink()
        pytest.fail("The real ACL did not deny CreateFileW(GENERIC_WRITE, CREATE_NEW)")
    assert ctypes.get_last_error() == 5  # ERROR_ACCESS_DENIED, not a sharing violation.


@pytest.mark.parametrize("operation", ["Install", "Restore"])
def test_denied_root_creation_refuses_before_mutation_and_retries(games, operation):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"original user display configuration\r\n")
    baseline = relevant(game)
    if operation == "Restore":
        installed(game)
    before, entry = tree_snapshot(game), registry(game)

    with deny_directory_right(game, "CreateFiles"):
        assert_creation_denied(game)
        result, report = run(operation, game)
        assert result.returncode != 0, report
        assert report["code"] == "target_not_writable", report
        assert tree_snapshot(game) == before
        assert registry(game) == entry

    if operation == "Install":
        installed(game)
    result, report = run("Restore", game)
    assert result.returncode == 0 and report["restoration"] == "verified", report
    assert relevant(game) == baseline
    assert not registry(game)
    assert not any((game / name).exists() for name in (STATE, TX, UNINSTALL))


def test_committed_removal_cleanup_retry_does_not_require_root_creation(games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"original display configuration\r\n")
    baseline = relevant(game)
    installed(game)
    result, report = run("Restore", game, fault="throw:cleanup-uninstaller")
    assert result.returncode != 0 and report["code"] == "cleanup_pending", report
    assert report["restoration"] == "verified"
    assert json.loads((game / TX / "journal.json").read_text(encoding="utf-8-sig"))["phase"] == "committed"
    assert (game / UNINSTALL).is_file()

    with deny_directory_right(game, "CreateFiles"):
        assert_creation_denied(game)
        result, report = run("Restore", game)
        assert result.returncode == 0 and report["restoration"] == "verified", report
        assert relevant(game) == baseline
        assert not registry(game)
        assert not any((game / name).exists() for name in (STATE, TX, UNINSTALL))


def test_space_preflight_rejects_oversized_request_without_allocating_disk(games):
    """Run the real native preflight with a gated impossible size; allocate no payload."""
    game = games()
    (game / ".umtwp-test-fixture").write_text("disposable lifecycle test\n")
    before = tree_snapshot(game)
    result, report = run("Install", game, fault="space:maximum")
    assert result.returncode == 2, (result.stdout, result.stderr)
    assert report["code"] == "insufficient_space", report
    assert tree_snapshot(game) == before
    assert not registry(game)
