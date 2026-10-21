"""Pin rebuilt Credits-off proxies after the campaign tooltip locale changes.

The source build lives outside the checkout. Historical R289 receipt hashes
stay in medieval_state.h for local upgrades.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
NON_ZOOM = {
    "scroll-sprite-off": "scroll_sprite_off_sha256",
    "scroll-sprite-off-fast": "scroll_sprite_off_fast_sha256",
    "scroll-sprite-off-icon-off": "scroll_sprite_off_icon_off_sha256",
    "scroll-sprite-off-fast-icon-off": "scroll_sprite_off_fast_icon_off_sha256",
    "fast-only": "fast_only_sha256",
    "icon-only": "icon_only_sha256",
    "fast-icon": "fast_icon_sha256",
}
ZOOM = {
    "scroll-sprite-off-zoom", "scroll-sprite-off-fast-zoom",
    "scroll-sprite-off-icon-off-zoom", "scroll-sprite-off-fast-icon-off-zoom",
    "zoom-only", "fast-only-zoom", "icon-only-zoom", "fast-icon-zoom",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()
    zoom = json.loads((build / "zoom-build-manifest.json").read_text(encoding="utf-8-sig"))
    scaffold = json.loads((build / "scaffold" / "BUILD_MANIFEST.json").read_text(encoding="utf-8-sig"))
    zoom_records = {item["name"]: item for item in zoom["variants"]}
    if zoom["schema"] != "mtw-zoom-payload-build-v1" or set(zoom_records) != ZOOM:
        raise ValueError("Expected exactly the eight source-built zoom variants")
    for name, expected in zoom["source_inputs"].items():
        source = (ROOT / name).resolve()
        if not source.is_relative_to(ROOT) or digest(source.read_bytes()) != expected["sha256"]:
            raise ValueError(f"Zoom build source changed: {name}")
    if scaffold["status"] != "development_candidate" or any(
        value != 0 for key, value in scaffold.items() if key.endswith("_tests_exit")
    ) or scaffold["smoke_loader_exit"] != 0:
        raise ValueError("Native scaffold checks did not pass")
    pending: list[tuple[Path, bytes]] = []
    report = {"schema": "mtw-zoom-tooltip-runtime-refresh-v1", "build": str(build),
              "applied": args.apply, "variants": []}
    for name in (*NON_ZOOM, *sorted(ZOOM)):
        built = ((build / "payloads") if name in ZOOM else
                 (build / "scaffold" / "combined-r154")) / f"D3D9-{name}.dll"
        target = RUNTIME / f"D3D9-{name}.dll"
        manifest_path = RUNTIME / f"payload-manifest-{name}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        old_bytes, new_bytes = target.read_bytes(), built.read_bytes()
        record = manifest["files"]["D3D9.dll"]
        if (record["sha256"], record["length"]) != (digest(old_bytes), len(old_bytes)):
            raise ValueError(f"Packaged runtime differs from its manifest: {name}")
        expected = (zoom_records[name]["sha256"] if name in ZOOM else
                    scaffold[NON_ZOOM[name]])
        if digest(new_bytes) != expected or (name in ZOOM and
                                              len(new_bytes) != zoom_records[name]["length"]):
            raise ValueError(f"Source build differs from its manifest: {name}")
        changed = old_bytes != new_bytes
        if changed:
            record.update(sha256=digest(new_bytes), length=len(new_bytes))
            record["provenance"] = record["provenance"].replace(
                "R289", "R290-zoom-tooltip")
            manifest["runtime_identity"] = f"R290-zoom-tooltip-{name}"
            pending.extend((
                (target, new_bytes),
                (manifest_path, (json.dumps(manifest, indent=2) + "\n").encode("utf-8")),
            ))
        report["variants"].append({"name": name, "changed": changed,
                                   "old_sha256": digest(old_bytes),
                                   "new_sha256": digest(new_bytes)})
    if args.apply:
        for path, data in pending:
            path.write_bytes(data)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{'Applied' if args.apply else 'Checked'} {len(report['variants'])} Credits-off variants")


if __name__ == "__main__":
    main()
