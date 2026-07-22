from __future__ import annotations

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "installer.nsi"


def script_text() -> str:
    return SCRIPT.read_text(encoding="utf-8")


def test_identity_is_centralized_and_unicode_mui2_metadata_is_complete() -> None:
    product = json.loads((ROOT / "config" / "product.json").read_text(encoding="utf-8"))
    text = script_text()
    assert '!include "include\\product.nsh"' in text
    assert "Unicode true" in text
    assert "!include MUI2.nsh" in text
    assert '!define MUI_FONT "Tahoma"' in text
    assert "SetCompressor /SOLID lzma" in text
    assert "RequestExecutionLevel user" in text
    assert "RequestExecutionLevel highest" not in text
    assert 'VIProductVersion "${PRODUCT_VERSION_QUAD}"' in text
    for key in ("ProductName", "CompanyName", "FileDescription", "FileVersion", "ProductVersion", "LegalCopyright"):
        assert f'VIAddVersionKey "{key}"' in text
    assert product["output_filename"] not in text


def test_wizard_matches_reference_structure_and_atomic_component_model() -> None:
    text = script_text()
    assert "!insertmacro MUI_PAGE_WELCOME" in text
    assert "Page custom CompatibilityPageCreate CompatibilityPageLeave" in text
    assert "!insertmacro MUI_PAGE_INSTFILES" in text
    assert "!insertmacro MUI_PAGE_FINISH" in text
    assert "!insertmacro MUI_UNPAGE_CONFIRM" in text
    assert "!insertmacro MUI_UNPAGE_INSTFILES" in text
    assert "!insertmacro MUI_UNPAGE_FINISH" in text
    assert "i900,i660" in text
    assert "i848,i500" in text
    assert '${NSD_CreateBitmap} 352 92 480 270' in text
    product = json.loads((ROOT / "config" / "product.json").read_text(encoding="utf-8"))
    assert product["product_name"] == "Unofficial Medieval: Total War Collection Patch"
    assert product["setup_caption"] == "Unofficial Medieval: Total War Collection Patch Setup"
    assert product["output_filename"] == "Unofficial Medieval Total War Collection Patch.exe"
    assert product["version"] == "1.0.0"
    assert product["component_name"] == "Terrain Movement Fix"
    assert '${NSD_CreateCheckbox} 12 94 295 24 "${PRODUCT_COMPONENT_NAME}"' in text
    assert 'Section "${PRODUCT_COMPONENT_NAME}" MainSection' in text
    assert "SectionIn RO" in text
    assert "${NSD_Check} $CompatibilityCheck" in text
    assert "Var CompatibilityCheck" in text
    assert "Var TargetText" in text
    assert "Var BrowseButton" in text


def test_detection_uses_evidenced_gog_and_steam_paths_plus_manual_browse() -> None:
    text = script_text()
    assert '$EXEDIR\\Medieval_TW.exe' in text
    assert "SetRegView 32" in text
    assert 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1397939414_is1' in text
    assert 'ReadRegStr $0 HKLM' in text
    assert "nsDialogs::SelectFolderDialog" in text
    assert 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 345260' in text
    assert "libraryfolders.vdf" in text
    assert "Total War Medieval 1 Gold" in text
    assert "SHOGUN" not in text.upper()


def test_steam_detection_precedes_gog_across_registry_views() -> None:
    text = script_text()
    detect = text.split("\nFunction DetectGamePath\n", 1)[1].split("FunctionEnd", 1)[0]
    steam_hits = [
        match.start()
        for match in re.finditer(
            re.escape("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 345260"),
            detect,
        )
    ]
    gog_hits = [
        match.start()
        for match in re.finditer(
            re.escape("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1397939414_is1"),
            detect,
        )
    ]
    assert len(steam_hits) == 4
    assert len(gog_hits) == 4
    assert max(steam_hits) < min(gog_hits)


def test_payload_and_engine_are_embedded_without_forbidden_game_files() -> None:
    text = script_text()
    for name in ("D3D9.dll", "dgVoodoo_D3D9.dll", "ddraw.dll", "D3DImm.dll", "dgVoodoo.conf"):
        assert f'vendor\\runtime\\{name}' in text
    assert 'vendor\\runtime\\payload-manifest.json' in text
    assert 'src\\install-engine.ps1' in text
    for forbidden in ("Medieval.Cfg", "~tmp.vrp", "D3D8.dll", ".pdb"):
        assert f'File "{forbidden}"' not in text
    assert "Medieval_TW.exe" not in "\n".join(
        line for line in text.splitlines() if re.search(r"^\s*File(?:\s|$)", line)
    )


def test_engine_drives_inspect_install_and_restore_with_uninstaller() -> None:
    text = script_text()
    assert '-Operation "$R0"' in text
    assert 'StrCpy $R0 "Inspect"' in text
    assert 'StrCpy $R0 "Install"' in text
    assert 'StrCpy $R0 "Restore"' in text
    assert '-OutputMode Human' in text
    assert 'WriteUninstaller "$PLUGINSDIR\\Uninstall.exe"' in text
    assert 'CopyFiles /SILENT "$PLUGINSDIR\\Uninstall.exe" "$INSTDIR\\.unofficial-medieval-total-war-patch"' in text
    assert "Section Uninstall" in text
    assert 'WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Unofficial Medieval Total War Collection Patch"' in text
    assert 'ReadRegStr $INSTDIR HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Unofficial Medieval Total War Collection Patch"' in text
    assert 'DeleteRegKey HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Unofficial Medieval Total War Collection Patch"' in text
    assert 'WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Unofficial Medieval Total War Patch"' not in text
    assert text.count('SetOutPath "$PLUGINSDIR\\payload"') >= 4
    assert text.count('SetOutPath "$PLUGINSDIR"') >= 4


def test_finish_badges_have_hover_images_and_current_links() -> None:
    text = script_text()
    product = json.loads((ROOT / "config" / "product.json").read_text(encoding="utf-8"))
    for name in ("discord-badge.bmp", "discord-badge-hover.bmp", "kofi-badge.bmp", "kofi-badge-hover.bmp"):
        assert name in text
    assert "FinishBadgeHoverTimer" in text
    assert product["discord_url"] == "https://discord.gg/zKbDADqWRC"
    assert product["kofi_url"] == "https://ko-fi.com/louiewoolger"
    assert 'ExecShell "open" "${PRODUCT_DISCORD_URL}"' in text
    assert 'ExecShell "open" "${PRODUCT_KOFI_URL}"' in text


def test_collection_patch_wizard_uses_exact_shogun_sibling_wording() -> None:
    text = script_text()

    assert '!define MUI_ABORTWARNING_TEXT "Are you sure you want to quit the Unofficial Medieval: Total War Collection Patch Setup?"' in text
    assert '!define MUI_WELCOMEPAGE_TITLE "Install Unofficial Medieval Total War Collection Patch"' in text
    assert '!define MUI_WELCOMEPAGE_TEXT "This installer patches your existing Medieval: Total War Collection folder."' in text
    assert '!define MUI_FINISHPAGE_TEXT "Selected options were applied to your game. Have fun!"' in text
    assert '!insertmacro MUI_HEADER_TEXT "Select patches" "Recommended options are selected by default. Hover over an option for more information."' in text
    assert '${NSD_CreateLabel} 0 0 100% 18 "Game folder"' in text
    assert '"Safety"' not in text
    assert '"Terrain Movement Fix"' in text
    assert "Installs dgVoodoo2 to fix click-to-move and drag-formation issues on modern Windows systems." in text
    assert "Windows XP is not supported." in text
    assert "SetCtlColors $PreviewWarningText FF0000 F0F0F0" in text

    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    repository = "LouieWoolger/medieval-total-war-collection-unofficial-patch"
    assert readme.startswith("# Unofficial Medieval: Total War Collection Patch\n")
    assert f"https://img.shields.io/github/downloads/{repository}/total?style=for-the-badge" in readme
    assert f"https://img.shields.io/github/v/release/{repository}?style=for-the-badge" in readme
    assert "https://img.shields.io/discord/1505490825889579018?style=for-the-badge" in readme
    assert "https://img.shields.io/badge/Ko--fi-Support-FF5F5F?style=for-the-badge" in readme
    assert "An installer for Medieval: Total War Collection on GOG and Steam." in readme
    assert "The installer looks for `Medieval_TW.exe`" in readme
    assert "- Terrain Movement Fix - installs dgVoodoo2 to fix click-to-move and drag-formation issues on modern Windows systems." in readme
    assert "The Terrain Movement Fix is for modern Windows systems. Windows XP is not supported." in readme
    assert "Unofficial Medieval Total War Collection Patch.exe" in readme
    assert [line for line in readme.splitlines() if line.startswith("## ")] == [
        "## Included Fixes",
        "## Requirements",
        "## Usage",
        "## Backups",
        "## Building from Source",
    ]
    for bloated_section in (
        "## Supported game build",
        "## Repair",
        "## Verify",
        "## Troubleshooting",
        "## Credits and licensing",
        "## Version history",
    ):
        assert bloated_section not in readme


def test_installer_has_no_network_fetch_telemetry_or_developer_paths() -> None:
    combined = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in (SCRIPT, ROOT / "src" / "install-engine.ps1")
    )
    lowered = combined.lower()
    for forbidden in (
        "downloadfile",
        "inetc::",
        "nsisdl::",
        "invoke-webrequest",
        "start-bitstransfer",
        "telemetry",
        "analytics",
    ):
        assert forbidden not in lowered
    assert not re.search(r"(?i)\b[a-z]:[\\/]", combined)
