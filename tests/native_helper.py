"""The real source-built native helper and legacy receipt checksum codec."""
import hashlib
import json
import os
from pathlib import Path


def helper_path() -> Path:
    value = os.environ.get("MTW_TEST_NATIVE_HELPER")
    if not value:
        raise RuntimeError("Build the native helper and set MTW_TEST_NATIVE_HELPER before running lifecycle tests")
    path = Path(value)
    if not path.is_file():
        raise RuntimeError(f"The source-built native helper is missing: {path}")
    return path


def canonical_json(value) -> str:
    # Windows PowerShell 5.1's ordered ConvertTo-Json -Compress representation.
    # Golden native/PowerShell vectors test this independently of the lifecycle.
    text = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    for char in ("'", "<", ">", "&", "\u0085", "\u2028", "\u2029"):
        text = text.replace(char, f"\\u{ord(char):04x}")
    return text


def seal(value: dict) -> None:
    body = {key: item for key, item in value.items() if key != "integrity_sha256"}
    value["integrity_sha256"] = hashlib.sha256(canonical_json(body).encode("utf-8")).hexdigest().upper()
