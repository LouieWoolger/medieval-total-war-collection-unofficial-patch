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
from native_helper import helper_path, seal
from registry_isolation import read_registration, registration_name, remove_test_registration
from game_fixture_assets import copy_campaign_text, copy_character_text

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
HEROES = "campmap/names/DEFAULT_HEROES.TXT"
CHARACTER_NAMES = "Loc/Eng/NAMES.TXT"
REGION_TITLES = "Loc/Eng/DEFAULT_REGION_SPECIFIC.TXT"
FIXED_HEROES = "B9BB6F6000BB426736F2E113A0D05331A014C8351AB0D8C1B3FD2FE10705ADDB"
FIXED_NAMES = "3421E5FE033FF3CD9D710FFA38F465B970060C4095C56B590C22AEDFEF4A6F15"
FIXED_REGION_TITLES = "F75F5DAFE7319715C81ABE87AC9F6D1058944AC505DA53E3C015A7EB353BF1F7"


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
        copy_campaign_text(Path(source), game)
        copy_character_text(Path(source), game)
        (game / "SaveGames").mkdir()
        (game / "SaveGames/personal.cpg").write_bytes(b"untouched personal save\x00\xff")
        (game / "unrelated-mod.txt").write_bytes(b"unrelated mod")
        assert read_registration(registration_name(game)) is None, "Fixture identity must not overwrite an existing key"
        created.append(game)
        return game

    yield make
    for game in created:
        remove_test_registration(game, tmp_path)


def run(operation: str, game: Path, *, fault: str = "", uninstaller: Path | None = None,
        terrain_fix: bool | None = None, scroll_fix: bool | None = None,
        sprite_fix: bool | None = None, parchment_fix: bool | None = False,
        name_fix: bool | None = None, economy_fix: bool | None = None):
    # This models a supplied package artifact, not a functional NSIS executable.
    # The compiled suite uses WriteUninstaller's actual output instead.
    source = uninstaller or game.parent / "synthetic-uninstaller.bin"
    if not source.exists():
        source.write_bytes(b"synthetic package uninstaller for lifecycle-only tests\x00\xff")
    command = [str(ENGINE),
               "-Operation", operation, "-Target", str(game),
               "-PayloadDirectory", str(PAYLOAD), "-InstallerVersion", PRODUCT["version"],
               "-UninstallerSource", str(source)]
    for option, selected in (("TerrainFix", terrain_fix), ("ScrollFix", scroll_fix),
                             ("SpriteFix", sprite_fix), ("ParchmentFix", parchment_fix),
                             ("NameFix", name_fix), ("EconomyFix", economy_fix)):
        if selected is not None:
            command.extend(("-" + option, str(int(selected))))
    if terrain_fix is False:
        command.extend(("-BattleIconFix", "0"))
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


def test_name_corrections_are_an_independent_selection(games):
    game = games()
    originals = {name: digest(game / name) for name in (HEROES, CHARACTER_NAMES, REGION_TITLES)}
    result, _ = run("Install", game, terrain_fix=False, scroll_fix=False,
                    sprite_fix=False, parchment_fix=False, name_fix=True)
    assert result.returncode == 0, (result.stdout, result.stderr)
    receipt = read_receipt(game)
    assert receipt["name_fix_enabled"] is True
    assert receipt["marriage_title_fix_enabled"] is True
    assert digest(game / HEROES) == FIXED_HEROES
    assert digest(game / CHARACTER_NAMES) == FIXED_NAMES
    assert digest(game / REGION_TITLES) == FIXED_REGION_TITLES
    assert digest(game / "Medieval_TW.exe") == SUPPORTED
    result, _ = run("Install", game, terrain_fix=False, scroll_fix=False,
                    sprite_fix=False, parchment_fix=False, name_fix=False)
    assert result.returncode != 0  # No fix selected.
    result, _ = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert {name: digest(game / name) for name in originals} == originals


def test_name_choice_reapplies_and_restores_marriage_letter_text(games):
    game = games("Name and title corrections")
    original = digest(game / REGION_TITLES)
    result, report = run("Install", game, terrain_fix=False, scroll_fix=False,
                         sprite_fix=False, parchment_fix=False, name_fix=True)
    assert result.returncode == 0, (report, result.stdout)
    assert digest(game / REGION_TITLES) == FIXED_REGION_TITLES
    assert read_receipt(game)["marriage_title_fix_enabled"] is True
    assert read_receipt(game)["files"][REGION_TITLES]["original_sha256"] == original
    assert digest(game / "Medieval_TW.exe") == SUPPORTED
    verify, _ = run("Verify", game)
    assert verify.returncode == 0, verify.stdout
    repeat, _ = run("Install", game, terrain_fix=False, scroll_fix=False,
                    sprite_fix=False, parchment_fix=False, name_fix=True)
    assert repeat.returncode == 0, repeat.stdout
    assert digest(game / REGION_TITLES) == FIXED_REGION_TITLES
    removed, _ = run("Restore", game)
    assert removed.returncode == 0, removed.stdout
    assert digest(game / REGION_TITLES) == original


def test_name_choice_toggle_off_preserves_subsequent_user_edit(games):
    game = games("Name choice toggle")
    path = game / REGION_TITLES
    original = path.read_bytes()
    on, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                sprite_fix=False, name_fix=True)
    assert on.returncode == 0, on.stdout
    off, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                 sprite_fix=False, name_fix=False)
    assert off.returncode == 0, off.stdout
    assert path.read_bytes() == original
    assert read_receipt(game)["name_fix_enabled"] is False
    assert read_receipt(game)["marriage_title_fix_enabled"] is False
    changed = original + b"\r\nuser title\r\n"
    path.write_bytes(changed)
    assert run("Verify", game)[0].returncode == 0
    assert run("Install", game, terrain_fix=False, scroll_fix=True,
               sprite_fix=False, name_fix=False)[0].returncode == 0
    assert path.read_bytes() == changed
    rejected, report = run("Install", game, terrain_fix=False, scroll_fix=True,
                           sprite_fix=False, name_fix=True)
    assert rejected.returncode != 0 and report["code"] == "unsupported_data"
    assert path.read_bytes() == changed
    assert run("Restore", game)[0].returncode == 0
    assert path.read_bytes() == changed


def test_v7_receipt_upgrades_to_marriage_titles_with_name_choice(games):
    game = games("V7 name and title upgrade")
    original = digest(game / REGION_TITLES)
    first, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                   sprite_fix=False, name_fix=False)
    assert first.returncode == 0, first.stdout
    path = game / STATE / "install-manifest.json"
    receipt = read_receipt(game)
    receipt["schema"] = "unofficial-medieval-total-war-patch-install-v7"
    receipt.pop("marriage_title_fix_enabled")
    receipt["files"].pop(REGION_TITLES)
    seal(receipt)
    path.write_text(json.dumps(receipt, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    (game / STATE / "originals" / "DEFAULT_REGION_SPECIFIC.TXT").unlink()
    upgrade, report = run("Install", game, terrain_fix=False, scroll_fix=True,
                          sprite_fix=False, name_fix=True)
    assert upgrade.returncode == 0, (report, upgrade.stdout)
    assert read_receipt(game)["schema"] == "unofficial-medieval-total-war-patch-install-v8"
    assert digest(game / REGION_TITLES) == FIXED_REGION_TITLES
    assert read_receipt(game)["files"][REGION_TITLES]["original_sha256"] == original
    assert run("Restore", game)[0].returncode == 0
    assert digest(game / REGION_TITLES) == original


def test_other_fix_leaves_character_data_untouched_when_name_fix_off(games):
    game = games()
    originals = {name: digest(game / name) for name in (HEROES, CHARACTER_NAMES)}
    result, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                    sprite_fix=False, parchment_fix=False, name_fix=False)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert read_receipt(game)["name_fix_enabled"] is False
    assert {name: digest(game / name) for name in originals} == originals
    result, _ = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)


def test_repeating_install_with_name_fix_off_does_not_archive_unchanged_snapshots(games):
    game = games("Name off repeat")
    original = {name: digest(game / name) for name in (HEROES, CHARACTER_NAMES)}
    for repair_count in (0, 1):
        result, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                        sprite_fix=False, name_fix=False)
        assert result.returncode == 0, (result.stdout, result.stderr)
        receipt = read_receipt(game)
        assert receipt["repair_count"] == repair_count
        assert receipt["name_fix_enabled"] is False
        assert receipt["recovery_archive"] == ""
        assert not list(game.glob(".medieval-recovery-*"))
        assert {name: digest(game / name) for name in original} == original


def test_name_component_can_be_disabled_and_later_text_edits_survive_removal(games):
    game = games("Toggle name corrections")
    original = {name: digest(game / name) for name in (HEROES, CHARACTER_NAMES)}
    first, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                   sprite_fix=False, name_fix=True)
    assert first.returncode == 0, first.stdout
    assert digest(game / CHARACTER_NAMES) == FIXED_NAMES
    off, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                 sprite_fix=False, name_fix=False)
    assert off.returncode == 0, off.stdout
    assert read_receipt(game)["name_fix_enabled"] is False
    assert {name: digest(game / name) for name in original} == original
    path = game / CHARACTER_NAMES
    manual = path.read_bytes() + b"\r\nuser edit\r\n"
    path.write_bytes(manual)
    verify, _ = run("Verify", game)
    assert verify.returncode == 0, verify.stdout
    reinstall, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                       sprite_fix=False, name_fix=False)
    assert reinstall.returncode == 0, reinstall.stdout
    assert path.read_bytes() == manual
    removed, _ = run("Restore", game)
    assert removed.returncode == 0, removed.stdout
    assert path.read_bytes() == manual


def test_name_component_accepts_unmodified_user_text_only_when_off(games):
    game = games("Other fix with changed text")
    path = game / CHARACTER_NAMES
    changed = path.read_bytes() + b"\r\nuser title\r\n"
    path.write_bytes(changed)
    installed_result, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                              sprite_fix=False, name_fix=False)
    assert installed_result.returncode == 0, installed_result.stdout
    assert path.read_bytes() == changed
    enabled, report = run("Install", game, terrain_fix=False, scroll_fix=True,
                          sprite_fix=False, name_fix=True)
    assert enabled.returncode != 0
    assert report["code"] == "unsupported_data"
    assert path.read_bytes() == changed
    removed, _ = run("Restore", game)
    assert removed.returncode == 0, removed.stdout
    assert path.read_bytes() == changed


def test_v6_receipt_upgrades_to_independent_name_selection(games):
    game = games("V6 name upgrade")
    originals = {name: digest(game / name) for name in (HEROES, CHARACTER_NAMES)}
    first, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                   sprite_fix=False, name_fix=True)
    assert first.returncode == 0, first.stdout
    path = game / STATE / "install-manifest.json"
    receipt = read_receipt(game)
    receipt["schema"] = "unofficial-medieval-total-war-patch-install-v6"
    receipt.pop("name_fix_enabled")
    receipt.pop("marriage_title_fix_enabled")
    receipt.pop("key_fix_enabled")
    receipt["files"].pop(REGION_TITLES)
    seal(receipt)
    path.write_text(json.dumps(receipt, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    off, _ = run("Install", game, terrain_fix=False, scroll_fix=True,
                 sprite_fix=False, name_fix=False)
    assert off.returncode == 0, off.stdout
    assert read_receipt(game)["schema"] == "unofficial-medieval-total-war-patch-install-v8"
    assert read_receipt(game)["name_fix_enabled"] is False
    assert {name: digest(game / name) for name in originals} == originals
    removed, _ = run("Restore", game)
    assert removed.returncode == 0, removed.stdout
    assert {name: digest(game / name) for name in originals} == originals


def installed(game: Path):
    result, report = run("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    return report


def relevant(game: Path):
    names = (*NAMES, "Medieval_TW.exe", "Loc/Eng/NAMES.TXT",
             "campmap/names/DEFAULT_HEROES.TXT", REGION_TITLES,
             "SaveGames/personal.cpg", "unrelated-mod.txt")
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


def test_character_parchment_selection_is_independent_and_reversible(games):
    game = games("Character parchment choice")
    before = relevant(game)
    stock_exe = digest(game / "Medieval_TW.exe")

    enabled, report = run("Install", game, terrain_fix=True, scroll_fix=False,
                          sprite_fix=False, parchment_fix=True)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    receipt = read_receipt(game)
    assert receipt["parchment_fix_enabled"] is True
    assert digest(game / "Medieval_TW.exe") == "1BF4B4D8123A1218E35D8D44E16ADE55ED0BDDF1A03B109299A034092524FBFA"
    assert receipt["files"]["Medieval_TW.exe"]["installed_sha256"] == digest(game / "Medieval_TW.exe")

    disabled, report = run("Install", game, terrain_fix=True, scroll_fix=False,
                           sprite_fix=False, parchment_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert read_receipt(game)["parchment_fix_enabled"] is False
    assert digest(game / "Medieval_TW.exe") == stock_exe

    default, report = run("Install", game, terrain_fix=True, scroll_fix=False,
                          sprite_fix=False, parchment_fix=None)
    assert default.returncode == 0, (default.stdout, default.stderr)
    assert read_receipt(game)["parchment_fix_enabled"] is True
    assert digest(game / "Medieval_TW.exe") == "1BF4B4D8123A1218E35D8D44E16ADE55ED0BDDF1A03B109299A034092524FBFA"
    restored, report = run("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert relevant(game) == before


def test_unit_balance_selection_repairs_toggles_and_restores(games):
    game = games("Unit balance choice")
    before = relevant(game)
    personal = game / "SaveGames/personal.cpg"
    personal_hash = digest(personal)
    options = dict(terrain_fix=False, scroll_fix=False, sprite_fix=False,
                   parchment_fix=False, name_fix=False, economy_fix=True)
    # Existing saves, including ones the installer cannot parse, belong to
    # the player. They must not prevent choosing or removing Unit Balance.
    long_save = game / "SaveGames" / (("\u00e9" * 130) + ".cpg")
    shutil.copy2(personal, long_save)
    long_hash = digest(long_save)
    enabled, report = run("Install", game, **options)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    balanced_hash = "61221C667368358D359C3D51D6692FE68729F747EF7B16403A85059B0E9C34DA"
    assert digest(game / "Medieval_TW.exe") == balanced_hash
    assert read_receipt(game)["economy_fix_enabled"] is True

    repaired, report = run("Install", game, **options)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert digest(game / "Medieval_TW.exe") == balanced_hash

    disabled, report = run("Install", game, **{**options, "economy_fix": False, "scroll_fix": True})
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert read_receipt(game)["economy_fix_enabled"] is False
    assert digest(game / "Medieval_TW.exe") != balanced_hash

    reenabled, report = run("Install", game, **{**options, "scroll_fix": True})
    assert reenabled.returncode == 0, (reenabled.stdout, reenabled.stderr)
    assert read_receipt(game)["economy_fix_enabled"] is True
    assert digest(game / "Medieval_TW.exe") != balanced_hash

    # An imported save must also remain untouched through a same-mode repair.
    imported = game / "SaveGames/ImportedAfterSwitch.cpg"
    shutil.copy2(personal, imported)
    inspected, report = run("Inspect", game)
    assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
    same_mode, report = run("Install", game, **{**options, "scroll_fix": True})
    assert same_mode.returncode == 0, (same_mode.stdout, same_mode.stderr)
    assert read_receipt(game)["economy_fix_enabled"] is True
    imported.unlink()

    restored, report = run("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert digest(personal) == personal_hash
    assert digest(long_save) == long_hash
    assert relevant(game) == before


def test_missing_exe_restore_with_existing_save(games):
    game = games("Missing EXE economy recovery")
    exe = game / "Medieval_TW.exe"
    original_sha256 = digest(exe)
    personal = game / "SaveGames/personal.cpg"
    personal_sha256 = digest(personal)

    installed, report = run("Install", game, terrain_fix=False, scroll_fix=False,
                            sprite_fix=False, economy_fix=True)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    assert read_receipt(game)["economy_fix_enabled"] is True
    receipt_path = game / STATE / "install-manifest.json"

    exe.unlink()
    restored, report = run("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert digest(exe) == original_sha256
    assert digest(personal) == personal_sha256
    assert not receipt_path.exists()
    assert not registry(game)


def test_missing_exe_restore_without_economy_change_keeps_personal_save(games):
    game = games("Missing EXE same economy")
    exe = game / "Medieval_TW.exe"
    original_sha256 = digest(exe)
    personal = game / "SaveGames/personal.cpg"
    personal_sha256 = digest(personal)
    installed, report = run("Install", game, terrain_fix=False, scroll_fix=True,
                            sprite_fix=False, economy_fix=False)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    assert read_receipt(game)["economy_fix_enabled"] is False

    exe.unlink()
    before = relevant(game)
    reinstall, report = run("Install", game, terrain_fix=False, scroll_fix=True,
                            sprite_fix=False, economy_fix=False)
    assert reinstall.returncode == 2, (reinstall.stdout, reinstall.stderr)
    assert report["code"] == "target_missing"
    assert relevant(game) == before
    restored, report = run("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert digest(exe) == original_sha256
    assert digest(personal) == personal_sha256


def test_present_unknown_exe_still_refuses_restore(games):
    game = games("Present unknown EXE economy")
    personal = game / "SaveGames/personal.cpg"
    personal_sha256 = digest(personal)
    installed, report = run("Install", game, terrain_fix=False, scroll_fix=False,
                            sprite_fix=False, economy_fix=True)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)

    exe = game / "Medieval_TW.exe"
    changed = bytearray(exe.read_bytes())
    changed[-1] ^= 1
    exe.write_bytes(changed)
    before = relevant(game)
    receipt_path = game / STATE / "install-manifest.json"
    receipt_sha256 = digest(receipt_path)
    registration = registry(game)
    blocked, report = run("Restore", game)
    assert blocked.returncode == 2, (blocked.stdout, blocked.stderr)
    assert report["code"] == "unsupported_executable"
    assert relevant(game) == before
    assert digest(receipt_path) == receipt_sha256
    assert registry(game) == registration
    assert digest(personal) == personal_sha256


def test_present_supported_exe_swap_can_restore_with_existing_save(games):
    game = games("Supported EXE policy swap")
    exe = game / "Medieval_TW.exe"
    stock_bytes = exe.read_bytes()
    personal = game / "SaveGames/personal.cpg"
    personal_sha256 = digest(personal)
    installed, report = run("Install", game, terrain_fix=False, scroll_fix=False,
                            sprite_fix=False, economy_fix=True)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    receipt_path = game / STATE / "install-manifest.json"

    # Simulate an out-of-band replacement with a *known* stock-price EXE.
    # Removal is still allowed when the live EXE already equals the original.
    exe.write_bytes(stock_bytes)
    restored, report = run("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert exe.read_bytes() == stock_bytes
    assert digest(personal) == personal_sha256
    assert not receipt_path.exists()


def test_legacy_receipt_reports_migration_with_existing_save(games):
    game = games("Legacy receipt with existing save")
    personal = game / "SaveGames/personal.cpg"
    personal_sha256 = digest(personal)
    installed, report = run("Install", game, terrain_fix=True, scroll_fix=False,
                            sprite_fix=False, economy_fix=True)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)

    receipt_path = game / STATE / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    receipt["schema"] = "unofficial-medieval-total-war-patch-install-v1"
    receipt["files"] = {name: receipt["files"][name] for name in (
        "dgVoodoo_D3D9.dll", "ddraw.dll", "D3DImm.dll", "dgVoodoo.conf", "D3D9.dll")}
    receipt_path.write_text(json.dumps(receipt, separators=(",", ":")), encoding="utf-8")
    registration = registry(game)
    receipt_sha256 = digest(receipt_path)

    for missing_exe in (False, True):
        if missing_exe:
            (game / "Medieval_TW.exe").unlink()
        before = relevant(game)
        blocked, report = run("Restore", game)
        assert blocked.returncode == 2, (blocked.stdout, blocked.stderr)
        assert report["code"] == "migration_required"
        assert relevant(game) == before
        assert digest(receipt_path) == receipt_sha256
        assert registry(game) == registration
        assert digest(personal) == personal_sha256


def test_extended_character_executable_can_be_reinstalled(games):
    game = games("Extended parchment executable reinstall")
    before = relevant(game)
    options = dict(terrain_fix=True, scroll_fix=True, sprite_fix=True,
                   parchment_fix=True)

    first, report = run("Install", game, **options)
    assert first.returncode == 0, (first.stdout, first.stderr)
    installed_hash = digest(game / "Medieval_TW.exe")
    assert (game / "Medieval_TW.exe").stat().st_size == 0x4EB000

    second, report = run("Install", game, **options)
    assert second.returncode == 0, (second.stdout, second.stderr)
    assert digest(game / "Medieval_TW.exe") == installed_hash

    removed, report = run("Restore", game)
    assert removed.returncode == 0, (removed.stdout, removed.stderr)
    assert relevant(game) == before


@pytest.mark.parametrize("target_length", [183, 218])
def test_deep_game_folder_reinstall_and_restore_cleans_metadata(games, tmp_path, target_length):
    # A deeply nested but valid game path makes transaction metadata longer
    # than the legacy Win32 enumeration limit during normal reinstall/restore.
    padding = target_length - len(str(tmp_path)) - 1
    assert padding > 0, "test needs a shorter temporary root for this boundary"
    name = "g" * padding
    game = games(name)
    assert len(str(game)) == target_length
    before = relevant(game)

    for _ in range(2):
        installed_result, _ = run("Install", game)
        assert installed_result.returncode == 0, (installed_result.stdout, installed_result.stderr)

    restored, _ = run("Restore", game)
    assert restored.returncode == 0, (restored.stdout, restored.stderr)
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not (game / UNINSTALL).exists()
    assert not list(game.glob(".unofficial-medieval-patch-preparing-*"))
    assert not registry(game)


@pytest.mark.parametrize("target_length", [183, 184])
def test_deep_game_folder_failed_install_rolls_back_cleanly(games, tmp_path, target_length):
    padding = target_length - len(str(tmp_path)) - 1
    assert padding > 0, "test needs a shorter temporary root for this boundary"
    name = "g" * padding
    game = games(name)
    assert len(str(game)) == target_length
    before = relevant(game)

    failed, report = run("Install", game, fault="throw:before-stage-exe",
                         terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert failed.returncode != 0, (failed.stdout, failed.stderr)
    assert report["rollback"] in {"not-needed", "verified"}, report
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not (game / UNINSTALL).exists()
    assert not list(game.glob(".unofficial-medieval-patch-preparing-*"))
    assert not registry(game)


def test_exact_current_proxy_without_receipt_is_restored_as_preinstall(games):
    game = games("Receipt lost with current proxy")
    proxy = PAYLOAD / "D3D9.dll"
    shutil.copy2(proxy, game / "D3D9.dll")
    prior_hash = digest(game / "D3D9.dll")

    installed = run("Install", game, terrain_fix=True, scroll_fix=False,
                    sprite_fix=False, parchment_fix=False)
    assert installed[0].returncode == 0, (installed[0].stdout, installed[0].stderr)
    receipt = read_receipt(game)
    assert receipt["preinstall_mode"] == "r293-key-text"
    assert receipt["files"]["D3D9.dll"]["existed"] is True
    assert receipt["files"]["D3D9.dll"]["baseline_kind"] == "verified-preinstall"
    assert receipt["files"]["D3D9.dll"]["original_sha256"] == prior_hash
    assert receipt["recovery_archive"] == ""

    removed, report = run("Restore", game)
    assert removed.returncode == 0, (removed.stdout, removed.stderr)
    assert digest(game / "D3D9.dll") == prior_hash


@pytest.mark.parametrize("cyd,kilij,heroes_fixed", [
    (False, False, False), (True, False, False), (False, True, False),
    (True, True, False), (True, False, True), (True, True, True),
])
def test_character_data_partial_and_preexisting_manual_fixes_restore_exact_input(
    games, cyd: bool, kilij: bool, heroes_fixed: bool,
):
    game = games("Supported text variants")
    heroes = (game / HEROES).read_bytes()
    names = (game / CHARACTER_NAMES).read_bytes()
    if heroes_fixed:
        heroes = heroes.replace(b"SET_FORENAMES::\tFN_SICILIAN\t14\t8",
                                 b"SET_FORENAMES::\tFN_SICILIAN\t4\t8")
        (game / HEROES).write_bytes(heroes)
    if cyd:
        names = names.replace(b'{"Cydwladwr" (Compatriot)}', b'{"Cydwladwr (Compatriot)"}')
    if kilij:
        names = names.replace(b'{"Kilij Arslan}', b'{"Kilij Arslan"}')
    (game / CHARACTER_NAMES).write_bytes(names)
    before = relevant(game)
    result, installed_report = run("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert digest(game / HEROES) == FIXED_HEROES
    assert digest(game / CHARACTER_NAMES) == FIXED_NAMES
    receipt = read_receipt(game)
    assert receipt["schema"] == "unofficial-medieval-total-war-patch-install-v8"
    for relative in (HEROES, CHARACTER_NAMES):
        row = receipt["files"][relative]
        assert row["original_sha256"] == before[relative]
        assert digest(game / STATE / row["snapshot_relative"]) == before[relative]
    result, removed = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert removed["restoration"] == "verified"
    assert relevant(game) == before


def test_character_data_unrecognized_edit_is_not_overwritten(games):
    game = games("Modded names")
    path = game / CHARACTER_NAMES
    modified = bytearray(path.read_bytes())
    modified[100] ^= 1
    path.write_bytes(modified)
    before = relevant(game)
    result, report = run("Install", game)
    assert result.returncode != 0
    assert report["code"] == "unsupported_data"
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not registry(game)


def test_character_data_oversized_edit_gets_actionable_refusal(games):
    game = games("Extended names")
    path = game / CHARACTER_NAMES
    path.write_bytes(path.read_bytes() + b"\r\nuser title\r\n")
    before = relevant(game)
    result, report = run("Install", game)
    assert result.returncode != 0
    assert report["code"] == "unsupported_data"
    assert "game-data mod" in report["message"]
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not registry(game)


def test_character_data_published_then_failed_install_rolls_back_both_files(games):
    game = games("Late data fault")
    before = relevant(game)
    result, report = run("Install", game, fault="throw:after-file-12")
    assert result.returncode != 0
    assert report["rollback"] == "verified", report
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not registry(game)


def test_character_data_changed_after_install_is_archived_before_exact_restore(games):
    game = games("Post install edit")
    before = relevant(game)
    installed(game)
    modified = (game / CHARACTER_NAMES).read_bytes() + b"\r\n// personal edit\r\n"
    (game / CHARACTER_NAMES).write_bytes(modified)
    result, removed = run("Restore", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert relevant(game) == before
    archive = Path(removed["recovery_archive"])
    assert (archive / "changed-NAMES.TXT").read_bytes() == modified
    manifest = json.loads((archive / "manifest.json").read_text(encoding="utf-8-sig"))
    assert any(row["file"] == "changed-NAMES.TXT" and
               row["source_relative"] == CHARACTER_NAMES for row in manifest["files"])


def test_character_data_supported_manual_change_after_install_is_archived_on_repair(games):
    game = games("Supported post install edit")
    before = relevant(game)
    installed(game)
    names = (game / CHARACTER_NAMES).read_bytes()
    partially_corrected = names.replace(b'{"Kilij Arslan"}', b'{"Kilij Arslan}')
    (game / CHARACTER_NAMES).write_bytes(partially_corrected)
    result, report = run("Install", game)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert digest(game / CHARACTER_NAMES) == FIXED_NAMES
    assert (Path(report["recovery_archive"]) / "changed-NAMES.TXT").read_bytes() == partially_corrected
    assert read_receipt(game)["files"][CHARACTER_NAMES]["original_sha256"] == before[CHARACTER_NAMES]
    result, _ = run("Restore", game)
    assert result.returncode == 0
    assert relevant(game) == before


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


def run_at_wait(operation, game, point, mutation, *, uninstaller=None, parchment_fix=False):
    source = uninstaller or game.parent / "synthetic-uninstaller.bin"
    if not source.exists():
        source.write_bytes(b"synthetic package uninstaller for lifecycle-only tests\x00\xff")
    env = dict(os.environ, MTW_ENABLE_LIFECYCLE_FAULTS="1")
    (game / ".umtwp-test-fixture").write_text("disposable lifecycle test\n")
    command = [str(ENGINE), "-Operation", operation, "-Target", str(game),
               "-PayloadDirectory", str(PAYLOAD), "-InstallerVersion", PRODUCT["version"],
               "-UninstallerSource", str(source), "-TestFault", "wait:" + point]
    if parchment_fix is not None:
        command += ["-ParchmentFix", str(int(parchment_fix))]
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
    staged_character_data = []

    def fail_after_data_staging():
        preparing = list(game.glob(".unofficial-medieval-patch-preparing-*"))
        assert len(preparing) == 1
        staged = {digest(path) for path in (preparing[0] / "after").glob("*.bin")}
        assert {FIXED_HEROES, FIXED_NAMES} <= staged
        staged_character_data.append(True)
        source.unlink()

    code, report, out, err = run_at_wait("Install", game, "before-stage-uninstaller",
                                        fail_after_data_staging, uninstaller=source)
    assert staged_character_data
    assert code != 0, (out, err)
    assert relevant(game) == before
    assert not (game / STATE).exists()
    assert not list(game.glob(".unofficial-medieval-patch-preparing-*"))
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
