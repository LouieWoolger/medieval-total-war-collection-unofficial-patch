import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "src" / "install-engine.ps1"
PAYLOAD = ROOT / "vendor" / "runtime"
PRODUCT = json.loads((ROOT / "config" / "product.json").read_text(encoding="utf-8"))
SUPPORTED_EXE_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def supported_exe_source() -> Path:
    value = os.environ.get("MTW_TEST_GAME_EXE")
    if not value:
        pytest.skip("MTW_TEST_GAME_EXE is required for destructive disposable scenarios")
    path = Path(value)
    assert sha256(path) == SUPPORTED_EXE_HASH
    return path


def new_game(root: Path, name: str = "Game Folder") -> Path:
    game = root / name
    game.mkdir(parents=True)
    shutil.copy2(supported_exe_source(), game / "Medieval_TW.exe")
    return game


def invoke(
    operation: str,
    game: Path,
    payload: Path = PAYLOAD,
    cwd: Path | None = None,
) -> tuple[subprocess.CompletedProcess[str], dict]:
    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ENGINE),
        "-Operation",
        operation,
        "-Target",
        str(game),
        "-PayloadDirectory",
        str(payload),
        "-InstallerVersion",
        PRODUCT["version"],
        "-InstallerPath",
        str(ROOT / "tests" / "placeholder installer.exe"),
    ]
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    output_lines = [line for line in result.stdout.splitlines() if line.strip()]
    parsed = json.loads(output_lines[-1]) if output_lines else {}
    return result, parsed


def relevant_snapshot(game: Path) -> dict[str, str | None]:
    names = [
        "Medieval_TW.exe",
        "D3D9.dll",
        "dgVoodoo_D3D9.dll",
        "ddraw.dll",
        "D3DImm.dll",
        "dgVoodoo.conf",
        "D3D8.dll",
    ]
    names += [f"{name}.unofficial-patch.bak" for name in names[1:6]]
    return {name: sha256(game / name) if (game / name).is_file() else None for name in names}


def assert_final_runtime(game: Path) -> None:
    manifest = json.loads((PAYLOAD / "payload-manifest.json").read_text(encoding="utf-8"))
    for name, record in manifest["files"].items():
        assert sha256(game / name) == record["sha256"]


def make_payload_variant(root: Path) -> Path:
    payload = root / "payload variant"
    shutil.copytree(PAYLOAD, payload)
    config = payload / "dgVoodoo.conf"
    config.write_text(
        config.read_text(encoding="utf-8") + "\n; installer receipt upgrade regression\n",
        encoding="utf-8",
    )
    manifest_path = payload / "payload-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["files"]["dgVoodoo.conf"]["length"] = config.stat().st_size
    manifest["files"]["dgVoodoo.conf"]["sha256"] = sha256(config)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return payload


def test_engine_uses_standard_user_and_terrain_fix_wording() -> None:
    text = ENGINE.read_text(encoding="utf-8")
    assert "Installed and verified the Terrain Movement Fix." in text
    assert "Run the installer as administrator." not in text
    assert "Grant your account write access or choose a writable game installation." in text


def test_inspect_clean_supported_folder(tmp_path: Path) -> None:
    game = new_game(tmp_path)
    result, report = invoke("Inspect", game)
    assert result.returncode == 0, result.stderr
    assert report["status"] == "ok"
    assert report["mode"] == "clean"
    assert report["target_executable_sha256"] == SUPPORTED_EXE_HASH
    assert relevant_snapshot(game)["D3D9.dll"] is None


def test_engine_ignores_native_system_dll_in_host_working_directory(tmp_path: Path) -> None:
    """NSIS extracts a native System.dll beside its plugins before invoking PowerShell."""
    game = new_game(tmp_path, "NSIS Hostile CWD")
    hostile_cwd = tmp_path / "NSIS Plugin Directory"
    hostile_cwd.mkdir()
    (hostile_cwd / "System.dll").write_bytes(b"native NSIS plugin, not a .NET assembly")

    result, report = invoke("Inspect", game, cwd=hostile_cwd)

    assert result.returncode == 0, (result.stdout, result.stderr)
    assert report["status"] == "ok"
    assert report["mode"] == "clean"


def test_clean_install_and_exact_restore(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Medieval Clean")
    before = relevant_snapshot(game)
    result, installed = invoke("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert installed["status"] == "ok"
    assert installed["action"] == "install"
    assert_final_runtime(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    assert receipt_path.is_file()
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["locked_settings"]["FullscreenAttributes"] == "fake"

    result, restored = invoke("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert restored["status"] == "ok"
    assert restored["action"] == "restore"
    assert relevant_snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_r185_repair_is_idempotent_and_preserves_receipt(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Repair R185")
    first, _ = invoke("Install", game)
    assert first.returncode == 0
    manifest_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    first_manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    first_state = relevant_snapshot(game)

    second, report = invoke("Install", game)
    assert second.returncode == 0, (second.stdout, second.stderr)
    assert report["action"] == "repair"
    second_manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    assert second_manifest["installation_id"] == first_manifest["installation_id"]
    assert second_manifest["repair_count"] == first_manifest["repair_count"] + 1
    assert relevant_snapshot(game) == first_state


def test_managed_payload_upgrade_refreshes_receipt_and_remains_restorable(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Managed Payload Upgrade")
    before = relevant_snapshot(game)
    first, _ = invoke("Install", game)
    assert first.returncode == 0, (first.stdout, first.stderr)

    payload = make_payload_variant(tmp_path)
    upgraded, report = invoke("Install", game, payload=payload)
    assert upgraded.returncode == 0, (upgraded.stdout, upgraded.stderr)
    assert report["action"] == "repair"

    expected = json.loads((payload / "payload-manifest.json").read_text(encoding="utf-8"))
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    for name, record in expected["files"].items():
        assert sha256(game / name) == record["sha256"]
        assert receipt["files"][name]["installed_sha256"] == record["sha256"]
        assert receipt["files"][name]["installed_length"] == record["length"]

    restored, result = invoke("Restore", game, payload=payload)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert result["action"] == "restore"
    assert relevant_snapshot(game) == before


def test_previous_fullscreen_config_upgrades_to_single_window_presentation(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Previous Fullscreen Config")
    manifest = json.loads((PAYLOAD / "payload-manifest.json").read_text(encoding="utf-8"))
    for name in manifest["files"]:
        shutil.copy2(PAYLOAD / name, game / name)

    config = game / "dgVoodoo.conf"
    previous = (
        config.read_bytes()
        .replace(
            b"FullscreenAttributes                 = fake\n",
            b"FullscreenAttributes                 = \r\n",
        )
    )
    config.write_bytes(previous)
    assert sha256(config) == "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA"

    inspected, report = invoke("Inspect", game)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    assert report["mode"] == "r185"

    upgraded, report = invoke("Install", game)
    assert upgraded.returncode == 0, (upgraded.stdout, upgraded.stderr)
    assert report["action"] == "install"
    assert_final_runtime(game)


def test_known_batched_config_upgrades_to_default_presentation(
    tmp_path: Path,
) -> None:
    game = new_game(tmp_path, "Known Batched Config")
    manifest = json.loads((PAYLOAD / "payload-manifest.json").read_text(encoding="utf-8"))
    for name in manifest["files"]:
        shutil.copy2(PAYLOAD / name, game / name)

    config = game / "dgVoodoo.conf"
    previous = config.read_bytes().replace(
        b"PrimarySurfaceBatchedUpdate         = false\r\n",
        b"PrimarySurfaceBatchedUpdate         = true\r\n",
    )
    config.write_bytes(previous)
    assert sha256(config) == "BD21E07D4B9282A8CA0F53613CCB852D419E5D54967A96C8E34A60D2F96E476A"

    upgraded, report = invoke("Install", game)
    assert upgraded.returncode == 0, (upgraded.stdout, upgraded.stderr)
    assert report["action"] == "install"
    assert_final_runtime(game)
    assert "PrimarySurfaceBatchedUpdate         = false" in config.read_text(
        encoding="utf-8-sig"
    )


def test_managed_known_r185_and_previous_fullscreen_config_upgrade_safely(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Managed Previous Runtime")
    installed, _ = invoke("Install", game)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)

    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    receipt["files"]["D3D9.dll"]["installed_sha256"] = (
        "C0D597364734EAEA26ABA83F1FA6B8875B830E4D626D5CF41F42DAEB92106CB4"
    )
    receipt["locked_settings"].pop("FullscreenAttributes")
    receipt_path.write_text(json.dumps(receipt, indent=4) + "\n", encoding="utf-8")

    config = game / "dgVoodoo.conf"
    previous = (
        config.read_bytes()
        .replace(
            b"FullscreenAttributes                 = fake\n",
            b"FullscreenAttributes                 = \r\n",
        )
    )
    config.write_bytes(previous)
    assert sha256(config) == "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA"

    upgraded, report = invoke("Install", game)
    assert upgraded.returncode == 0, (upgraded.stdout, upgraded.stderr)
    assert report["action"] == "repair"
    assert_final_runtime(game)

    upgraded_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert upgraded_receipt["files"]["D3D9.dll"]["installed_sha256"] == sha256(game / "D3D9.dll")
    assert upgraded_receipt["files"]["dgVoodoo.conf"]["installed_sha256"] == sha256(config)
    assert upgraded_receipt["locked_settings"]["FullscreenAttributes"] == "fake"


def test_unsupported_executable_refuses_with_zero_changes(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unsupported")
    data = bytearray((game / "Medieval_TW.exe").read_bytes())
    data[-1] ^= 0xFF
    (game / "Medieval_TW.exe").write_bytes(data)
    before = relevant_snapshot(game)
    result, report = invoke("Install", game)
    assert result.returncode != 0
    assert report["code"] == "unsupported_executable"
    assert relevant_snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_unknown_wrapper_refuses_with_zero_changes(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unknown Wrapper")
    (game / "D3D9.dll").write_bytes(b"unknown local wrapper")
    before = relevant_snapshot(game)
    result, report = invoke("Install", game)
    assert result.returncode != 0
    assert report["code"] == "wrapper_conflict"
    assert "D3D9.dll" in report["message"]
    assert relevant_snapshot(game) == before


def test_existing_sidecar_is_preserved_and_immediate_state_restored(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing Backup")
    sidecar = game / "D3D9.dll.unofficial-patch.bak"
    sidecar.write_bytes(b"older backup must survive")
    old_sidecar_hash = sha256(sidecar)
    before = relevant_snapshot(game)

    result, _ = invoke("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert sha256(sidecar) == old_sidecar_hash
    result, _ = invoke("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant_snapshot(game) == before
    assert sha256(sidecar) == old_sidecar_hash


def test_restore_refuses_post_install_user_modification(tmp_path: Path) -> None:
    game = new_game(tmp_path, "User Modified")
    result, _ = invoke("Install", game)
    assert result.returncode == 0
    (game / "D3D9.dll").write_bytes(b"user replacement")
    before_restore = relevant_snapshot(game)
    result, report = invoke("Restore", game)
    assert result.returncode != 0
    assert report["code"] == "postinstall_modified"
    assert relevant_snapshot(game) == before_restore
    assert (game / ".unofficial-medieval-total-war-patch").exists()


def test_unicode_path_round_trip(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Mediæval Ünicode 騎士")
    before = relevant_snapshot(game)
    result, _ = invoke("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert_final_runtime(game)
    result, _ = invoke("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant_snapshot(game) == before
