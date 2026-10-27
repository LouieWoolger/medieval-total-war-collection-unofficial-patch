import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
from native_helper import helper_path, seal
from registry_isolation import LEGACY_NAMES, read_registration, registration_name, remove_test_registration
from game_fixture_assets import copy_campaign_text, copy_character_text


ROOT = Path(__file__).resolve().parents[1]
ENGINE = helper_path()
PAYLOAD = ROOT / "vendor" / "runtime"
PRODUCT = json.loads((ROOT / "config" / "product.json").read_text(encoding="utf-8"))
SUPPORTED_EXE_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"


@pytest.fixture(autouse=True)
def isolated_registration(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    legacy = {name: read_registration(name) for name in LEGACY_NAMES}
    games = []
    create = new_game

    def tracked(root: Path, name: str = "Game Folder") -> Path:
        game = create(root, name)
        assert read_registration(registration_name(game)) is None
        games.append(game)
        return game

    monkeypatch.setattr(sys.modules[__name__], "new_game", tracked)
    try:
        yield
    finally:
        for game in games:
            remove_test_registration(game, tmp_path)
        assert {name: read_registration(name) for name in LEGACY_NAMES} == legacy


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def historical_config(name: str, expected_sha256: str) -> bytes:
    # Exact legacy bytes are fixtures so tests also work from shallow clones
    # and source bundles without the commits that originally carried them.
    data = (ROOT / "tests" / "fixtures" / name).read_bytes()
    assert hashlib.sha256(data).hexdigest().upper() == expected_sha256
    return data


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
    source = supported_exe_source()
    shutil.copy2(source, game / "Medieval_TW.exe")
    copy_campaign_text(source, game)
    copy_character_text(source, game)
    return game


def invoke(
    operation: str,
    game: Path,
    payload: Path = PAYLOAD,
    cwd: Path | None = None,
    selection: tuple[int, int, int, int] | None = None,
    fault: str = "",
) -> tuple[subprocess.CompletedProcess[str], dict]:
    command = [
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
    if operation == "Install":
        fixture = game.parent / "synthetic-engine-test-uninstaller.bin"
        if not fixture.exists():
            fixture.write_bytes(b"Synthetic engine transaction fixture; not an executable")
        command += ["-UninstallerSource", str(fixture)]
    if selection is not None:
        terrain, scroll, sprite, fast = selection
        command += ["-TerrainFix", str(terrain), "-ScrollFix", str(scroll),
                    "-SpriteFix", str(sprite), "-FastMovesFix", str(fast),
                    "-BattleIconFix", str(int(bool(terrain)))]
    # These historical LAA fixtures predate the new independent UI option.
    command += ["-ParchmentFix", "0"]
    environment = dict(os.environ)
    if fault:
        (game / ".umtwp-test-fixture").write_text("disposable LAA transaction fixture\n")
        environment["MTW_ENABLE_LIFECYCLE_FAULTS"] = "1"
        command += ["-TestFault", fault]
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        env=environment,
    )
    output_lines = [line for line in result.stdout.splitlines() if line.strip()]
    parsed = json.loads(output_lines[-1]) if output_lines else {}
    return result, parsed


def relevant_snapshot(game: Path) -> dict[str, str | None]:
    names = [
        "Medieval_TW.exe",
        "campmap/names/DEFAULT_HEROES.TXT",
        "Loc/Eng/NAMES.TXT",
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
    text = (ROOT / "src/medieval_fix_patcher.c").read_text(encoding="utf-8") + (ROOT / "src/lifecycle.h").read_text(encoding="utf-8")
    text = re.sub(r'"\s*"', '', text)  # C adjacent literal concatenation.
    assert "Installed and verified the Terrain Movement Fix, recovery state and uninstaller." in text
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


@pytest.fixture(scope="module")
def laa_variants():
    spec = importlib.util.spec_from_file_location(
        "mtw_laa_variants", ROOT / "tools" / "generate-laa-exe-variants.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    return generator.build_variants(supported_exe_source().read_bytes())


@pytest.fixture(scope="module")
def fast_only_payload(tmp_path_factory):
    path = tmp_path_factory.mktemp("fast-only-payload")
    shutil.copy2(PAYLOAD / "D3D9-fast-only.dll", path / "D3D9.dll")
    shutil.copy2(PAYLOAD / "payload-manifest-fast-only.json", path / "payload-manifest.json")
    return path


@pytest.mark.parametrize("style", ["retain", "zero", "recompute"])
@pytest.mark.parametrize("selection", [(1, 0, 0, 0), (0, 1, 0, 0),
                                       (0, 0, 1, 0), (1, 1, 1, 0), (0, 0, 0, 1)])
def test_laa_fresh_install_repair_and_exact_restore(
        tmp_path: Path, laa_variants, fast_only_payload, style: str,
        selection: tuple[int, int, int, int]) -> None:
    game = new_game(tmp_path, "LAA exact original")
    original = laa_variants[(0, 0, 0, style)]
    (game / "Medieval_TW.exe").write_bytes(original)
    before = relevant_snapshot(game)
    inspected, report = invoke("Inspect", game)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    assert report["managed_installation"] is False
    payload = fast_only_payload if selection == (0, 0, 0, 1) else PAYLOAD
    first, report = invoke("Install", game, selection=selection, payload=payload)
    assert first.returncode == 0, (first.stdout, first.stderr)
    expected = (selection[1], selection[2], 0,
                "recompute" if selection[1] or selection[2] else style)
    assert sha256(game / "Medieval_TW.exe") == hashlib.sha256(
        laa_variants[expected]).hexdigest().upper()
    receipt = json.loads((game / ".unofficial-medieval-total-war-patch" /
                          "install-manifest.json").read_text(encoding="utf-8-sig"))
    assert receipt["files"]["Medieval_TW.exe"]["original_sha256"] == sha256(
        game / ".unofficial-medieval-total-war-patch" / "originals" / "Medieval_TW.exe")
    second, report = invoke("Install", game, selection=selection, payload=payload)
    assert second.returncode == 0, (second.stdout, second.stderr)
    restored, report = invoke("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert relevant_snapshot(game) == before


@pytest.mark.parametrize("style", ["retain", "zero", "recompute"])
def test_laa_deselect_direct_fix_restores_exact_original(
        tmp_path: Path, laa_variants, style: str) -> None:
    game = new_game(tmp_path, "LAA deselect")
    original = laa_variants[(0, 0, 0, style)]
    (game / "Medieval_TW.exe").write_bytes(original)
    first, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert first.returncode == 0, (first.stdout, first.stderr)
    changed, report = invoke("Install", game, selection=(1, 0, 0, 0))
    assert changed.returncode == 0, (changed.stdout, changed.stderr)
    assert (game / "Medieval_TW.exe").read_bytes() == original
    restored, report = invoke("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert (game / "Medieval_TW.exe").read_bytes() == original


@pytest.mark.parametrize("style", ["retain", "zero", "recompute"])
def test_managed_external_laa_requires_guarded_repair(
        tmp_path: Path, laa_variants, style: str) -> None:
    game = new_game(tmp_path, "External LAA")
    first, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert first.returncode == 0, (first.stdout, first.stderr)
    (game / "Medieval_TW.exe").write_bytes(laa_variants[(1, 1, 0, style)])
    before = relevant_snapshot(game)
    remove, report = invoke("Restore", game)
    assert remove.returncode != 0
    assert report["code"] == "reconciliation_required"
    assert relevant_snapshot(game) == before
    repair, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert repair.returncode == 0, (repair.stdout, repair.stderr)
    receipt = json.loads((game / ".unofficial-medieval-total-war-patch" /
                          "install-manifest.json").read_text(encoding="utf-8-sig"))
    assert receipt["files"]["Medieval_TW.exe"]["original_sha256"] == hashlib.sha256(
        laa_variants[(0, 0, 0, style)]).hexdigest().upper()
    assert (game / "Medieval_TW.exe").read_bytes() == laa_variants[(1, 1, 0, style)]
    restored, report = invoke("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert (game / "Medieval_TW.exe").read_bytes() == laa_variants[(0, 0, 0, style)]


def test_laa_with_unrelated_exe_edit_is_rejected_without_changes(tmp_path: Path, laa_variants) -> None:
    game = new_game(tmp_path, "Unsupported LAA edit")
    changed = bytearray(laa_variants[(0, 0, 0, "recompute")])
    changed[0x2000] ^= 1
    (game / "Medieval_TW.exe").write_bytes(changed)
    before = relevant_snapshot(game)
    result, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert result.returncode != 0
    assert report["code"] == "unsupported_executable"
    assert relevant_snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_external_laa_repair_rejects_damaged_original(tmp_path: Path, laa_variants) -> None:
    game = new_game(tmp_path, "Damaged original")
    installed, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    state = game / ".unofficial-medieval-total-war-patch"
    receipt = state / "install-manifest.json"
    original = state / "originals" / "Medieval_TW.exe"
    original.write_bytes(b"damaged original")
    (game / "Medieval_TW.exe").write_bytes(laa_variants[(1, 1, 0, "recompute")])
    before, receipt_before = relevant_snapshot(game), receipt.read_bytes()
    result, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert result.returncode != 0
    assert report["code"] == "receipt_invalid"
    assert relevant_snapshot(game) == before
    assert receipt.read_bytes() == receipt_before
    assert original.read_bytes() == b"damaged original"


def test_laa_receipt_option_forgery_is_rejected(tmp_path: Path, laa_variants) -> None:
    game = new_game(tmp_path, "Forged LAA receipt")
    (game / "Medieval_TW.exe").write_bytes(laa_variants[(0, 0, 0, "recompute")])
    installed, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    receipt = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    forged = json.loads(receipt.read_text(encoding="utf-8-sig"))
    forged["scroll_fix_enabled"] = False
    seal(forged)
    receipt.write_text(json.dumps(forged), encoding="utf-8")
    before = relevant_snapshot(game)
    result, report = invoke("Restore", game)
    assert result.returncode != 0
    assert report["code"] == "receipt_invalid"
    assert relevant_snapshot(game) == before


def test_laa_install_fault_rolls_back_and_retry_restores_original(
        tmp_path: Path, laa_variants) -> None:
    game = new_game(tmp_path, "LAA rollback")
    original = laa_variants[(0, 0, 0, "recompute")]
    (game / "Medieval_TW.exe").write_bytes(original)
    before = relevant_snapshot(game)
    result, report = invoke("Install", game, selection=(1, 1, 1, 0), fault="throw:after-file-5")
    assert result.returncode != 0
    assert report["rollback"] == "verified"
    assert relevant_snapshot(game) == before
    installed, report = invoke("Install", game, selection=(1, 1, 1, 0))
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    restored, report = invoke("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert (game / "Medieval_TW.exe").read_bytes() == original


def test_engine_ignores_native_system_dll_in_host_working_directory(tmp_path: Path) -> None:
    """The native helper works from NSIS's plugin directory without CLR loading."""
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


def test_existing_custom_dgvoodoo_config_is_replaced_and_restored(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Custom dgVoodoo Config")
    config = game / "dgVoodoo.conf"
    config.write_text(
        "; user-maintained dgVoodoo profile\n"
        "[GeneralExt]\n"
        "DesktopResolution = 1920x1080\n",
        encoding="utf-8",
    )
    original_hash = sha256(config)
    before = relevant_snapshot(game)

    inspected, report = invoke("Inspect", game)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    assert report["mode"] == "clean"

    installed, report = invoke("Install", game)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    assert report["action"] == "install"
    assert_final_runtime(game)

    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    record = receipt["files"]["dgVoodoo.conf"]
    assert record["existed"] is True
    assert record["original_sha256"] == original_hash
    assert sha256(game / "dgVoodoo.conf.unofficial-patch.bak") == original_hash

    restored, report = invoke("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert report["action"] == "restore"
    assert relevant_snapshot(game) == before


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


def test_managed_repair_replaces_modified_dgvoodoo_config(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Managed Custom Config")
    before_install = relevant_snapshot(game)
    first, _ = invoke("Install", game)
    assert first.returncode == 0, (first.stdout, first.stderr)

    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    first_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    (game / "dgVoodoo.conf").write_text(
        "; changed after the previous patch installation\n"
        "[GeneralExt]\n"
        "DesktopResolution = 2560x1440\n",
        encoding="utf-8",
    )

    verified, report = invoke("Verify", game)
    assert verified.returncode != 0
    assert report["code"] == "verification_failed"

    inspected, report = invoke("Inspect", game)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    assert report["managed_installation"] is True

    repaired, report = invoke("Install", game)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert report["action"] == "repair"
    assert_final_runtime(game)

    repaired_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert repaired_receipt["installation_id"] == first_receipt["installation_id"]
    assert repaired_receipt["repair_count"] == first_receipt["repair_count"] + 1

    restored, report = invoke("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert report["action"] == "restore"
    assert relevant_snapshot(game) == before_install


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
    previous = historical_config(
        "dgVoodoo-legacy-fullscreen.conf",
        "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA",
    )
    config.write_bytes(previous)
    assert sha256(config) == "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA"

    inspected, report = invoke("Inspect", game)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    assert report["mode"] == "r292-dust-alpha"

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
    previous = historical_config(
        "dgVoodoo-2.87.2.conf",
        "EF8DF4EBA5AF028891678A641304D297F3D759A7EBF4FBE8FFB735B9808E5A97",
    ).replace(
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
    # Generate the historical v1 receipt with the pinned released engine itself.
    # The engine never writes Windows registration; the old NSIS package did.
    old_engine = tmp_path / "released-v1-engine.ps1"
    proof_path = os.environ.get("MTW_LEGACY_PACKAGE_PROOF")
    if proof_path:
        proof = json.loads(Path(proof_path).read_text())
        assert proof["historical_commit"] == "1d24a75"
        historical = Path(proof["installer"]).parents[1] / "src/install-engine.ps1"
        old_engine.write_bytes(historical.read_bytes())
    else:
        released = subprocess.run(["git", "show", "1d24a75:src/install-engine.ps1"],
                                  cwd=ROOT, capture_output=True, check=True)
        old_engine.write_bytes(released.stdout)
    assert sha256(old_engine) == "47273A6C8C4902D14B9983391966AC1470FF0DDBAEA61830F92A09165221CD72"
    installed = subprocess.run([
        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(old_engine),
        "-Operation", "Install", "-Target", str(game), "-PayloadDirectory", str(PAYLOAD),
        "-InstallerVersion", PRODUCT["version"],
    ], capture_output=True, text=True)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)

    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    receipt["files"]["D3D9.dll"]["installed_sha256"] = (
        "C0D597364734EAEA26ABA83F1FA6B8875B830E4D626D5CF41F42DAEB92106CB4"
    )
    receipt["locked_settings"].pop("FullscreenAttributes")
    receipt_path.write_text(json.dumps(receipt, indent=4) + "\n", encoding="utf-8")

    config = game / "dgVoodoo.conf"
    previous = historical_config(
        "dgVoodoo-legacy-fullscreen.conf",
        "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA",
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


def test_incomplete_state_directory_is_preserved_on_failed_install(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Incomplete State")
    state = game / ".unofficial-medieval-total-war-patch"
    state.mkdir()
    marker = state / "orphan.tmp"
    marker.write_bytes(b"pre-existing interruption marker\x00\xff")
    before = relevant_snapshot(game)

    result, report = invoke("Install", game)

    assert result.returncode != 0
    assert report["code"] in {"receipt_invalid", "transaction_failed"}
    assert marker.read_bytes() == b"pre-existing interruption marker\x00\xff"
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


def test_restore_archives_post_install_user_modification(tmp_path: Path) -> None:
    game = new_game(tmp_path, "User Modified")
    before_install = relevant_snapshot(game)
    result, _ = invoke("Install", game)
    assert result.returncode == 0
    (game / "D3D9.dll").write_bytes(b"user replacement")
    result, report = invoke("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant_snapshot(game) == before_install
    assert (Path(report["recovery_archive"]) / "D3D9.dll").read_bytes() == b"user replacement"
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_unicode_path_round_trip(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Medi\u00e6val \u00dcnicode \u9a0e\u58eb")
    before = relevant_snapshot(game)
    result, _ = invoke("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert_final_runtime(game)
    result, _ = invoke("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant_snapshot(game) == before
