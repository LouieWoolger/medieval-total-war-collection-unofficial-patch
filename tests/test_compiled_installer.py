from __future__ import annotations

import hashlib
import importlib.util
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import re
import uuid
import sys

import pytest
from registry_isolation import (LEGACY_NAMES, read_registration, registration_name,
                                remove_test_registration, assert_empty_registration)
from game_fixture_assets import copy_campaign_text, copy_character_text
from native_helper import seal


ROOT = Path(__file__).resolve().parents[1]
DIST_INSTALLER = Path(os.environ.get("MTW_TEST_INSTALLER", str(ROOT / "dist" / "Unofficial Medieval Total War Collection Patch.exe")))
PAYLOAD = ROOT / "vendor" / "runtime"
SUPPORTED_EXE_HASH = "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
SCROLL_EXE_HASH = "50829CD084355D81EC94D6F4489D1F60E2EF7FA92983D0EAD07D43832DEEF15B"
SPRITE_EXE_HASH = "72A42C3635ED808E87CF85BAF24D39601376E4B0143B109B0A63771541B662D0"
COMBINED_EXE_HASH = "982921CEFDED31C298F249742B8A001BB1A823F77FB3965E01892A34ED12E627"
REPORTED_LAA_HASH = "9FDA59114FB470C6E2E65AC37019D044BB775EF4B76E00E6585D790B129E3710"
LAA_COMBINED_EXE_HASH = "05006A7779EC6CAA2FB4253609C7447AD8A7EE23C5847A302D7731C198091EB7"
RUNTIME_NAMES = ("D3D9.dll", "dgVoodoo_D3D9.dll", "ddraw.dll", "D3DImm.dll", "dgVoodoo.conf")
LOCALE_NAME = "Loc/Eng/CHANGES.TXT"
HEROES_NAME = "campmap/names/DEFAULT_HEROES.TXT"
NAMES_NAME = "Loc/Eng/NAMES.TXT"
REGION_TITLES_NAME = "Loc/Eng/DEFAULT_REGION_SPECIFIC.TXT"
FIXED_HEROES_HASH = "B9BB6F6000BB426736F2E113A0D05331A014C8351AB0D8C1B3FD2FE10705ADDB"
FIXED_NAMES_HASH = "3421E5FE033FF3CD9D710FFA38F465B970060C4095C56B590C22AEDFEF4A6F15"
FIXED_REGION_TITLES_HASH = "F75F5DAFE7319715C81ABE87AC9F6D1058944AC505DA53E3C015A7EB353BF1F7"
STOCK_LOCALE_HASH = "A4ED1815E5BBE23DC0B4279AFB9398E1FABC6A9A1F2A014DB96F5F25E2B5B3DC"
FAST_LOCALE_HASH = "2BC2C971F1AC2C564490A526C6E8B1AE255E2CECBC43B45EC0B6B45A8FDF0F5B"
ZOOM_LOCALE_HASH = "F2724F3F76CABB6289C327229988CEADF04D1D820BFBC0E8D7BDE52207FBC3D3"
FAST_ZOOM_LOCALE_HASH = "F8A6A90167B5E2DD90B1482E9FB5BFBC5B1A64FA2DB4949A84C859BE46B2E78D"


pytestmark = pytest.mark.skipif(
    os.environ.get("MTW_RUN_COMPILED_INSTALLER_TESTS") != "1",
    reason="set MTW_RUN_COMPILED_INSTALLER_TESTS=1 to run compiled installer tests",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def supported_exe_source() -> Path:
    value = os.environ.get("MTW_TEST_GAME_EXE")
    if not value:
        pytest.fail("MTW_TEST_GAME_EXE is required")
    path = Path(value)
    assert sha256(path) == SUPPORTED_EXE_HASH
    return path


def reported_laa_executable() -> bytes:
    script = ROOT / "tools" / "generate-laa-exe-variants.py"
    spec = importlib.util.spec_from_file_location("mtw_laa_variants", script)
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    data = generator.build_variants(supported_exe_source().read_bytes())[(0, 0, 0, "recompute")]
    assert hashlib.sha256(data).hexdigest().upper() == REPORTED_LAA_HASH
    return data


@pytest.fixture(autouse=True)
def isolated_uninstall_registration(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    legacy_before = {name: read_registration(name) for name in LEGACY_NAMES}
    games = []
    create_game = new_game

    def tracked_game(root: Path, name: str) -> Path:
        game = create_game(root, name)
        assert read_registration(registration_name(game)) is None
        games.append(game)
        return game

    monkeypatch.setattr(sys.modules[__name__], "new_game", tracked_game)
    try:
        yield
    finally:
        for game in games:
            remove_test_registration(game, tmp_path)
        assert {name: read_registration(name) for name in LEGACY_NAMES} == legacy_before


def new_game(root: Path, name: str) -> Path:
    game = root / name
    game.mkdir(parents=True)
    source = supported_exe_source()
    shutil.copy2(source, game / "Medieval_TW.exe")
    copy_campaign_text(source, game)
    copy_character_text(source, game)
    shutil.copy2(DIST_INSTALLER, game / DIST_INSTALLER.name)
    return game


def snapshot(game: Path) -> dict[str, str | None]:
    names = ["Medieval_TW.exe", LOCALE_NAME, HEROES_NAME, NAMES_NAME,
             REGION_TITLES_NAME, *RUNTIME_NAMES, "D3D8.dll"]
    names += [f"{name}.unofficial-patch.bak" for name in RUNTIME_NAMES]
    return {name: sha256(game / name) if (game / name).is_file() else None for name in names}


def expected_runtime() -> dict[str, str]:
    manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
    return {name: record["sha256"] for name, record in manifest["files"].items()}


def assert_runtime(game: Path) -> None:
    for name, expected in expected_runtime().items():
        assert sha256(game / name) == expected


def assert_registration(game: Path) -> None:
    values = read_registration(registration_name(game))
    assert values is not None
    assert Path(values["InstallLocation"][0]).resolve() == game.resolve()
    assert values["UninstallString"][0] == f'"{game / "Uninstall Unofficial Medieval Patch.exe"}"'


def run_installer(game: Path, *, terrain_fix: bool | None = None, scroll_fix: bool | None = None,
                  sprite_fix: bool | None = None,
                  tutorial_fix: bool | None = False,
                  parchment_fix: bool | None = False,
                  name_fix: bool | None = None,
                  fast_moves_fix: bool | None = None,
                  zoom_fix: bool | None = None,
                  key_fix: bool | None = None,
                  credits_fix: bool | None = False,
                  economy_fix: bool | None = None,
                  battle_icon_fix: bool | None = None) -> subprocess.CompletedProcess[str]:
    logs = game.parent / "diagnostics" / uuid.uuid4().hex
    logs.mkdir(parents=True)
    command = [str(game / DIST_INSTALLER.name), "/S", f"/LOGDIR={logs}"]
    if terrain_fix is not None:
        command.append(f"/TERRAINFIX={int(terrain_fix)}")
    if scroll_fix is not None:
        command.append(f"/SCROLLFIX={int(scroll_fix)}")
    if sprite_fix is not None:
        command.append(f"/SPRITEFIX={int(sprite_fix)}")
    if tutorial_fix is not None:
        command.append(f"/TUTORIALFIX={int(tutorial_fix)}")
    if parchment_fix is not None:
        command.append(f"/PARCHMENTFIX={int(parchment_fix)}")
    if name_fix is not None:
        command.append(f"/NAMEFIX={int(name_fix)}")
    if fast_moves_fix is not None:
        command.append(f"/FASTMOVESFIX={int(fast_moves_fix)}")
    if zoom_fix is not None:
        command.append(f"/ZOOMFIX={int(zoom_fix)}")
    if key_fix is not None:
        command.append(f"/KEYFIX={int(key_fix)}")
    if credits_fix is not None:
        command.append(f"/CREDITSFIX={int(credits_fix)}")
    if economy_fix is not None:
        command.append(f"/ECONOMYFIX={int(economy_fix)}")
    # Legacy lifecycle cases choose no wrapper when Terrain is deselected.
    # Test the installer's checked default separately with an explicit call.
    if battle_icon_fix is None:
        battle_icon_fix = terrain_fix is not False
    command.append(f"/BATTLEICONFIX={int(battle_icon_fix)}")
    result = subprocess.run(
        command,
        cwd=game,
        text=True,
        capture_output=True,
        timeout=180,
    )
    for log in logs.rglob("*.log"):
        encoding = "utf-16" if log.name == "installer.log" else "utf-8-sig"
        result.stdout += f"\n{log}\n" + log.read_text(encoding=encoding, errors="replace")
    return result


def test_compiled_name_choice_includes_marriage_titles_and_restores_text(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Name and title corrections")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    only_names = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                               tutorial_fix=False, parchment_fix=False, name_fix=True,
                               battle_icon_fix=False, fast_moves_fix=False)
    assert only_names.returncode == 0, (only_names.stdout, only_names.stderr)
    assert sha256(game / HEROES_NAME) == FIXED_HEROES_HASH
    assert sha256(game / NAMES_NAME) == FIXED_NAMES_HASH
    assert sha256(game / REGION_TITLES_NAME) == FIXED_REGION_TITLES_HASH
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["name_fix_enabled"] is True
    assert receipt["marriage_title_fix_enabled"] is True

    off = run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False,
                        tutorial_fix=False, parchment_fix=False, name_fix=False,
                        battle_icon_fix=False, fast_moves_fix=False)
    assert off.returncode == 0, (off.stdout, off.stderr)
    assert sha256(game / HEROES_NAME) == before[HEROES_NAME]
    assert sha256(game / NAMES_NAME) == before[NAMES_NAME]
    assert sha256(game / REGION_TITLES_NAME) == before[REGION_TITLES_NAME]
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["name_fix_enabled"] is False
    assert receipt["marriage_title_fix_enabled"] is False
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_balance_choice_installs_toggles_and_restores(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Independent balance option")
    save = game / "SaveGames" / "personal.cpg"
    save.parent.mkdir()
    save.write_bytes(b"existing personal save\x00\xff")
    save_hash = sha256(save)
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    options = dict(terrain_fix=False, scroll_fix=False, sprite_fix=False,
                   tutorial_fix=False, parchment_fix=False, name_fix=False,
                   fast_moves_fix=False, battle_icon_fix=False, economy_fix=True)

    only_balance = run_installer(game, **options)
    assert only_balance.returncode == 0, (only_balance.stdout, only_balance.stderr)
    assert sha256(game / "Medieval_TW.exe") == "61221C667368358D359C3D51D6692FE68729F747EF7B16403A85059B0E9C34DA"
    assert not (game / "D3D9.dll").exists()
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["economy_fix_enabled"] is True
    assert sha256(save) == save_hash

    disabled = run_installer(game, **{**options, "scroll_fix": True, "economy_fix": False})
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["economy_fix_enabled"] is False
    assert sha256(save) == save_hash

    reenabled = run_installer(game, **{**options, "scroll_fix": True})
    assert reenabled.returncode == 0, (reenabled.stdout, reenabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == "E720B97ABF4BF6B49DC2E547F96B60A67C6FE1069C8DCA5FEF814126106769D4"
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["economy_fix_enabled"] is True
    assert sha256(save) == save_hash

    removed = run_uninstaller(game)
    assert removed.returncode == 0, (removed.stdout, removed.stderr)
    assert snapshot(game) == before
    assert sha256(save) == save_hash


def test_compiled_keybinding_choice_installs_toggles_and_restores(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Independent keybinding option")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    spec = importlib.util.spec_from_file_location(
        "mtw_release_manifest", ROOT / "tools" / "build-release-manifest.py")
    manifest_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(manifest_module)
    variants = manifest_module.supported_executable_variants()
    key_only_hash = next(entry["sha256"] for entry in variants if
                         entry["key"] and not any(entry[name] for name in
                         ("scroll", "sprite", "tutorial", "ui", "economy", "laa")))
    scroll_key_hash = next(entry["sha256"] for entry in variants if
                           entry["key"] and entry["scroll"] and
                           not any(entry[name] for name in
                           ("sprite", "tutorial", "ui", "economy", "laa")))
    options = dict(terrain_fix=False, scroll_fix=False, sprite_fix=False,
                   tutorial_fix=False, parchment_fix=False, name_fix=False,
                   fast_moves_fix=False, battle_icon_fix=False,
                   economy_fix=False, zoom_fix=False, key_fix=True)
    fresh_default = run_installer(game, **{**options, "scroll_fix": True,
                                           "fast_moves_fix": None, "economy_fix": None,
                                           "zoom_fix": None, "key_fix": None})
    assert fresh_default.returncode == 0, (fresh_default.stdout, fresh_default.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    fresh_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    for choice in ("fast_moves_fix_enabled", "economy_fix_enabled",
                   "zoom_fix_enabled", "key_fix_enabled"):
        assert fresh_receipt[choice] is False

    enabled = run_installer(game, **options)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == key_only_hash
    assert not (game / "D3D9.dll").exists()
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["key_fix_enabled"] is True
    disabled = run_installer(game, **{**options, "scroll_fix": True, "key_fix": False})
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["key_fix_enabled"] is False
    reenabled = run_installer(game, **{**options, "scroll_fix": True})
    assert reenabled.returncode == 0, (reenabled.stdout, reenabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == scroll_key_hash
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["key_fix_enabled"] is True

    omitted_on_reinstall = run_installer(game, **{**options, "scroll_fix": True,
                                                  "credits_fix": None, "key_fix": None})
    assert omitted_on_reinstall.returncode == 0, (
        omitted_on_reinstall.stdout, omitted_on_reinstall.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["key_fix_enabled"] is False

    removed = run_uninstaller(game)
    assert removed.returncode == 0, (removed.stdout, removed.stderr)
    assert snapshot(game) == before


def test_compiled_tutorial_fix_is_recommended_independent_and_reversible(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Tutorial resolution option")
    before = snapshot(game)
    script = ROOT / "tools" / "generate-laa-exe-variants.py"
    spec = importlib.util.spec_from_file_location("mtw_tutorial_variants", script)
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    variants = generator.build_variants(supported_exe_source().read_bytes())
    expected = hashlib.sha256(variants[(0, 0, 1, "base")]).hexdigest().upper()
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"

    only_tutorial = run_installer(game, terrain_fix=False, scroll_fix=False,
                                  sprite_fix=False, tutorial_fix=True, fast_moves_fix=False)
    assert only_tutorial.returncode == 0, (only_tutorial.stdout, only_tutorial.stderr)
    assert sha256(game / "Medieval_TW.exe") == expected
    assert (game / "Medieval_TW.exe").read_bytes()[0x306A5C:0x306A62] == b"\xe9\x78\x00\x00\x00\x90"
    assert not (game / "D3D9.dll").exists()
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["tutorial_fix_enabled"] is True

    disabled = run_installer(game, terrain_fix=True, scroll_fix=False,
                             sprite_fix=False, tutorial_fix=False, fast_moves_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    assert (game / "D3D9.dll").is_file()
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["tutorial_fix_enabled"] is False

    default = run_installer(game, tutorial_fix=None)
    assert default.returncode == 0, (default.stdout, default.stderr)
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["tutorial_fix_enabled"] is True
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_character_parchment_choice_is_recommended_and_reversible(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Character parchment option")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    ui_only_hash = "1BF4B4D8123A1218E35D8D44E16ADE55ED0BDDF1A03B109299A034092524FBFA"

    enabled = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                            tutorial_fix=False, parchment_fix=True, fast_moves_fix=False,
                            battle_icon_fix=False)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == ui_only_hash
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["parchment_fix_enabled"] is True

    disabled = run_installer(game, terrain_fix=True, scroll_fix=False, sprite_fix=False,
                             tutorial_fix=False, parchment_fix=False, fast_moves_fix=False,
                             battle_icon_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["parchment_fix_enabled"] is False

    default = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                            tutorial_fix=False, parchment_fix=None, fast_moves_fix=False,
                            battle_icon_fix=False)
    assert default.returncode == 0, (default.stdout, default.stderr)
    assert sha256(game / "Medieval_TW.exe") == ui_only_hash
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["parchment_fix_enabled"] is True
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_character_parchment_reinstalls_extended_executable(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Extended parchment reinstall")
    before = snapshot(game)
    options = dict(terrain_fix=True, scroll_fix=True, sprite_fix=True,
                   tutorial_fix=False, parchment_fix=True,
                   fast_moves_fix=False, battle_icon_fix=False)

    first = run_installer(game, **options)
    assert first.returncode == 0, (first.stdout, first.stderr)
    executable = game / "Medieval_TW.exe"
    assert executable.stat().st_size == 0x4EB000
    installed_hash = sha256(executable)

    second = run_installer(game, **options)
    assert second.returncode == 0, (second.stdout, second.stderr)
    assert sha256(executable) == installed_hash

    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_battle_icon_choice_is_independent_and_reversible(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Battle icon option")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    choices = (
        (False, False, True, "payload-manifest-icon-only.json"),
        (False, True, True, "payload-manifest-fast-icon.json"),
        (False, True, False, "payload-manifest-fast-only.json"),
        (True, False, False, "payload-manifest-scroll-sprite-off-icon-off.json"),
        (True, False, True, "payload-manifest-scroll-sprite-off.json"),
        (True, True, False, "payload-manifest-scroll-sprite-off-fast-icon-off.json"),
        (True, True, True, "payload-manifest-scroll-sprite-off-fast.json"),
    )
    installation_id = None
    for terrain, fast, icon, manifest_name in choices:
        result = run_installer(game, terrain_fix=terrain, scroll_fix=False,
                               sprite_fix=False, tutorial_fix=False,
                               fast_moves_fix=fast, battle_icon_fix=icon)
        assert result.returncode == 0, (result.stdout, result.stderr)
        manifest = json.loads((PAYLOAD / manifest_name).read_text(encoding="utf-8"))
        expected = manifest["files"]["D3D9.dll"]["sha256"]
        receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
        assert sha256(game / "D3D9.dll") == expected
        assert receipt["files"]["D3D9.dll"]["installed_sha256"] == expected
        assert receipt["battle_icon_fix_enabled"] is icon
        assert receipt["terrain_fix_enabled"] is terrain
        assert receipt["fast_moves_fix_enabled"] is fast
        if installation_id is None:
            installation_id = receipt["installation_id"]
        assert receipt["installation_id"] == installation_id
        for name in RUNTIME_NAMES[1:]:
            assert (game / name).is_file() is terrain
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_scroll_choice_changes_exe_and_can_toggle(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Scroll choice")
    before = snapshot(game)
    proxy = expected_runtime()["D3D9.dll"]

    disabled = run_installer(game, scroll_fix=False, sprite_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    assert sha256(game / "D3D9.dll") == proxy
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    disabled_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert disabled_receipt["files"]["D3D9.dll"]["installed_sha256"] == proxy
    for name in RUNTIME_NAMES[1:]:
        assert sha256(game / name) == expected_runtime()[name]

    enabled = run_installer(game, scroll_fix=True, sprite_fix=False)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    assert sha256(game / "D3D9.dll") == proxy
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    enabled_receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert enabled_receipt["installation_id"] == disabled_receipt["installation_id"]
    assert enabled_receipt["files"]["D3D9.dll"]["installed_sha256"] == proxy
    assert enabled_receipt["files"]["Medieval_TW.exe"]["installed_sha256"] == SCROLL_EXE_HASH

    disabled_again = run_installer(game, scroll_fix=False, sprite_fix=False)
    assert disabled_again.returncode == 0, (disabled_again.stdout, disabled_again.stderr)
    assert sha256(game / "D3D9.dll") == proxy
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_all_seven_independent_fix_combinations(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Independent fixes")
    before = snapshot(game)
    choices = ((False, False, True), (False, True, False), (False, True, True),
               (True, False, False), (True, False, True),
               (True, True, False), (True, True, True))
    installation_id = None
    for terrain, scroll, sprite in choices:
        manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
        result = run_installer(game, terrain_fix=terrain, scroll_fix=scroll, sprite_fix=sprite)
        assert result.returncode == 0, (result.stdout, result.stderr)
        expected_hash = manifest["files"]["D3D9.dll"]["sha256"]
        if terrain:
            assert sha256(game / "D3D9.dll") == expected_hash
        else:
            assert all(not (game / name).exists() for name in RUNTIME_NAMES)
        exe_hash = {(False, False): SUPPORTED_EXE_HASH, (True, False): SCROLL_EXE_HASH,
                    (False, True): SPRITE_EXE_HASH, (True, True): COMBINED_EXE_HASH}[(scroll, sprite)]
        assert sha256(game / "Medieval_TW.exe") == exe_hash
        assert manifest["scroll_fix_enabled"] is False
        assert manifest["sprite_fix_enabled"] is False
        receipt = json.loads((game / ".unofficial-medieval-total-war-patch" / "install-manifest.json").read_text(encoding="utf-8-sig"))
        assert receipt["files"]["D3D9.dll"]["installed_sha256"] == (expected_hash if terrain else None)
        assert receipt["terrain_fix_enabled"] is terrain
        assert receipt["scroll_fix_enabled"] is scroll
        assert receipt["sprite_fix_enabled"] is sprite
        assert receipt["sprite_delivery"] == "direct-exe"
        assert receipt["schema"] == "unofficial-medieval-total-war-patch-install-v8"
        if installation_id is None:
            installation_id = receipt["installation_id"]
        assert receipt["installation_id"] == installation_id
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_fast_computer_moves_is_independent_reversible_and_combines_with_terrain(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Fast computer moves combinations")
    before = snapshot(game)
    base_manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
    variants = (
        (False, True, "payload-manifest-fast-only.json"),
        (True, True, "payload-manifest-scroll-sprite-off-fast.json"),
        (True, False, "payload-manifest-scroll-sprite-off.json"),
    )
    installation_id = None
    for terrain, fast, manifest_name in variants:
        result = run_installer(game, terrain_fix=terrain, scroll_fix=False,
                               sprite_fix=False, fast_moves_fix=fast)
        assert result.returncode == 0, (result.stdout, result.stderr)
        receipt = json.loads((game / ".unofficial-medieval-total-war-patch" / "install-manifest.json")
                             .read_text(encoding="utf-8-sig"))
        assert receipt["schema"] == "unofficial-medieval-total-war-patch-install-v8"
        assert receipt["fast_moves_fix_enabled"] is fast
        assert receipt["terrain_fix_enabled"] is terrain
        if installation_id is None:
            installation_id = receipt["installation_id"]
        assert receipt["installation_id"] == installation_id
        expected_proxy = (json.loads((PAYLOAD / manifest_name).read_text(encoding="utf-8"))
                          ["files"]["D3D9.dll"]["sha256"] if manifest_name else None)
        assert receipt["files"]["D3D9.dll"]["installed_sha256"] == expected_proxy
        assert (sha256(game / "D3D9.dll") if expected_proxy else None) == expected_proxy
        if not expected_proxy:
            assert not (game / "D3D9.dll").exists()
        if terrain:
            for name in RUNTIME_NAMES[1:]:
                assert sha256(game / name) == base_manifest["files"][name]["sha256"]
        else:
            for name in RUNTIME_NAMES[1:]:
                assert not (game / name).exists()
        assert sha256(game / LOCALE_NAME) == (FAST_LOCALE_HASH if fast else STOCK_LOCALE_HASH)
        assert receipt["files"][LOCALE_NAME]["installed_sha256"] == (
            FAST_LOCALE_HASH if fast else STOCK_LOCALE_HASH)
        assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_fast_computer_moves_rejects_unsupported_locale_without_game_mutation(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unsupported fast campaign text")
    locale = game / LOCALE_NAME
    altered = bytearray(locale.read_bytes())
    altered[100] ^= 1
    locale.write_bytes(altered)
    before = snapshot(game)
    result = run_installer(game, terrain_fix=False, scroll_fix=False,
                           sprite_fix=False, fast_moves_fix=True)
    assert result.returncode != 0
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_compiled_keep_campaign_zoom_composes_and_restores_all_runtime_choices(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Campaign zoom combinations")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    variants = (
        (False, False, False, "zoom-only"),
        (False, True, False, "fast-only-zoom"),
        (False, False, True, "icon-only-zoom"),
        (False, True, True, "fast-icon-zoom"),
        (True, False, False, "scroll-sprite-off-icon-off-zoom"),
        (True, True, False, "scroll-sprite-off-fast-icon-off-zoom"),
        (True, False, True, "scroll-sprite-off-zoom"),
        (True, True, True, "scroll-sprite-off-fast-zoom"),
    )
    installation_id = None
    for terrain, fast, icon, name in variants:
        economy = terrain and fast and icon
        installed = run_installer(game, terrain_fix=terrain, scroll_fix=False, sprite_fix=False,
                                  tutorial_fix=False, parchment_fix=False, name_fix=False,
                                  fast_moves_fix=fast, zoom_fix=True, battle_icon_fix=icon,
                                  economy_fix=economy)
        assert installed.returncode == 0, (installed.stdout, installed.stderr)
        receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
        manifest = json.loads((PAYLOAD / f"payload-manifest-{name}.json").read_text(encoding="utf-8"))
        expected = manifest["files"]["D3D9.dll"]["sha256"]
        assert sha256(game / "D3D9.dll") == expected
        assert receipt["files"]["D3D9.dll"]["installed_sha256"] == expected
        assert receipt["zoom_fix_enabled"] is True
        assert receipt["fast_moves_fix_enabled"] is fast
        assert receipt["terrain_fix_enabled"] is terrain
        assert receipt["battle_icon_fix_enabled"] is icon
        assert receipt["economy_fix_enabled"] is economy
        assert (sha256(game / "Medieval_TW.exe") != SUPPORTED_EXE_HASH) is economy
        assert sha256(game / LOCALE_NAME) == (FAST_ZOOM_LOCALE_HASH if fast else ZOOM_LOCALE_HASH)
        assert (game / "dgVoodoo_D3D9.dll").is_file() is terrain
        if installation_id is None:
            installation_id = receipt["installation_id"]
        assert receipt["installation_id"] == installation_id

    disabled = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                             tutorial_fix=False, parchment_fix=False, name_fix=False,
                             fast_moves_fix=True, zoom_fix=False, battle_icon_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["zoom_fix_enabled"] is False
    assert sha256(game / LOCALE_NAME) == FAST_LOCALE_HASH
    assert sha256(game / "D3D9.dll") == json.loads(
        (PAYLOAD / "payload-manifest-fast-only.json").read_text(encoding="utf-8"))["files"]["D3D9.dll"]["sha256"]
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_keep_campaign_zoom_rejects_modified_locale_without_mutation(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unsupported zoom text")
    locale = game / LOCALE_NAME
    changed = bytearray(locale.read_bytes())
    changed[100] ^= 1
    locale.write_bytes(changed)
    before = snapshot(game)
    result = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                           fast_moves_fix=False, zoom_fix=True, battle_icon_fix=False)
    assert result.returncode != 0
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_compiled_repairs_stock_exe_restored_by_storefront_verification(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Storefront EXE restoration")
    before = snapshot(game)
    assert run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False).returncode == 0
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    original_id = json.loads(receipt_path.read_text(encoding="utf-8-sig"))["installation_id"]
    shutil.copy2(supported_exe_source(), game / "Medieval_TW.exe")
    assert sha256(game / "Medieval_TW.exe") == SUPPORTED_EXE_HASH
    repaired = run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    assert receipt["installation_id"] == original_id
    assert receipt["repair_count"] == 1
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def process_has_exited(pid: int) -> bool:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        error = ctypes.get_last_error()
        assert error == 87, f"Cannot inspect diagnostic PID {pid}: Win32 {error}"
        return True
    try:
        return kernel.WaitForSingleObject(handle, 0) == 0
    finally:
        kernel.CloseHandle(handle)


def run_uninstaller(game: Path, *, expect_removal: bool = True, cwd: Path | None = None,
                    registered: bool = False) -> subprocess.CompletedProcess[str]:
    path = game / "Uninstall Unofficial Medieval Patch.exe"
    logs = game.parent / "diagnostics" / uuid.uuid4().hex
    logs.mkdir(parents=True)
    command = [str(path), "/S", f"/LOGDIR={logs}"]
    if registered:
        values = read_registration(registration_name(game))
        assert values is not None, "The scoped fixture has no uninstall registration"
        assert Path(values["InstallLocation"][0]).resolve() == game.resolve()
        quiet = values["QuietUninstallString"][0]
        assert quiet == f'"{path}" /S', "Refusing an unexpected registered command"
        # Execute the actual registered string after validating this fixture's
        # target, with only the test-owned diagnostic directory appended.
        command = quiet + " " + subprocess.list2cmdline([f"/LOGDIR={logs}"])
    result = subprocess.run(command, cwd=cwd or game, text=True, capture_output=True, timeout=180)
    # Normal NSIS uninstall self-copies. The outer launcher's return is not the
    # result of the actual uninstaller: await its durable terminal record and PID.
    deadline = time.monotonic() + 180
    diagnostic = ""
    while time.monotonic() < deadline:
        candidates = list(logs.rglob("installer.log"))
        for candidate in candidates:
            try:
                diagnostic = candidate.read_text(encoding="utf-16")
            except (OSError, UnicodeError):
                continue
            pids = re.findall(r"installer_pid=(\d+)", diagnostic)
            children = re.findall(r"child_pid=(\d+)", diagnostic)
            terminal = any(value in diagnostic for value in (
                "result=success operation=uninstall", "result=failure", "result=completed_with_warning"))
            if terminal and pids and all(process_has_exited(int(pid)) for pid in pids + children):
                break
        else:
            time.sleep(0.1)
            continue
        break
    else:
        pytest.fail(f"No completed uninstaller child within 180s. Diagnostics: {logs}\n{diagnostic}")
    if expect_removal:
        assert "result=success operation=uninstall" in diagnostic, diagnostic
        assert "child_exit=0" in diagnostic, diagnostic
        deadline = time.monotonic() + 10
        while path.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
    result.stdout += diagnostic
    return result


def assert_round_trip(game: Path, before: dict[str, str | None]) -> None:
    install = run_installer(game)
    assert install.returncode == 0, (install.stdout, install.stderr)
    assert_runtime(game)
    assert_registration(game)
    assert (game / ".unofficial-medieval-total-war-patch" / "install-manifest.json").is_file()
    restore = run_uninstaller(game)
    assert restore.returncode == 0, (restore.stdout, restore.stderr)
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    assert_empty_registration(registration_name(game))


def test_compiled_credits_only_default_installs_and_restores_clean_game(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Credits only from clean game")
    before = snapshot(game)
    installed = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                              tutorial_fix=False, parchment_fix=False, name_fix=False,
                              fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                              battle_icon_fix=False, credits_fix=None)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    manifest = json.loads((PAYLOAD / "payload-manifest-credits-only.json").read_text(encoding="utf-8"))
    expected = manifest["files"]["D3D9.dll"]["sha256"]
    receipt = json.loads((game / ".unofficial-medieval-total-war-patch" / "install-manifest.json")
                         .read_text(encoding="utf-8-sig"))
    assert receipt["credits_flash_fix_enabled"] is True
    assert receipt["terrain_fix_enabled"] is False
    assert receipt["zoom_fix_enabled"] is False
    assert receipt["files"]["D3D9.dll"]["installed_sha256"] == expected
    assert sha256(game / "D3D9.dll") == expected
    assert not (game / "dgVoodoo_D3D9.dll").exists()
    assert_registration(game)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before
    assert_empty_registration(registration_name(game))


def test_compiled_prior_receipt_without_credits_field_upgrades_and_switches_back(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Older receipt before Credits option")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    first = run_installer(game, terrain_fix=True, scroll_fix=False, sprite_fix=False,
                          tutorial_fix=False, parchment_fix=False, name_fix=False,
                          fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                          battle_icon_fix=True, credits_fix=False)
    assert first.returncode == 0, (first.stdout, first.stderr)
    prior = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    original_id = prior["installation_id"]
    assert prior.pop("credits_flash_fix_enabled") is False
    seal(prior)
    receipt_path.write_text(json.dumps(prior, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")

    retained = run_installer(game, terrain_fix=True, scroll_fix=False, sprite_fix=False,
                             tutorial_fix=False, parchment_fix=False, name_fix=False,
                             fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                             battle_icon_fix=True, credits_fix=None)
    assert retained.returncode == 0, (retained.stdout, retained.stderr)
    assert json.loads(receipt_path.read_text(encoding="utf-8-sig"))["credits_flash_fix_enabled"] is False
    assert sha256(game / "D3D9.dll") == json.loads(
        (PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))[
            "files"]["D3D9.dll"]["sha256"]

    enabled = run_installer(game, terrain_fix=True, scroll_fix=False, sprite_fix=False,
                            tutorial_fix=False, parchment_fix=False, name_fix=False,
                            fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                            battle_icon_fix=True, credits_fix=True)
    assert enabled.returncode == 0, (enabled.stdout, enabled.stderr)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    on_manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off-credits.json")
                             .read_text(encoding="utf-8"))
    assert receipt["installation_id"] == original_id
    assert receipt["credits_flash_fix_enabled"] is True
    assert receipt["files"]["D3D9.dll"]["installed_sha256"] == on_manifest["files"]["D3D9.dll"]["sha256"]
    assert sha256(game / "D3D9.dll") == on_manifest["files"]["D3D9.dll"]["sha256"]

    disabled = run_installer(game, terrain_fix=True, scroll_fix=False, sprite_fix=False,
                             tutorial_fix=False, parchment_fix=False, name_fix=False,
                             fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                             battle_icon_fix=True, credits_fix=False)
    assert disabled.returncode == 0, (disabled.stdout, disabled.stderr)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    off_manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json")
                              .read_text(encoding="utf-8"))
    assert receipt["installation_id"] == original_id
    assert receipt["credits_flash_fix_enabled"] is False
    assert sha256(game / "D3D9.dll") == off_manifest["files"]["D3D9.dll"]["sha256"]
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before
    assert_empty_registration(registration_name(game))


def test_compiled_prior_explicit_credits_choice_survives_upgrade_and_repair(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Credits preference across upgrades")
    before = snapshot(game)
    receipt_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    choices = ((False, False), (None, False), (True, True), (None, True),
               (False, False), (None, False))
    installation_id = None
    for requested, expected in choices:
        result = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                               tutorial_fix=False, parchment_fix=False, name_fix=True,
                               fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                               battle_icon_fix=False, credits_fix=requested)
        assert result.returncode == 0, (result.stdout, result.stderr)
        receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
        assert receipt["credits_flash_fix_enabled"] is expected
        assert receipt["name_fix_enabled"] is True
        if installation_id is None:
            installation_id = receipt["installation_id"]
        assert receipt["installation_id"] == installation_id
        if expected:
            assert sha256(game / "D3D9.dll") == json.loads(
                (PAYLOAD / "payload-manifest-credits-only.json").read_text(encoding="utf-8"))[
                    "files"]["D3D9.dll"]["sha256"]
        else:
            assert not (game / "D3D9.dll").exists()
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before
    assert_empty_registration(registration_name(game))


def test_compiled_credits_only_to_name_only_removes_proxy_and_restores(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Credits only to Name only")
    before = snapshot(game)
    first = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                          tutorial_fix=False, parchment_fix=False, name_fix=False,
                          fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                          battle_icon_fix=False, credits_fix=True)
    assert first.returncode == 0, (first.stdout, first.stderr)
    assert (game / "D3D9.dll").is_file()
    second = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                           tutorial_fix=False, parchment_fix=False, name_fix=True,
                           fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                           battle_icon_fix=False, credits_fix=False)
    assert second.returncode == 0, (second.stdout, second.stderr)
    assert not (game / "D3D9.dll").exists()
    receipt = json.loads((game / ".unofficial-medieval-total-war-patch" / "install-manifest.json")
                         .read_text(encoding="utf-8-sig"))
    assert receipt["credits_flash_fix_enabled"] is False
    assert receipt["name_fix_enabled"] is True
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before
    assert_empty_registration(registration_name(game))


def test_compiled_credits_only_preserves_unrecognized_existing_wrapper(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Credits only foreign wrapper")
    (game / "D3D9.dll").write_bytes(b"unrecognized existing D3D9 proxy")
    before = snapshot(game)
    result = run_installer(game, terrain_fix=False, scroll_fix=False, sprite_fix=False,
                           tutorial_fix=False, parchment_fix=False, name_fix=False,
                           fast_moves_fix=False, zoom_fix=False, economy_fix=False,
                           battle_icon_fix=False, credits_fix=True)
    assert result.returncode != 0
    assert '"code":"wrapper_conflict"' in result.stdout
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    assert read_registration(registration_name(game)) is None


def test_compiled_clean_install_and_uninstall(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Clean game with spaces")
    assert_round_trip(game, snapshot(game))


def test_compiled_character_data_fixes_share_one_transaction_and_restore_originals(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Character data")
    before = snapshot(game)
    installed = run_installer(game)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    assert sha256(game / HEROES_NAME) == FIXED_HEROES_HASH
    assert sha256(game / NAMES_NAME) == FIXED_NAMES_HASH
    receipt = json.loads((game / ".unofficial-medieval-total-war-patch/install-manifest.json")
                         .read_text(encoding="utf-8-sig"))
    assert receipt["schema"] == "unofficial-medieval-total-war-patch-install-v8"
    for name in (HEROES_NAME, NAMES_NAME):
        row = receipt["files"][name]
        assert row["original_sha256"] == before[name]
        assert row["installed_sha256"] == sha256(game / name)
        assert sha256(game / ".unofficial-medieval-total-war-patch" / row["snapshot_relative"]) == before[name]
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_reported_laa_install_repair_deselect_and_exact_uninstall(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Reported LAA game")
    original = reported_laa_executable()
    (game / "Medieval_TW.exe").write_bytes(original)
    before = snapshot(game)
    installed = run_installer(game, terrain_fix=True, scroll_fix=True,
                              sprite_fix=True, fast_moves_fix=True)
    assert installed.returncode == 0, (installed.stdout, installed.stderr)
    assert sha256(game / "Medieval_TW.exe") == LAA_COMBINED_EXE_HASH
    expected = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off-fast.json")
                          .read_text(encoding="utf-8"))["files"]["D3D9.dll"]["sha256"]
    assert sha256(game / "D3D9.dll") == expected
    assert sha256(game / LOCALE_NAME) == FAST_LOCALE_HASH
    repaired = run_installer(game, terrain_fix=True, scroll_fix=False,
                             sprite_fix=False, fast_moves_fix=True)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert (game / "Medieval_TW.exe").read_bytes() == original
    assert sha256(game / "D3D9.dll") == expected
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_registered_quiet_uninstall_from_another_directory(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Registered game")
    before = snapshot(game)
    assert run_installer(game).returncode == 0
    caller = tmp_path / "Other working directory"
    caller.mkdir()
    sentinel = caller / "personal.txt"
    sentinel.write_bytes(b"unrelated working directory")
    result = run_uninstaller(game, registered=True, cwd=caller)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert snapshot(game) == before
    assert_empty_registration(registration_name(game))
    assert not (game / "Uninstall Unofficial Medieval Patch.exe").exists()
    assert sentinel.read_bytes() == b"unrelated working directory"


def test_compiled_pre_win7_component_refusal_precedes_helper_launch(tmp_path: Path, monkeypatch) -> None:
    """Exercise the real NSIS refusal branch; this is not execution on Windows XP."""
    game = new_game(tmp_path, "Unsupported component OS")
    (game / ".umtwp-test-fixture").write_text("disposable package capability fixture")
    before = {str(p.relative_to(game)): sha256(p) for p in game.rglob("*") if p.is_file()}
    monkeypatch.setenv("MTW_ENABLE_LIFECYCLE_FAULTS", "1")
    monkeypatch.setenv("MTW_TEST_COMPONENT_OS", "pre-win7")
    result = run_installer(game)
    assert result.returncode == 2, result.stdout
    assert "error=unsupported_component_os" in result.stdout
    assert "no_selection=1" in result.stdout
    assert "child_pid=" not in result.stdout
    assert "phase=prepare-uninstaller" not in result.stdout
    assert {str(p.relative_to(game)): sha256(p) for p in game.rglob("*") if p.is_file()} == before
    assert read_registration(registration_name(game)) is None


def test_compiled_scrolling_only_bypasses_terrain_platform_gate(tmp_path: Path, monkeypatch) -> None:
    """A synthetic pre-Win7 probe checks selection routing, not OS compatibility."""
    game = new_game(tmp_path, "Scrolling-only platform selection")
    (game / ".umtwp-test-fixture").write_text("disposable package capability fixture")
    before = snapshot(game)
    monkeypatch.setenv("MTW_ENABLE_LIFECYCLE_FAULTS", "1")
    monkeypatch.setenv("MTW_TEST_COMPONENT_OS", "pre-win7")
    result = run_installer(game, terrain_fix=False, scroll_fix=True, sprite_fix=False)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert sha256(game / "Medieval_TW.exe") == SCROLL_EXE_HASH
    assert all(not (game / name).exists() for name in RUNTIME_NAMES)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_invalid_component_combinations_leave_game_untouched(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Invalid component combinations")
    before = snapshot(game)
    for terrain, scroll, sprite in ((False, False, False),):
        result = run_installer(game, terrain_fix=terrain, scroll_fix=scroll, sprite_fix=sprite,
                               name_fix=False)
        assert result.returncode == 2, (result.stdout, result.stderr)
        assert snapshot(game) == before
        assert read_registration(registration_name(game)) is None


def test_compiled_custom_config_is_replaced_and_restored(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing custom config")
    config = game / "dgVoodoo.conf"
    config.write_text(
        "; user-maintained dgVoodoo profile\n"
        "[GeneralExt]\n"
        "DesktopResolution = 1920x1080\n",
        encoding="utf-8",
    )
    before = snapshot(game)
    original_hash = sha256(config)

    install = run_installer(game)
    assert install.returncode == 0, (install.stdout, install.stderr)
    assert_runtime(game)
    assert sha256(game / "dgVoodoo.conf.unofficial-patch.bak") == original_hash

    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before


def test_compiled_managed_repair_replaces_modified_config(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Managed custom config")
    before_install = snapshot(game)
    assert run_installer(game).returncode == 0

    (game / "dgVoodoo.conf").write_text(
        "; changed after the previous patch installation\n"
        "[GeneralExt]\n"
        "DesktopResolution = 2560x1440\n",
        encoding="utf-8",
    )
    repaired = run_installer(game)
    assert repaired.returncode == 0, (repaired.stdout, repaired.stderr)
    assert_runtime(game)

    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before_install


def test_compiled_receiptless_current_patch_runtime_restored_on_removal(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing current runtime")
    for name in RUNTIME_NAMES:
        shutil.copy2(PAYLOAD / name, game / name)
    before = snapshot(game)
    first = run_installer(game)
    assert first.returncode == 0
    manifest_path = game / ".unofficial-medieval-total-war-patch" / "install-manifest.json"
    first_receipt = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    assert first_receipt["preinstall_mode"] == "r293-key-text"
    assert first_receipt["recovery_archive"] == ""
    for name in RUNTIME_NAMES:
        assert first_receipt["files"][name]["baseline_kind"] == "verified-preinstall"
        assert first_receipt["files"][name]["original_sha256"] == before[name]
    second = run_installer(game)
    assert second.returncode == 0
    second_receipt = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    assert second_receipt["installation_id"] == first_receipt["installation_id"]
    assert second_receipt["repair_count"] == 1
    assert second_receipt["recovery_archive"] == ""
    installed_manifest = json.loads((PAYLOAD / "payload-manifest-scroll-sprite-off.json").read_text(encoding="utf-8"))
    for name, expected in expected_runtime().items():
        assert second_receipt["files"][name]["installed_sha256"] == expected
        assert second_receipt["files"][name]["installed_length"] == installed_manifest["files"][name]["length"]
    assert second_receipt["installer_sha256"] == sha256(game / DIST_INSTALLER.name)
    assert_runtime(game)
    assert run_uninstaller(game).returncode == 0
    after = snapshot(game)
    assert after == before
    assert list(game.glob(".medieval-recovery-*")) == []


def test_compiled_two_folders_have_independent_registrations(tmp_path: Path) -> None:
    first = new_game(tmp_path, "First game")
    second = new_game(tmp_path, "Second game")
    for game in (first, second):
        assert run_installer(game).returncode == 0
        assert_registration(game)
    second_registration = read_registration(registration_name(second))
    assert registration_name(first) != registration_name(second)
    assert run_uninstaller(first, cwd=second).returncode == 0
    assert_empty_registration(registration_name(first))
    assert read_registration(registration_name(second)) == second_registration
    assert_runtime(second)
    assert run_uninstaller(second).returncode == 0
    assert_empty_registration(registration_name(second))


def test_compiled_unsupported_executable_is_zero_change(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unsupported executable")
    data = bytearray((game / "Medieval_TW.exe").read_bytes())
    data[-1] ^= 0xFF
    (game / "Medieval_TW.exe").write_bytes(data)
    before = snapshot(game)
    result = run_installer(game)
    assert result.returncode != 0
    # Guarded initialization refuses this identity before opening helper.log.
    # The captured Human console must still explain the real refusal.
    assert 'This Medieval_TW.exe build is unsupported (SHA-256 ' in result.stdout
    assert 'No files were changed.' in result.stdout
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()


def test_compiled_unknown_wrapper_is_zero_change(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Unknown wrapper")
    (game / "D3D9.dll").write_bytes(b"community wrapper with unknown identity")
    before = snapshot(game)
    result = run_installer(game)
    assert result.returncode != 0
    assert '"code":"wrapper_conflict"' in result.stdout
    assert snapshot(game) == before


def test_compiled_existing_backup_is_never_overwritten(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Existing backup")
    sidecar = game / "D3D9.dll.unofficial-patch.bak"
    sidecar.write_bytes(b"older backup remains authoritative to its owner")
    before = snapshot(game)
    assert_round_trip(game, before)


def test_compiled_game_running_refuses_without_changes(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Running game")
    before = snapshot(game)
    # Hold the actual supported target image without executing game code.
    process = subprocess.Popen(
        [str(game / "Medieval_TW.exe")],
        cwd=game, creationflags=0x00000004,  # CREATE_SUSPENDED
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        assert process.poll() is None
        result = run_installer(game)
        assert result.returncode != 0
        assert '"code":"game_running"' in result.stdout
        assert snapshot(game) == before
    finally:
        process.terminate()
        process.wait(timeout=10)


def test_compiled_unrelated_same_basename_process_does_not_block_target(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Independent game")
    helper_dir = tmp_path / "process helper"
    helper_dir.mkdir()
    helper = helper_dir / "Medieval_TW.exe"
    shutil.copy2(Path(os.environ["SystemRoot"]) / "System32" / "ping.exe", helper)
    before = snapshot(game)
    process = subprocess.Popen(
        [str(helper), "-t", "127.0.0.1"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(0.3)
        assert_round_trip(game, before)
    finally:
        process.terminate()
        process.wait(timeout=10)


def test_compiled_short_temp_alias_round_trip(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    temp = Path(os.environ["LOCALAPPDATA"]) / "Temp"
    assert temp.is_dir()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetShortPathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    kernel.GetShortPathNameW.restype = wintypes.DWORD
    buffer = ctypes.create_unicode_buffer(32768)
    length = kernel.GetShortPathNameW(str(temp), buffer, len(buffer))
    assert 0 < length < len(buffer), ctypes.get_last_error()
    if "~" not in buffer.value:
        pytest.skip("This user's temporary directory has no 8.3 alias")
    monkeypatch.setenv("TEMP", buffer.value)
    monkeypatch.setenv("TMP", buffer.value)
    game = new_game(tmp_path, "Short temporary path")
    assert_round_trip(game, snapshot(game))


def test_compiled_unicode_path_round_trip(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Mediæval Ünicode 騎士")
    assert_round_trip(game, snapshot(game))


def test_compiled_restore_archives_user_modified_file(tmp_path: Path) -> None:
    game = new_game(tmp_path, "User modified")
    before_install = snapshot(game)
    assert run_installer(game).returncode == 0
    changed = b"user replacement after installation"
    (game / "D3D9.dll").write_bytes(changed)
    assert run_uninstaller(game).returncode == 0
    assert snapshot(game) == before_install
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    archives = list(game.glob(".medieval-recovery-*/D3D9.dll"))
    assert len(archives) == 1
    assert archives[0].read_bytes() == changed
    assert_empty_registration(registration_name(game))


def test_compiled_elevation_handoff_rejects_another_account(tmp_path: Path) -> None:
    game = new_game(tmp_path, "Account-bound handoff ü")
    before = snapshot(game)
    logs = tmp_path / "handoff diagnostics"
    # NSIS /D= consumes the remaining, unquoted command line. Mirror the
    # elevation launcher rather than Python's generic argv quoting rules.
    command = f'"{game / DIST_INSTALLER.name}" /S /REQUIREOWNER=S-1-5-18 /LOGDIR="{logs}" /D={game}'
    result = subprocess.run(command, cwd=tmp_path,
                            capture_output=True, timeout=60)
    assert result.returncode == 2
    assert snapshot(game) == before
    assert not (game / ".unofficial-medieval-total-war-patch").exists()
    assert read_registration(registration_name(game)) is None
    console = "\n".join(p.read_text(encoding="utf-8") for p in logs.rglob("helper-console.log"))
    assert "same Windows account" in console
