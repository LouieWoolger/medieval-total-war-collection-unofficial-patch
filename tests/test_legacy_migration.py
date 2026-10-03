"""Opt-in package migration tests; historical installer uses a UUID test key only."""
from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid
import winreg

import pytest

import test_compiled_installer as packaged
from registry_isolation import (LEGACY_NAMES, UNINSTALL_PARENT, read_registration,
                                remove_test_registration, registration_name)


pytestmark = pytest.mark.skipif(
    os.environ.get("MTW_RUN_LEGACY_MIGRATION_TESTS") != "1",
    reason="set MTW_RUN_LEGACY_MIGRATION_TESTS=1 with isolated historical package proof",
)


@pytest.fixture
def legacy_package():
    proof = json.loads(Path(os.environ["MTW_LEGACY_PACKAGE_PROOF"]).read_text())
    assert proof["historical_commit"] == "1d24a75"
    assert re.fullmatch(r"MedievalLegacyTest-[0-9a-f]{32}", proof["test_registry_name"])
    assert proof["sole_source_substitution"]["count"] == 11
    installer = Path(proof["installer"])
    assert packaged.sha256(installer) == proof["installer_sha256"]
    source = installer.parents[1]
    for relative, digest in proof["original_source_sha256"].items():
        path = source / relative
        if relative == "installer.nsi":
            data = path.read_bytes()
            change = proof["sole_source_substitution"]
            assert packaged.sha256(path) == proof["patched_installer_nsi_sha256"]
            assert data.count(change["new"].encode()) == 11
            assert change["old"].encode() not in data
            assert hashlib.sha256(data.replace(change["new"].encode(), change["old"].encode())).hexdigest().upper() == digest
        else:
            assert packaged.sha256(path) == digest, relative
    assert read_registration(proof["test_registry_name"]) is None
    return proof


@pytest.fixture(autouse=True)
def registration_guard(tmp_path: Path):
    before = real_fixed_registrations()
    yield
    for game in tmp_path.iterdir():
        if game.is_dir() and (game / "Medieval_TW.exe").is_file():
            remove_test_registration(game, tmp_path)
    assert real_fixed_registrations() == before


def delete_test_legacy_key(proof: dict, game: Path) -> None:
    name = proof["test_registry_name"]
    assert re.fullmatch(r"MedievalLegacyTest-[0-9a-f]{32}", name)
    current = read_registration(name)
    if current is None:
        return
    assert Path(current["InstallLocation"][0]).resolve() == game.resolve()
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_PARENT, 0,
                        winreg.KEY_WRITE | winreg.KEY_WOW64_32KEY) as parent:
        winreg.DeleteKey(parent, name)


def current_operation(game: Path, operation: str):
    """Use the actual C package, including its generated root uninstaller."""
    return packaged.run_installer(game) if operation == "Install" else packaged.run_uninstaller(game)


def test_actual_historical_package_retains_earliest_original_then_removes(tmp_path: Path, legacy_package):
    game = packaged.new_game(tmp_path, "Historical package migration")
    custom = b"; earliest personal configuration before version 1.0.0\r\n"
    (game / "dgVoodoo.conf").write_bytes(custom)
    before = packaged.snapshot(game)
    setup = game / packaged.DIST_INSTALLER.name
    shutil.copy2(legacy_package["installer"], setup)
    try:
        old = subprocess.run([str(setup), "/S"], cwd=game, capture_output=True, timeout=180)
        assert old.returncode == 0
        state = game / ".unofficial-medieval-total-war-patch"
        old_receipt = json.loads((state / "install-manifest.json").read_text(encoding="utf-8-sig"))
        old_entry = read_registration(legacy_package["test_registry_name"])
        assert old_entry is not None
        assert (state / "Uninstall.exe").is_file()
        assert (state / "originals/dgVoodoo.conf").read_bytes() == custom
        shutil.copy2(packaged.DIST_INSTALLER, setup)
        upgraded = current_operation(game, "Install")
        assert upgraded.returncode == 0, upgraded.stdout
        receipt = json.loads((state / "install-manifest.json").read_text(encoding="utf-8-sig"))
        assert receipt["installation_id"] == old_receipt["installation_id"]
        assert receipt["schema"] != old_receipt["schema"]
        assert (state / "originals/dgVoodoo.conf").read_bytes() == custom
        assert not (state / "Uninstall.exe").exists()
        assert len(list(game.glob(".medieval-recovery-*/legacy-uninstaller.exe"))) == 1
        assert read_registration(legacy_package["test_registry_name"]) == old_entry
        assert current_operation(game, "Restore").returncode == 0
        assert packaged.snapshot(game) == before
        assert not state.exists()
    finally:
        delete_test_legacy_key(legacy_package, game)


def test_primary_copied_metadata_and_runtime_migrates_without_retargeting(tmp_path: Path):
    source = Path(os.environ["MTW_PRIMARY_LEGACY_FIXTURE"])
    game = packaged.new_game(tmp_path, "Copied primary fixture")
    for name in packaged.RUNTIME_NAMES:
        shutil.copy2(source / name, game / name)
        assert packaged.sha256(game / name) == packaged.sha256(source / name)
    state_name = ".unofficial-medieval-total-war-patch"
    shutil.copytree(source / state_name, game / state_name)
    old = json.loads((game / state_name / "install-manifest.json").read_text(encoding="utf-8-sig"))
    assert old["schema"] == "unofficial-medieval-total-war-patch-install-v1"
    assert all(not record["existed"] for record in old["files"].values())
    result = current_operation(game, "Install")
    assert result.returncode == 0, result.stdout
    new = json.loads((game / state_name / "install-manifest.json").read_text(encoding="utf-8-sig"))
    assert new["installation_id"] != old["installation_id"]
    packaged.assert_registration(game)
    assert not (game / state_name / "Uninstall.exe").exists()
    assert current_operation(game, "Restore").returncode == 0
    assert all(not (game / name).exists() for name in packaged.RUNTIME_NAMES)
    assert packaged.sha256(game / "Medieval_TW.exe") == packaged.SUPPORTED_EXE_HASH


def real_fixed_registrations() -> dict:
    def contents(key):
        subkeys, values, _ = winreg.QueryInfoKey(key)
        result = {"values": [winreg.EnumValue(key, i) for i in range(values)], "children": {}}
        for i in range(subkeys):
            name = winreg.EnumKey(key, i)
            with winreg.OpenKey(key, name) as child:
                result["children"][name] = contents(child)
        return result

    snapshots = {}
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            for name in LEGACY_NAMES:
                identity = (int(hive), view, name)
                try:
                    with winreg.OpenKey(hive, UNINSTALL_PARENT + "\\" + name, 0, winreg.KEY_READ | view) as key:
                        snapshots[identity] = contents(key)
                except FileNotFoundError:
                    snapshots[identity] = None
    return snapshots


@pytest.fixture(scope="module")
def native_registry_driver(tmp_path_factory):
    from test_c_backend_lifecycle import build_c_engine
    output = tmp_path_factory.mktemp("c-registry-driver") / "native_registry_override.exe"
    return build_c_engine(output, isolated=True)


def tree_contents(key):
    children, values, _ = winreg.QueryInfoKey(key)
    result = {"values": dict((name, (value, kind)) for name, value, kind in
                             (winreg.EnumValue(key, i) for i in range(values))), "children": {}}
    for i in range(children):
        name = winreg.EnumKey(key, i)
        with winreg.OpenKey(key, name) as child:
            result["children"][name] = tree_contents(child)
    return result


def sandbox_snapshot(namespace):
    result = {}
    for label in ("HKCU32", "HKCU64", "HKLM32", "HKLM64"):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, namespace + "\\" + label, 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            result[label] = tree_contents(key)
    return result


def delete_sandbox(namespace):
    assert re.fullmatch(r"Software\\MedievalPatchLifecycleTests\\[0-9a-f]{32}", namespace)
    def erase(parent, name):
        with winreg.OpenKey(parent, name, 0, winreg.KEY_READ | winreg.KEY_WRITE) as key:
            while winreg.QueryInfoKey(key)[0]:
                erase(key, winreg.EnumKey(key, 0))
        winreg.DeleteKey(parent, name)
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\MedievalPatchLifecycleTests", 0,
                        winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as parent:
        erase(parent, namespace.rsplit("\\", 1)[1])


@pytest.mark.parametrize("scenario", ["success", "success-clean", "rollback"])
def test_fixed_legacy_ownership_in_process_isolated_registry(tmp_path: Path, legacy_package, scenario: str,
                                                            native_registry_driver: Path):
    game = packaged.new_game(tmp_path, "Isolated fixed key " + scenario)
    historical_source = Path(legacy_package["installer"]).parents[1]
    old_engine = historical_source / "src/install-engine.ps1"
    assert packaged.sha256(old_engine) == legacy_package["original_source_sha256"]["src\\install-engine.ps1"]
    baseline = subprocess.run([
        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(old_engine),
        "-Operation", "Install", "-Target", str(game), "-PayloadDirectory", str(historical_source / "vendor/runtime"),
        "-InstallerVersion", "1.0.0",
    ], text=True, capture_output=True, timeout=180)
    assert baseline.returncode == 0, (baseline.stdout, baseline.stderr)
    real_before = real_fixed_registrations()
    state_before = {str(p.relative_to(game)): packaged.sha256(p) for p in game.rglob("*") if p.is_file()}
    namespace = "Software\\MedievalPatchLifecycleTests\\" + uuid.uuid4().hex
    report = {"namespace": namespace, "scenario": scenario, "driver_sha256": packaged.sha256(native_registry_driver)}
    try:
        for label in ("HKCU32", "HKCU64", "HKLM32", "HKLM64"):
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, namespace + "\\" + label, 0,
                                    winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as root:
                winreg.SetValueEx(root, "IsolationSentinel", 0, winreg.REG_SZ, namespace.rsplit("\\", 1)[1])
                for name in LEGACY_NAMES:
                    location = str(tmp_path / "Foreign copy") if name == LEGACY_NAMES[0] else str(game)
                    with winreg.CreateKeyEx(root, UNINSTALL_PARENT + "\\" + name) as key:
                        for field, value in {
                            "DisplayName": "Unofficial Medieval: Total War Collection Patch",
                            "Publisher": "Louie Woolger", "InstallLocation": location,
                            "UninstallString": f'"{location}\\.unofficial-medieval-total-war-patch\\Uninstall.exe"',
                        }.items():
                            winreg.SetValueEx(key, field, 0, winreg.REG_SZ, value)
                        if name == LEGACY_NAMES[1] and scenario != "success-clean":
                            winreg.SetValueEx(key, "CommunitySetting", 0, winreg.REG_SZ, "keep-this-value")
                            with winreg.CreateKey(key, "CommunityChild") as child:
                                winreg.SetValueEx(child, "Marker", 0, winreg.REG_SZ, "keep-this-child")
        before = sandbox_snapshot(namespace)
        fake_uninstaller = tmp_path / "synthetic-uninstaller.bin"
        fake_uninstaller.write_bytes(b"test-only native engine uninstaller fixture")
        command = [str(native_registry_driver), namespace, "--operation", "Install", "--target", str(game),
                   "--payload", str(packaged.PAYLOAD), "--version", "1.0.0", "--uninstaller", str(fake_uninstaller),
                   "--log", str(tmp_path / "override-engine.log"), "--output", "Json"]
        env = dict(os.environ)
        if scenario == "rollback":
            env["MTW_ENABLE_LIFECYCLE_FAULTS"] = "1"
            (game / ".umtwp-test-fixture").write_text("test")
            command += ["--test-fault", "throw:before-commit"]
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=180)
        (tmp_path / "override-console.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        after = sandbox_snapshot(namespace)
        report.update(before=before, after=after, exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
        assert real_fixed_registrations() == real_before
        engine = json.loads(result.stdout)
        if scenario == "rollback":
            assert result.returncode == 2, report
            assert engine["code"] == "injected_failure" and engine["rollback"] == "verified", engine
            # Rollback restores the values exactly, but deliberately retains
            # the now empty per-copy container created by this transaction.
            expected = json.loads(json.dumps(before))
            entries = expected["HKCU32"]
            for component in UNINSTALL_PARENT.split("\\"):
                entries = entries["children"][component]
            entries["children"][registration_name(game)] = {"values": {}, "children": {}}
            # JSON turns stored tuples into lists on both sides; the raw report
            # above still records the physical before and after snapshots.
            assert json.loads(json.dumps(after)) == expected
            for relative, digest in state_before.items():
                assert packaged.sha256(game / relative) == digest
        else:
            assert result.returncode == 0 and engine["status"] == "ok", report
            for label in before:
                if label == "HKCU64":
                    # Windows shares HKCU views; production deliberately scans it once.
                    assert after[label] == before[label]
                    continue
                def uninstall_entries(tree):
                    for component in UNINSTALL_PARENT.split("\\"):
                        tree = tree["children"][component]
                    return tree["children"]
                old_entries, new_entries = uninstall_entries(before[label]), uninstall_entries(after[label])
                assert new_entries[LEGACY_NAMES[0]] == old_entries[LEGACY_NAMES[0]]
                if scenario == "success-clean":
                    assert new_entries[LEGACY_NAMES[1]] == {"values": {}, "children": {}}
                else:
                    kept = new_entries[LEGACY_NAMES[1]]
                    assert kept["values"] == {"CommunitySetting": ("keep-this-value", winreg.REG_SZ)}
                    assert kept["children"] == old_entries[LEGACY_NAMES[1]]["children"]
            if scenario == "success-clean":
                # Retired fixed-name empty keys must not be readopted on repair
                # or prevent removal and a fresh install into the scoped key.
                for operation in ("Install", "Restore", "Install", "Restore"):
                    next_command = list(command)
                    next_command[next_command.index("--operation") + 1] = operation
                    repeated = subprocess.run(next_command, env=env, text=True, capture_output=True, timeout=180)
                    report.setdefault("repeated", []).append({"operation": operation, "exit": repeated.returncode,
                                                               "stdout": repeated.stdout, "stderr": repeated.stderr})
                    assert repeated.returncode == 0, report["repeated"][-1]
                final = sandbox_snapshot(namespace)
                report["final"] = final
                for label in ("HKCU32", "HKLM32", "HKLM64"):
                    entries = uninstall_entries(final[label])
                    assert entries[LEGACY_NAMES[1]] == {"values": {}, "children": {}}
                    assert entries[LEGACY_NAMES[0]] == uninstall_entries(before[label])[LEGACY_NAMES[0]]
                assert uninstall_entries(final["HKCU32"])[registration_name(game)] == {"values": {}, "children": {}}
        report["status"] = "pass"
    finally:
        delete_sandbox(namespace)
        report["sandbox_removed"] = True
        (tmp_path / "registry-override-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        assert real_fixed_registrations() == real_before
