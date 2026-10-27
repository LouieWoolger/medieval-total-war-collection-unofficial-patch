"""The one name and title choice includes the marriage letter correction."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_marriage_titles_share_the_name_and_title_choice() -> None:
    product = json.loads((ROOT / "config/product.json").read_text(encoding="utf-8"))
    installer = (ROOT / "installer.nsi").read_text(encoding="utf-8")
    support = (ROOT / "installer-support.nsh").read_text(encoding="utf-8")
    engine = (ROOT / "src/medieval_fix_patcher.c").read_text(encoding="utf-8")
    lifecycle = (ROOT / "src/lifecycle.h").read_text(encoding="utf-8")

    assert product["name_component_name"] == "Name and Title Fix"
    assert "marriage_title_component_name" not in product
    assert installer.count('"${PRODUCT_NAME_COMPONENT_NAME}"') >= 2
    assert "MarriageTitleCheck" not in installer
    assert "MarriageTitleSelected" not in installer
    assert "PRODUCT_MARRIAGE_TITLE_COMPONENT_NAME" not in installer
    assert "MARRIAGETITLEFIX" not in installer + support
    assert "marriage_title_fix=" not in support
    assert "marriage-title-fix" not in engine
    assert "MarriageTitleFix" not in engine
    assert 'L"marriage_title_fix"' not in engine
    assert "m.options.marriage_title_fix = m.options.name_fix;" in lifecycle
    assert 'mtw_boolean(d, receipt, "marriage_title_fix_enabled", m->options.marriage_title_fix, e)' in lifecycle


def test_name_preview_explains_the_included_letter_fix() -> None:
    installer = (ROOT / "installer.nsi").read_text(encoding="utf-8")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    assert 'File /oname=name-and-title.bmp' in installer
    assert '${NSD_OnClick} $NameCheck PreviewName' in installer
    assert '!insertmacro CHECK_PREVIEW_HOVER $NameCheck "name"' in installer
    assert '!insertmacro CHECK_PREVIEW_FOCUS $NameCheck "name"' in installer
    description = 'Corrects Sicilian and Welsh character names and titles, and faction titles in marriage letters.'
    assert description in installer
    assert description in readme
    assert "PreviewMarriageTitle" not in installer
    assert 'File /oname=marriage-letter-title.bmp' not in installer
