"""Pin source-built dust alpha repair in the active terrain payloads.

Only variants containing the terrain and dust graphics bridge change. Historical wrappers and
unchanged feature-only variants retain their exact released bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
TERRAIN = (
    "scroll-sprite-off",
    "scroll-sprite-off-fast",
    "scroll-sprite-off-icon-off",
    "scroll-sprite-off-fast-icon-off",
    "scroll-sprite-off-zoom",
    "scroll-sprite-off-fast-zoom",
    "scroll-sprite-off-icon-off-zoom",
    "scroll-sprite-off-fast-icon-off-zoom",
)
FEATURE_ONLY = (
    "fast-only", "icon-only", "fast-icon", "zoom-only",
    "fast-only-zoom", "icon-only-zoom", "fast-icon-zoom",
)
TERRAIN_HASH_MACROS = {
    "scroll-sprite-off": "MEDIEVAL_R292_TERRAIN_ICON_HASH",
    "scroll-sprite-off-fast": "MEDIEVAL_R292_TERRAIN_FAST_ICON_HASH",
    "scroll-sprite-off-icon-off": "MEDIEVAL_R292_TERRAIN_NO_ICON_HASH",
    "scroll-sprite-off-fast-icon-off": "MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_HASH",
    "scroll-sprite-off-zoom": "MEDIEVAL_R292_TERRAIN_ICON_ZOOM_HASH",
    "scroll-sprite-off-fast-zoom": "MEDIEVAL_R292_TERRAIN_FAST_ICON_ZOOM_HASH",
    "scroll-sprite-off-icon-off-zoom": "MEDIEVAL_R292_TERRAIN_NO_ICON_ZOOM_HASH",
    "scroll-sprite-off-fast-icon-off-zoom":
        "MEDIEVAL_R292_TERRAIN_FAST_NO_ICON_ZOOM_HASH",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def source_build_file(build: Path, name: str, credits: bool) -> Path:
    if credits:
        return build / "payloads" / f"D3D9-{name}-credits.dll"
    if name.endswith("-zoom") or name == "zoom-only":
        return build / "payloads" / f"D3D9-{name}.dll"
    return build / "scaffold" / "combined-r154" / f"D3D9-{name}.dll"


def validate_build(build: Path, credits: bool) -> dict[str, tuple[str, int]]:
    scaffold = read_json(build / "scaffold" / "BUILD_MANIFEST.json")
    zoom = read_json(build / ("zoom" if credits else "") / "zoom-build-manifest.json")
    if scaffold["status"] != "development_candidate" or any(
        value != 0 for key, value in scaffold.items() if key.endswith("_tests_exit")
    ) or scaffold["smoke_loader_exit"] != 0:
        raise ValueError(f"Scaffold verification failed: {build}")
    for name, record in zoom["source_inputs"].items():
        source = (ROOT / name).resolve()
        if not source.is_relative_to(ROOT) or digest(source.read_bytes()) != record["sha256"]:
            raise ValueError(f"Build source changed: {name}")
    for field, name in (
        ("frontend_fix_source_sha256", "vendor/r185/source/frontend_fix.c"),
        ("primary_origin_guard_core_source_sha256", "vendor/r185/source/primary_origin_guard_core.c"),
        ("dust_bridge_source_sha256", "vendor/dust-fidelity/dust_565_bridge.cpp"),
        ("dust_atlas_alpha_source_sha256", "vendor/dust-fidelity/dust_atlas_alpha.h"),
        ("dust_alpha_modulate_shader_source_sha256", "vendor/dust-fidelity/dust_alpha_modulate_ps.h"),
        ("executable_variants_header_sha256", "include/mtw_exe_variants.h"),
    ):
        if digest((ROOT / name).read_bytes()) != scaffold[field]:
            raise ValueError(f"Scaffold source changed: {name}")
    credits_records: dict[str, dict] = {}
    if credits:
        manifest = read_json(build / "credits-build-manifest.json")
        if manifest["status"] != "development_candidate":
            raise ValueError("Credits build is not a development candidate")
        credits_records = {row["name"]: row for row in manifest["variants"]}
        if set(credits_records) != set(TERRAIN) | set(FEATURE_ONLY) | {"credits-only"}:
            raise ValueError("Unexpected Credits payload set")
    zoom_records = {row["name"]: row for row in zoom["variants"]}
    if set(zoom_records) != {name for name in TERRAIN + FEATURE_ONLY if name.endswith("-zoom") or name == "zoom-only"}:
        raise ValueError("Unexpected Zoom payload set")
    records = {}
    for name in TERRAIN + FEATURE_ONLY:
        path = source_build_file(build, name, credits)
        data = path.read_bytes()
        expected = credits_records[name] if credits else (
            zoom_records[name] if name.endswith("-zoom") or name == "zoom-only" else None
        )
        if expected and (digest(data), len(data)) != (expected["sha256"], expected["length"]):
            raise ValueError(f"Built payload differs from build manifest: {name}")
        records[name] = (digest(data), len(data))
    return records


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--off-build", type=Path, required=True)
    parser.add_argument("--on-build", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    builds = {False: args.off_build.resolve(), True: args.on_build.resolve()}
    build_records = {credits: validate_build(build, credits)
                     for credits, build in builds.items()}
    pending: list[tuple[Path, bytes]] = []
    report = {"schema": "mtw-dust-alpha-runtime-refresh-v1",
              "applied": args.apply, "variants": []}
    replacements: dict[str, tuple[str, int]] = {}
    terrain_replacements: dict[str, str] = {}
    for credits in (False, True):
        for name in TERRAIN + FEATURE_ONLY:
            suffix = "-credits" if credits else ""
            target = RUNTIME / f"D3D9-{name}{suffix}.dll"
            manifest_path = RUNTIME / f"payload-manifest-{name}{suffix}.json"
            old_data = target.read_bytes()
            new_data = source_build_file(builds[credits], name, credits).read_bytes()
            old_manifest = read_json(manifest_path)
            record = old_manifest["files"]["D3D9.dll"]
            if (digest(old_data), len(old_data)) != (record["sha256"], record["length"]):
                raise ValueError(f"Installed payload differs from its manifest: {name}{suffix}")
            if (digest(new_data), len(new_data)) != build_records[credits][name]:
                raise ValueError(f"Candidate changed after build: {name}{suffix}")
            changed = old_data != new_data
            if changed != (name in TERRAIN):
                raise ValueError(f"Unexpected changed or unchanged variant: {name}{suffix}")
            if changed:
                record.update(sha256=digest(new_data), length=len(new_data))
                record["provenance"] = (
                    "Source-built PE32 D3D9 proxy with exact supported game identity; "
                    "dust transparency state, atlas mask and sampler are repaired only for selected draws "
                    "while FastVideoMemoryAccess remains enabled."
                )
                old_manifest["runtime_identity"] = (
                    f"R292-credits-dust-alpha-{name}" if credits else
                    f"R292-dust-alpha-{name}"
                )
                pending.extend(((target, new_data),
                                (manifest_path, (json.dumps(old_manifest, indent=2) + "\n").encode("utf-8"))))
                if credits:
                    replacements[digest(old_data)] = (digest(new_data), len(new_data))
                else:
                    terrain_replacements[name] = digest(new_data)
            report["variants"].append({"name": name, "credits": credits,
                                       "changed": changed,
                                       "old_sha256": digest(old_data),
                                       "new_sha256": digest(new_data)})

    base = RUNTIME / "D3D9.dll"
    base_manifest = RUNTIME / "payload-manifest.json"
    original_base = RUNTIME / "D3D9-scroll-sprite-off.dll"
    if base.read_bytes() != original_base.read_bytes() or base_manifest.read_bytes() != (
        RUNTIME / "payload-manifest-scroll-sprite-off.json").read_bytes():
        raise ValueError("Default payload is not the exact base alias")
    pending_dict = dict(pending)
    pending.extend(((base, pending_dict[original_base]),
                    (base_manifest, pending_dict[RUNTIME / "payload-manifest-scroll-sprite-off.json"])))

    header_path = ROOT / "include" / "credits_payload_hashes.h"
    header = header_path.read_text(encoding="utf-8")
    frozen_path = ROOT / "include" / "credits_payload_r291_hashes.h"
    if not frozen_path.exists():
        table = header[header.index("static const MtwCreditsProxyVariant mtw_credits_proxy_variants[]"):]
        frozen = ("/* Frozen R291 Credits identities for dust alpha upgrades. */\n"
                  "#ifndef MTW_CREDITS_PAYLOAD_R291_HASHES_H\n"
                  "#define MTW_CREDITS_PAYLOAD_R291_HASHES_H\n"
                  "#include \"credits_payload_hashes.h\"\n" +
                  table.replace("mtw_credits_proxy", "mtw_r291_credits_proxy"))
        pending.append((frozen_path, frozen.encode("utf-8")))
    for old, (new, length) in replacements.items():
        pattern = rf'"{old}", \d+u'
        if len(re.findall(pattern, header)) != 1:
            raise ValueError(f"Credits identity not unique: {old}")
        header = re.sub(pattern, f'"{new}", {length}u', header)
    pending.append((header_path, header.encode("utf-8")))
    state_path = ROOT / "src" / "medieval_state.h"
    state = state_path.read_text(encoding="utf-8")
    if "MEDIEVAL_R292_TERRAIN_ICON_HASH" in state:
        for name, value in terrain_replacements.items():
            macro = TERRAIN_HASH_MACROS[name]
            pattern = rf'(#define {macro} ")[A-F0-9]{{64}}(")'
            if len(re.findall(pattern, state)) != 1:
                raise ValueError(f"R292 identity not unique: {macro}")
            state = re.sub(pattern, lambda match: match[1] + value + match[2], state)
        pending.append((state_path, state.encode("utf-8")))
    report["files_to_update"] = [str(path.relative_to(ROOT)) for path, _ in pending]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.apply:
        for path, data in pending:
            path.write_bytes(data)
    print(f"{'Applied' if args.apply else 'Checked'} {len(TERRAIN) * 2} terrain variants; "
          "feature-only variants unchanged")


if __name__ == "__main__":
    main()
