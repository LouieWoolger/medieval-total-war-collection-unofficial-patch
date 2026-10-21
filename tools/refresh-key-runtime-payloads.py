"""Stage rebuilt D3D9 payloads after extending the exact game EXE table.

Run only against an isolated source checkout and a successful task-owned output
    from build-credits-payloads.ps1. Historical runtime DLLs remain in Git history;
their hashes stay pinned in medieval_state.h for receipt upgrades.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
ACTIVE = (
    "scroll-sprite-off", "scroll-sprite-off-fast",
    "scroll-sprite-off-icon-off", "scroll-sprite-off-fast-icon-off",
    "fast-only", "icon-only", "fast-icon",
    "scroll-sprite-off-zoom", "scroll-sprite-off-fast-zoom",
    "scroll-sprite-off-icon-off-zoom", "scroll-sprite-off-fast-icon-off-zoom",
    "zoom-only", "fast-only-zoom", "icon-only-zoom", "fast-icon-zoom",
)
ZOOM = {name for name in ACTIVE if name.endswith("-zoom") or name == "zoom-only"}
SCAFFOLD_FIELDS = {
    "scroll-sprite-off": "scroll_sprite_off_sha256",
    "scroll-sprite-off-fast": "scroll_sprite_off_fast_sha256",
    "scroll-sprite-off-icon-off": "scroll_sprite_off_icon_off_sha256",
    "scroll-sprite-off-fast-icon-off": "scroll_sprite_off_fast_icon_off_sha256",
    "fast-only": "fast_only_sha256",
    "icon-only": "icon_only_sha256",
    "fast-icon": "fast_icon_sha256",
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
    zoom_root = build / "zoom" if (build / "zoom" / "zoom-build-manifest.json").exists() else build
    zoom = json.loads((zoom_root / "zoom-build-manifest.json").read_text(encoding="utf-8-sig"))
    scaffold = json.loads((build / "scaffold" / "BUILD_MANIFEST.json").read_text(encoding="utf-8-sig"))
    table_hash = digest((ROOT / "include" / "mtw_exe_variants.h").read_bytes())
    if zoom["schema"] != "mtw-zoom-payload-build-v1" or scaffold["status"] != "development_candidate":
        raise ValueError("The requested source build is not the expected development candidate")
    if zoom["source_inputs"]["include/mtw_exe_variants.h"]["sha256"] != table_hash:
        raise ValueError("The rebuilt payload uses another executable identity table")
    zoom_records = {row["name"]: row for row in zoom["variants"]}
    if set(zoom_records) != ZOOM:
        raise ValueError("The zoom build did not include exactly the eight active variants")
    if any(value != 0 for key, value in scaffold.items() if key.endswith("_tests_exit")) or scaffold["smoke_loader_exit"] != 0:
        raise ValueError("The native scaffold component gates did not pass")

    pending: list[tuple[Path, bytes]] = []
    report = {"schema": "mtw-key-runtime-refresh-v1", "identity_table_sha256": table_hash,
              "build": str(build), "applied": args.apply, "variants": []}
    for name in ACTIVE:
        zoomed = name in ZOOM
        built = (zoom_root / "payloads" if zoomed else build / "scaffold" / "combined-r154") / f"D3D9-{name}.dll"
        target = RUNTIME / f"D3D9-{name}.dll"
        manifest_path = RUNTIME / f"payload-manifest-{name}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        old_bytes = target.read_bytes()
        new_bytes = built.read_bytes()
        old_record = manifest["files"]["D3D9.dll"]
        if old_record["length"] != len(old_bytes) or old_record["sha256"] != digest(old_bytes):
            raise ValueError(f"Existing payload manifest differs from {target}")
        if zoomed:
            record = zoom_records[name]
            if record["length"] != len(new_bytes) or record["sha256"] != digest(new_bytes):
                raise ValueError(f"Zoom build manifest differs from {built}")
        elif scaffold[SCAFFOLD_FIELDS[name]] != digest(new_bytes):
            raise ValueError(f"Native scaffold manifest differs from {built}")
        if manifest["target_executable"]["sha256"] != "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5":
            raise ValueError(f"Unexpected game identity in {manifest_path}")
        old_identity = manifest["runtime_identity"]
        already_current = old_identity == f"R288-key-customisation-{name}"
        if not already_current and not old_identity.startswith(("R202-", "R203-", "R204-keytable-")):
            raise ValueError(f"Unexpected runtime identity: {old_identity}")
        old_record["length"] = len(new_bytes)
        old_record["sha256"] = digest(new_bytes)
        old_record["provenance"] = old_record["provenance"].replace(
            "R202", "R288-key-customisation").replace("R203", "R288-key-customisation").replace(
            "R204-keytable", "R288-key-customisation")
        manifest["runtime_identity"] = f"R288-key-customisation-{name}"
        pending.append((target, new_bytes))
        pending.append((manifest_path, (json.dumps(manifest, indent=2) + "\n").encode("utf-8")))
        report["variants"].append({"name": name, "old_sha256": digest(old_bytes),
                                   "new_sha256": digest(new_bytes), "old_length": len(old_bytes),
                                   "new_length": len(new_bytes), "manifest": manifest_path.name,
                                   "already_current": already_current})
    base = RUNTIME / "D3D9.dll"
    base_manifest = RUNTIME / "payload-manifest.json"
    previous = base.read_bytes()
    replacement = next(data for path, data in pending if path.name == "D3D9-scroll-sprite-off.dll")
    manifest = json.loads(base_manifest.read_text(encoding="utf-8"))
    if (manifest["files"]["D3D9.dll"]["sha256"] != digest(previous) or
            manifest["files"]["D3D9.dll"]["length"] != len(previous)):
        raise ValueError("Base payload manifest differs from its current DLL")
    if manifest["runtime_identity"] != "R288-key-customisation-scroll-sprite-off" and not manifest["runtime_identity"].startswith(("R202-", "R203-", "R204-keytable-")):
        raise ValueError("Unexpected base runtime identity")
    manifest["files"]["D3D9.dll"].update(
        sha256=digest(replacement), length=len(replacement),
        provenance=manifest["files"]["D3D9.dll"]["provenance"].replace(
            "R202", "R288-key-customisation").replace("R203", "R288-key-customisation").replace(
            "R204-keytable", "R288-key-customisation"))
    manifest["runtime_identity"] = "R288-key-customisation-scroll-sprite-off"
    pending.append((base, replacement))
    pending.append((base_manifest, (json.dumps(manifest, indent=2) + "\n").encode("utf-8")))
    if args.apply:
        for path, data in pending:
            path.write_bytes(data)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{'Applied' if args.apply else 'Checked'} {len(ACTIVE)} active DLL variants and the base alias")


if __name__ == "__main__":
    main()
