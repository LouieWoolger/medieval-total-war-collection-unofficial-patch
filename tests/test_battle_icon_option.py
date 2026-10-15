"""Installer and payload contract for the independent battle unit icon repair."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"


def manifest(name: str) -> dict:
    return json.loads((RUNTIME / name).read_text(encoding="utf-8"))


def test_battle_icon_option_is_a_recommended_independent_choice() -> None:
    product = json.loads((ROOT / "config/product.json").read_text(encoding="utf-8"))
    installer = (ROOT / "installer.nsi").read_text(encoding="utf-8")
    support = (ROOT / "installer-support.nsh").read_text(encoding="utf-8")
    assert product["battle_icon_component_name"] == "Battle Unit Portrait Fix"
    assert 'StrCpy $BattleIconSelected "1"' in installer
    assert '${NSD_CreateCheckbox} 12 214 295 24 "${PRODUCT_BATTLE_ICON_COMPONENT_NAME}"' in installer
    assert '"/BATTLEICONFIX="' in installer
    assert 'battle_icon_fix=$BattleIconSelected' in support
    assert '/BATTLEICONFIX=$BattleIconSelected' in support
    assert 'Fixes unit portraits disappearing from battle unit cards after Alt+Tab.' in installer
    preview = (ROOT / "assets/battle-unit-icons.bmp").read_bytes()
    assert preview[:2] == b"BM"
    assert int.from_bytes(preview[18:22], "little") == 480
    assert int.from_bytes(preview[22:26], "little") == 270


def test_icon_on_off_payloads_are_distinct_and_do_not_change_other_files() -> None:
    on = manifest("payload-manifest-scroll-sprite-off.json")
    off = manifest("payload-manifest-scroll-sprite-off-icon-off.json")
    icon_only = manifest("payload-manifest-icon-only.json")
    fast_icon = manifest("payload-manifest-fast-icon.json")
    assert on["battle_icon_fix_enabled"] is True
    assert off["battle_icon_fix_enabled"] is False
    assert icon_only["battle_icon_fix_enabled"] is True
    assert fast_icon["battle_icon_fix_enabled"] is True
    assert on["files"]["D3D9.dll"]["sha256"] != off["files"]["D3D9.dll"]["sha256"]
    for name in on["files"].keys() - {"D3D9.dll"}:
        assert on["files"][name] == off["files"][name]
    assert set(icon_only["files"]) == {"D3D9.dll"}
    assert set(fast_icon["files"]) == {"D3D9.dll"}
    assert icon_only["files"]["D3D9.dll"]["sha256"] != fast_icon["files"]["D3D9.dll"]["sha256"]
