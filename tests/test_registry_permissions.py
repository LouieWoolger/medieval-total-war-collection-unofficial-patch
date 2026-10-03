"""Real Windows registry ACL failures, confined to sentinel-owned test keys."""
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid
import winreg

import pytest

from registry_isolation import LEGACY_NAMES, UNINSTALL_PARENT
from test_c_backend_lifecycle import build_c_engine
from test_c_backend_state import state_exe, invoke, isolated_registry
from test_legacy_migration import sandbox_snapshot, delete_sandbox
from test_lifecycle import games, PAYLOAD, PRODUCT, STATE, TX, UNINSTALL, digest
from test_lifecycle_adversarial import tree_snapshot
from test_lifecycle_permissions import powershell, ps_literal


@contextmanager
def deny_registry_right(path, rights=winreg.KEY_SET_VALUE):
    assert re.match(r"Software\\MedievalPatchLifecycleTests\\[0-9a-f]{32}\\", path)
    api = ctypes.WinDLL("advapi32")
    api.RegGetKeySecurity.argtypes = [wintypes.HKEY, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
    api.RegSetKeySecurity.argtypes = [wintypes.HKEY, wintypes.DWORD, ctypes.c_void_p]
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, 0x20000 | 0x40000 | winreg.KEY_WOW64_64KEY) as key:
        size = wintypes.DWORD()
        assert api.RegGetKeySecurity(int(key), 4, None, ctypes.byref(size)) == 122
        original = ctypes.create_string_buffer(size.value)
        assert api.RegGetKeySecurity(int(key), 4, original, ctypes.byref(size)) == 0
        try:
            powershell(
                "$ErrorActionPreference='Stop'\n"
                "$base=[Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser','Registry64')\n"
                f"$key=$base.OpenSubKey({ps_literal(path)},[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,[Security.AccessControl.RegistryRights]::ChangePermissions -bor [Security.AccessControl.RegistryRights]::ReadPermissions)\n"
                "$acl=$key.GetAccessControl()\n"
                "$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User\n"
                f"$rule=New-Object Security.AccessControl.RegistryAccessRule($sid,[Security.AccessControl.RegistryRights]{rights},[Security.AccessControl.AccessControlType]::Deny)\n"
                "$acl.AddAccessRule($rule)\n$key.SetAccessControl($acl)\n$key.Dispose()\n$base.Dispose()\n"
            )
            with pytest.raises(PermissionError):
                winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, rights | winreg.KEY_WOW64_64KEY)
            yield
        finally:
            assert api.RegSetKeySecurity(int(key), 4, original) == 0


def test_unchanged_readonly_registration_needs_no_write_access(state_exe, tmp_path, isolated_registry):
    namespace, path, create = isolated_registry
    with create(path()) as key:
        winreg.SetValueEx(key, "DisplayName", 0, winreg.REG_SZ, "Unchanged patch")
    before = invoke(state_exe, ["--live-read", namespace, "Registry32", "modern"])
    assert before.returncode == 0
    wanted = tmp_path / "unchanged.json"
    wanted.write_bytes(before.stdout)
    with deny_registry_right(path()):
        result = invoke(state_exe, ["--live-set", namespace, "Registry32", "modern", wanted])
        assert result.returncode == 0, result.stdout
    assert invoke(state_exe, ["--live-read", namespace, "Registry32", "modern"]).stdout == before.stdout


def test_existing_registration_does_not_need_subkey_creation_right(state_exe, tmp_path, isolated_registry):
    namespace, path, create = isolated_registry
    with create(path()) as key:
        winreg.SetValueEx(key, "DisplayName", 0, winreg.REG_SZ, "Old patch")
    wanted = tmp_path / "changed.json"
    wanted.write_text(json.dumps({"exists": True, "subkeys": 0, "values": [
        {"name": "DisplayName", "kind": "String", "value": "Updated patch"}]}))
    with deny_registry_right(path(), winreg.KEY_CREATE_SUB_KEY):
        result = invoke(state_exe, ["--live-set", namespace, "Registry32", "modern", wanted])
        assert result.returncode == 0, result.stdout
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path()) as key:
        assert winreg.QueryValueEx(key, "DisplayName")[0] == "Updated patch"


@pytest.fixture(scope="module")
def registry_engine(tmp_path_factory):
    return build_c_engine(tmp_path_factory.mktemp("registry-acl-engine") / "helper.exe", isolated=True)


@pytest.fixture
def legacy_registration(games):
    game = games("Legacy GOG ü copy")
    manifest = json.loads((PAYLOAD / "payload-manifest.json").read_text())
    files = {}
    for name in manifest["files"]:
        shutil.copy2(PAYLOAD / name, game / name)
        files[name] = {"existed": False, "original_sha256": None, "original_length": None,
                       "snapshot_relative": None, "sidecar_relative": None, "sidecar_created": False,
                       "installed_sha256": digest(game / name), "installed_length": (game / name).stat().st_size}
    receipt = {"schema": "unofficial-medieval-total-war-patch-install-v1", "status": "installed",
               "installation_id": str(uuid.uuid4()), "target_directory": str(game),
               "target_executable_sha256": digest(game / "Medieval_TW.exe"), "installer_version": "1.0.0",
               "preinstall_mode": "clean", "files": files, "installed_utc": "2026-07-23T11:49:20Z",
               "repair_count": 11}
    (game / STATE).mkdir()
    (game / STATE / "install-manifest.json").write_text(json.dumps(receipt), encoding="utf-8")
    namespace = "Software\\MedievalPatchLifecycleTests\\" + uuid.uuid4().hex
    try:
        for label in ("HKCU32", "HKCU64", "HKLM32", "HKLM64"):
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, namespace + "\\" + label, 0,
                                   winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY) as root:
                winreg.SetValueEx(root, "IsolationSentinel", 0, winreg.REG_SZ, namespace.rsplit("\\", 1)[1])
        yield game, namespace
    finally:
        delete_sandbox(namespace)


def make_entry(namespace, game, label="HKLM32", name=LEGACY_NAMES[1]):
    path = namespace + "\\" + label + "\\" + UNINSTALL_PARENT + "\\" + name
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY) as key:
        for field, value in {"DisplayName": "Unofficial Medieval: Total War Patch", "Publisher": "Louie Woolger",
                             "InstallLocation": str(game), "UninstallString": f'"{game / STATE / "Uninstall.exe"}"'}.items():
            winreg.SetValueEx(key, field, 0, winreg.REG_SZ, value)
    return path


def engine_command(helper, game, namespace, operation="Install"):
    remover = game.parent / "remover.bin"
    remover.write_bytes(b"synthetic uninstaller; real package tested separately")
    return [str(helper), namespace, "--operation", operation, "--target", str(game), "--payload", str(PAYLOAD),
            "--version", PRODUCT["version"], "--uninstaller", str(remover)]


@pytest.mark.parametrize("view", ["HKLM32", "HKLM64"])
def test_denied_legacy_registration_stops_before_game_mutation(registry_engine, legacy_registration, view):
    game, namespace = legacy_registration
    path = make_entry(namespace, game, view)
    before, registrations = tree_snapshot(game), sandbox_snapshot(namespace)
    with deny_registry_right(path):
        result = subprocess.run(engine_command(registry_engine, game, namespace), capture_output=True, text=True, encoding="utf-8", timeout=45)
        report = json.loads(result.stdout)
        assert report["code"] == "elevation_required", report
        assert result.returncode == 740
        assert tree_snapshot(game) == before
        assert sandbox_snapshot(namespace) == registrations
    result = subprocess.run(engine_command(registry_engine, game, namespace), capture_output=True, text=True, encoding="utf-8", timeout=45)
    assert result.returncode == 0, result.stdout
    assert not (game / TX).exists()
    assert (game / UNINSTALL).is_file()


def test_permission_revoked_after_preflight_rolls_back_unchanged_machine_entry(registry_engine, legacy_registration):
    game, namespace = legacy_registration
    path = make_entry(namespace, game)
    (game / ".umtwp-test-fixture").write_text("disposable registry ACL test")
    before = {name: value for name, value in tree_snapshot(game).items() if not name.startswith(".umtwp-")}
    registrations = sandbox_snapshot(namespace)
    command = engine_command(registry_engine, game, namespace) + ["--test-fault", "wait:before-stage-uninstaller"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
                               env=dict(os.environ, MTW_ENABLE_LIFECYCLE_FAULTS="1"))
    try:
        ready = game / ".umtwp-fault-waiting"
        deadline = time.monotonic() + 30
        while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        assert ready.exists(), process.communicate(timeout=1)
        with deny_registry_right(path):
            ready.unlink()
            out, err = process.communicate(timeout=45)
            report = json.loads(out)
            assert process.returncode == 2 and report["rollback"] == "verified", (out, err)
            assert not (game / TX).exists()
            for name, record in before.items():
                assert tree_snapshot(game)[name] == record
            assert not (game / UNINSTALL).exists()
            # Empty retired containers are deliberately retained; values must be unchanged.
            assert sandbox_snapshot(namespace)["HKLM32"] == registrations["HKLM32"]
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()


@pytest.mark.parametrize("same_account", [False, True])
def test_elevation_handoff_cannot_switch_installation_owner(registry_engine, legacy_registration, same_account):
    game, namespace = legacy_registration
    before = tree_snapshot(game)
    sid = powershell("[Security.Principal.WindowsIdentity]::GetCurrent().User.Value") if same_account else "S-1-5-18"
    result = subprocess.run(engine_command(registry_engine, game, namespace) + ["--require-owner", sid],
                            capture_output=True, text=True, encoding="utf-8", timeout=45)
    report = json.loads(result.stdout)
    if same_account:
        assert result.returncode == 0, report
    else:
        assert report["code"] == "wrong_account", report
        assert tree_snapshot(game) == before
