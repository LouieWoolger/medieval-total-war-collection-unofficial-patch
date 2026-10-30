"""Pin source-built feature proxies while preserving released R293 identities.

Every input/old manifest is checked before writes. Terrain payloads and their
manifests are not changed. Re-running with the same source build is idempotent.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from runtime_platform import FEATURE_VARIANTS, audit_feature_proxy, file_record

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor/runtime"
HISTORY = ROOT / "include/runtime_proxy_r293_hashes.h"
ROW = re.compile(r'\{([01])u,\s*([01])u,\s*([01])u,\s*([01])u,\s*"([0-9A-F]{64})",\s*([0-9]+)u\},')


def history_header(rows: list[tuple]) -> bytes:
    lines = ["/* Released R293 feature identities retained by tools/refresh-feature-runtime.py. */",
             "#ifndef MTW_RUNTIME_PROXY_R293_HASHES_H", "#define MTW_RUNTIME_PROXY_R293_HASHES_H",
             "#include <stddef.h>", "#include <string.h>",
             "typedef struct MtwR293FeatureProxyVariant {",
             "    unsigned char fast, icon, zoom, credits;",
             "    const char *sha256;", "    size_t file_length;", "} MtwR293FeatureProxyVariant;",
             "static const MtwR293FeatureProxyVariant mtw_r293_feature_proxy_variants[] = {"]
    for fast, icon, zoom, credits, digest, length in rows:
        lines.append(f'    {{{fast}u, {icon}u, {zoom}u, {credits}u, "{digest}", {length}u}},')
    lines += ["};", "static inline const MtwR293FeatureProxyVariant *mtw_r293_feature_proxy_for_options(",
              "    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom, unsigned int credits) {",
              "    size_t i;", "    if (terrain) return NULL;",
              "    for (i = 0u; i < sizeof(mtw_r293_feature_proxy_variants) / sizeof(mtw_r293_feature_proxy_variants[0]); ++i) {",
              "        const MtwR293FeatureProxyVariant *v = &mtw_r293_feature_proxy_variants[i];",
              "        if (v->fast == fast && v->icon == icon && v->zoom == zoom && v->credits == credits) return v;",
              "    }", "    return NULL;", "}",
              "static inline int mtw_r293_feature_proxy_known_hash(const char *hash) {", "    size_t i;",
              "    if (!hash) return 0;",
              "    for (i = 0u; i < sizeof(mtw_r293_feature_proxy_variants) / sizeof(mtw_r293_feature_proxy_variants[0]); ++i)",
              "        if (!strcmp(hash, mtw_r293_feature_proxy_variants[i].sha256)) return 1;",
              "    return 0;", "}", "#endif", ""]
    return "\n".join(lines).encode("ascii")


def refresh(build: Path, report_path: Path, apply: bool) -> dict:
    manifest = json.loads((build / "feature-build-manifest.json").read_text(encoding="utf-8"))
    variants = {entry["name"]: entry for entry in manifest.get("variants", [])}
    if (manifest.get("schema") != "mtw-feature-runtime-build-v1" or manifest.get("status") != "pass" or
            set(variants) != set(FEATURE_VARIANTS) or len(manifest["variants"]) != 15):
        raise ValueError("Feature source build evidence is incomplete.")
    for relative, expected in manifest["source_inputs"].items():
        source = (ROOT / relative).resolve()
        if not source.is_relative_to(ROOT) or file_record(source) != expected:
            raise ValueError(f"Feature source changed after the build: {relative}")
    previous = []
    pending = []
    output_records = {}
    if HISTORY.exists():
        history = [tuple(int(value) for value in row[:4]) + (row[4], int(row[5]))
                   for row in ROW.findall(HISTORY.read_text(encoding="ascii"))]
        if len(history) != 15 or {row[:4] for row in history} != set(FEATURE_VARIANTS.values()):
            raise ValueError("Released R293 feature identity history is incomplete.")
    else:
        history = []
    report = {"schema": "mtw-feature-runtime-refresh-v1", "applied": apply, "variants": []}
    for name, flags in FEATURE_VARIANTS.items():
        entry = variants[name]
        expected_filename = f"D3D9-{name}.dll"
        if (entry["filename"] != expected_filename or entry["terrain"] or
                tuple(int(entry[key]) for key in ("fast", "icon", "zoom", "credits")) != flags or
                entry.get("factory_smoke_exit") != 0):
            raise ValueError(f"Feature build option flags differ: {name}")
        source = build / "payloads" / expected_filename
        record = file_record(source)
        if any(entry[key] != value for key, value in record.items()):
            raise ValueError(f"Feature build bytes changed: {name}")
        audit_feature_proxy(source.read_bytes())
        target = RUNTIME / expected_filename
        manifest_path = RUNTIME / f"payload-manifest-{name}.json"
        old_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        old_record = file_record(target)
        if set(old_manifest["files"]) != {"D3D9.dll"} or any(
                old_manifest["files"]["D3D9.dll"][key] != value for key, value in old_record.items()):
            raise ValueError(f"Existing feature manifest differs from DLL: {name}")
        row = (*flags, old_record["sha256"], old_record["length"])
        if history and old_record != record and row not in history:
            raise ValueError(f"Existing feature is neither released R293 nor the current source build: {name}")
        previous.append(row)
        old_manifest["runtime_identity"] = f"R294-xp-{name}"
        old_manifest["files"]["D3D9.dll"] = {**record,
            "provenance": "Source-built PE32 feature proxy using the bounded XP SP3 import surface, with exact supported game inputs and the selected hooks; forwards to Windows D3D9. XP execution is not established by the import audit."}
        pending.extend(((target, source.read_bytes()),
                        (manifest_path, (json.dumps(old_manifest, indent=2) + "\n").encode("utf-8"))))
        output_records[(0, *flags)] = record
        report["variants"].append({"name": name, "previous": old_record, "current": record})
    history_bytes = HISTORY.read_bytes() if history else history_header(previous)
    pending.append((HISTORY, history_bytes))
    for filename, credits in (("runtime_proxy_hashes.h", 0), ("credits_payload_hashes.h", 1)):
        path = ROOT / "include" / filename
        header = path.read_text(encoding="ascii")

        def substitute(match):
            terrain, fast, icon, zoom = map(int, match.groups()[:4])
            if terrain:
                return match.group(0)
            record = output_records[(terrain, fast, icon, zoom, credits)]
            return f'{{{terrain}u, {fast}u, {icon}u, {zoom}u, "{record["sha256"]}", {record["length"]}u}},'

        header = ROW.sub(substitute, header)
        pending.append((path, header.encode("ascii")))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if apply:
        for path, data in pending:
            path.write_bytes(data)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    result = refresh(args.build.resolve(), args.report.resolve(), args.apply)
    print(f"{'Pinned' if args.apply else 'Checked'} {len(result['variants'])} feature-only proxies; Terrain payloads unchanged.")


if __name__ == "__main__":
    main()
