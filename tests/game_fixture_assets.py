"""Copy the supported game's campaign text into disposable installer fixtures."""
from __future__ import annotations

import hashlib
from pathlib import Path
import shutil

STOCK_ENGLISH_SHA256 = "A4ED1815E5BBE23DC0B4279AFB9398E1FABC6A9A1F2A014DB96F5F25E2B5B3DC"


def copy_campaign_text(game_executable: Path, destination: Path) -> None:
    source = game_executable.parent / "Loc" / "Eng" / "CHANGES.TXT"
    data_hash = hashlib.sha256(source.read_bytes()).hexdigest().upper()
    assert data_hash == STOCK_ENGLISH_SHA256, f"Unsupported fixture localization: {source}"
    target = destination / "Loc" / "Eng" / "CHANGES.TXT"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
