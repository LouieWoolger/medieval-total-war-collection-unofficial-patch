"""Fail packaging if the native helper's Credits proxy identities drift."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
ROW = re.compile(
    r'\{([01])u,\s*([01])u,\s*([01])u,\s*([01])u,\s*"([0-9A-F]{64})",\s*([0-9]+)u\},'
)


def verify(header: Path, runtime: Path) -> None:
    rows = [((int(t), int(f), int(i), int(z)), h, int(length))
            for t, f, i, z, h, length in ROW.findall(header.read_text(encoding="utf-8"))]
    if len(rows) != 16 or len({flags for flags, _, _ in rows}) != 16:
        raise ValueError("Credits native identity table must contain 16 distinct flag combinations")
    manifests = sorted(runtime.glob("payload-manifest-*-credits.json"))
    manifests.append(runtime / "payload-manifest-credits-only.json")
    if len(manifests) != 16 or any(not path.is_file() for path in manifests):
        raise ValueError("Credits package must contain 16 payload manifests")
    expected = {}
    for path in manifests:
        name = path.name.removeprefix("payload-manifest-").removesuffix(".json")
        stem = "credits-only" if name == "credits-only" else name.removesuffix("-credits")
        terrain = stem.startswith("scroll-sprite-off")
        icon = "icon-off" not in stem if terrain else "icon" in stem
        flags = (int(terrain), int("fast" in stem), int(icon), int("zoom" in stem))
        binary = runtime / f"D3D9-{name}.dll"
        manifest = json.loads(path.read_text(encoding="utf-8"))
        digest = hashlib.sha256(binary.read_bytes()).hexdigest().upper()
        length = binary.stat().st_size
        descriptor = manifest["files"]["D3D9.dll"]
        if descriptor["sha256"] != digest or descriptor["length"] != length:
            raise ValueError(f"Credits manifest does not match packaged DLL: {name}")
        if flags in expected:
            raise ValueError(f"Credits manifests duplicate feature flags: {flags}")
        expected[flags] = (digest, length)
    for flags, digest, length in rows:
        if expected.get(flags) != (digest, length):
            raise ValueError(f"Credits native identity table differs from packaged DLL: {flags}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", type=Path, default=ROOT / "include/credits_payload_hashes.h")
    parser.add_argument("--runtime", type=Path, default=ROOT / "vendor/runtime")
    args = parser.parse_args()
    verify(args.header, args.runtime)
    print("Credits native identity table matches all 16 packaged DLLs and manifests.")


if __name__ == "__main__":
    main()
