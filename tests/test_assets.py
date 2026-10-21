from __future__ import annotations

import hashlib
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"

EXPECTED_ASSETS = {
    "compatibility.bmp": (
        388854,
        "E735AB2EDBE15A88D3E2A7A1EED19188EB97B2A6A1640C2ED61F5129A87F896E",
    ),
    "campaign-scrolling.bmp": (
        388854,
        "504DFA880F0C98A2E4BD7168C02A9237F89A70FCAAE08D70D364A42E214B5B6E",
    ),
    "character-parchment.bmp": (
        388854,
        "B3E4A4E2EC0F1CA559D29AE79CD2D798291546BA5AE423E39D5DB800767D5149",
    ),
    "name-and-title.bmp": (
        388854,
        "4BD8309BE32D7CE3098A03A84BC0F9513B0A9BA7F03B4FBCE97D09BE6296F268",
    ),
    "sprite-clipping.bmp": (
        388854,
        "945FF6E4CAF7974B4CF67875930152470ED1E93B8EAF47C927D68FC30626FA81",
    ),
    "campaign-tutorial-resolution.bmp": (
        388854,
        "F3B497185B2757B0BD2432DB408E98FA04C60AAB7264F0E7328D7DB69B740EDB",
    ),
    "battle-unit-icons.bmp": (
        388854,
        "EAB150CE9E18C35F7CF718DBEF7CD3BF9B05C9027180DCE0298E3839D0A89E98",
    ),
    "fast-computer-moves.bmp": (
        388854,
        "223B10F33D0033FC91E395DD1D0E06397942F1C28A7CE32424EF68CAFDB679DE",
    ),
    "unit-balance.bmp": (
        388854,
        "2533E2910CED21C1C1F26E6A228FCE9317EA45F9A02D96FCC7FDE4B87F281647",
    ),
    "campaign-zoom-option.bmp": (
        388854,
        "8D17FB8F0998961AC086E48398B8A5098EC8EF9E4FEE2EB65836F54298A941E9",
    ),
    "credits-illustration.bmp": (
        388854,
        "BBF16BFB77284E8CA03F4948C66681991AAD498961356BEBD13B743B4B959E7C",
    ),
    "keybinding-customisation.bmp": (
        388854,
        "48C8B3B9429C4C7E7E94C019E45ED2162C9246C174C55CF7D1AD1A3BDCACDFB1",
    ),
    "discord-badge-hover.bmp": (
        11702,
        "73BCF5C4A9D130C40EE1AD1D0DE818137B27C18738D64DF46F65C33E318C252D",
    ),
    "discord-badge.bmp": (
        11702,
        "C51BF11990B7D5587B1A280140F7F285C5E6968DCF6286D6B6836D6171342B3F",
    ),
    "kofi-badge-hover.bmp": (
        11702,
        "D542A19AEA0CF3803F902869069C7D301583BB6A94DE0BE34F744E47B82AB66E",
    ),
    "kofi-badge.bmp": (
        11702,
        "79D236EA901262ECF7B46E96DC33304AE26E85A9691F4BED85AA79FD23E698D1",
    ),
    "medieval.ico": (
        2238,
        "9F8C14E996EFA4C3A002BB7299C117E27E65ED1205B245FC2D93C9531F5D94AC",
    ),
    "welcome-finish.bmp": (
        154542,
        "C3EC1D0E4D078113FFF722F2EA14DEBFBF34342A00B12C2A08B1E7445FC6A7DE",
    ),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def test_only_finished_installer_assets_are_published() -> None:
    actual = {path.name for path in ASSETS.iterdir() if path.is_file()}
    assert actual == set(EXPECTED_ASSETS)
    assert not (ASSETS / "source").exists()
    assert not (ROOT / "tools" / "build-assets.ps1").exists()
    assert not (ROOT / "tools" / "build-assets.py").exists()
    assert "build-assets" not in (ROOT / "build.ps1").read_text(encoding="utf-8")


def test_finished_assets_are_exact() -> None:
    for name, (length, digest) in EXPECTED_ASSETS.items():
        path = ASSETS / name
        assert path.stat().st_size == length, name
        assert sha256(path) == digest, name


def test_nsis_bitmaps_have_exact_dimensions_and_format() -> None:
    expected = {
        "welcome-finish.bmp": (164, 314),
        "compatibility.bmp": (480, 270),
        "campaign-scrolling.bmp": (480, 270),
        "character-parchment.bmp": (480, 270),
        "name-and-title.bmp": (480, 270),
        "sprite-clipping.bmp": (480, 270),
        "campaign-tutorial-resolution.bmp": (480, 270),
        "battle-unit-icons.bmp": (480, 270),
        "fast-computer-moves.bmp": (480, 270),
        "unit-balance.bmp": (480, 270),
        "campaign-zoom-option.bmp": (480, 270),
        "credits-illustration.bmp": (480, 270),
        "keybinding-customisation.bmp": (480, 270),
        "discord-badge.bmp": (138, 28),
        "discord-badge-hover.bmp": (138, 28),
        "kofi-badge.bmp": (138, 28),
        "kofi-badge-hover.bmp": (138, 28),
    }
    for name, size in expected.items():
        with Image.open(ASSETS / name) as image:
            assert image.format == "BMP", name
            assert image.size == size, name
            assert image.mode == "RGB", name


def test_installer_icon_is_exact() -> None:
    path = ASSETS / "medieval.ico"
    with Image.open(path) as image:
        assert (32, 32) in set(image.info.get("sizes", []))
        assert image.format == "ICO"
