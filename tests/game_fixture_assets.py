"""Copy the supported game's campaign text into disposable installer fixtures."""
from __future__ import annotations

import hashlib
from pathlib import Path
import shutil

STOCK_ENGLISH_SHA256 = "A4ED1815E5BBE23DC0B4279AFB9398E1FABC6A9A1F2A014DB96F5F25E2B5B3DC"
STOCK_HEROES_SHA256 = "62B181D0804E48ECB527CAC1DC38D5DBE6CC79A22B9C576530F6F1C842D6E6DF"
STOCK_NAMES_SHA256 = "BDFB6F894C4B21C147A523A1FAD84B1A8F3CF8D809E98C69363266D4B17A7A76"


def copy_campaign_text(game_executable: Path, destination: Path) -> None:
    source = game_executable.parent / "Loc" / "Eng" / "CHANGES.TXT"
    data_hash = hashlib.sha256(source.read_bytes()).hexdigest().upper()
    assert data_hash == STOCK_ENGLISH_SHA256, f"Unsupported fixture localization: {source}"
    target = destination / "Loc" / "Eng" / "CHANGES.TXT"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def copy_character_text(game_executable: Path, destination: Path) -> None:
    for relative, expected in (
        (Path("campmap/names/DEFAULT_HEROES.TXT"), STOCK_HEROES_SHA256),
        (Path("Loc/Eng/NAMES.TXT"), STOCK_NAMES_SHA256),
    ):
        source = game_executable.parent / relative
        data_hash = hashlib.sha256(source.read_bytes()).hexdigest().upper()
        assert data_hash == expected, f"Unsupported fixture game data: {source}"
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
