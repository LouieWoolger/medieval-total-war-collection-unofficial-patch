"""Check a real-game dust bridge trace for both diffuse phases.

Capture with a tracer that does not intercept the D3D9 bridge entry. Frida
onLeave instrumentation at that entry replaces its return address, which is
part of the production gray-phase selector, and creates a false bypass.
Pass the capture directory and installed EXE/proxy/backend hashes. This gate
reads observed selections and D3D11 targets; it does not infer correctness
from a screenshot or arithmetic model tests.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re


SHA256 = re.compile(r"[0-9A-F]{64}\Z")


def exact_hash(value: str) -> str:
    result = value.upper()
    if not SHA256.fullmatch(result):
        raise argparse.ArgumentTypeError("expected a 64-character SHA-256")
    return result


def inspect(capture: Path, hashes: dict[str, str], min_phase: int,
            min_scratch: int) -> dict:
    manifest = json.loads((capture / "capture.json").read_text(encoding="utf-8"))
    observed_hashes = manifest.get("inputs_sha256", {})
    invalid: list[str] = []
    failures: list[str] = []
    if manifest.get("schema") not in (
            "mtw-dust-gray-candidate-backend-trace-v2",
            "mtw-dust-gray-r188-backend-trace-v2"):
        invalid.append("unknown trace capture schema")
    if manifest.get("selector_observation") != "backend_only_no_selector_hook":
        invalid.append("selector trace is not proven return-address safe")
    if manifest.get("result") != "captured" or manifest.get("errors"):
        invalid.append("capture did not finish cleanly")
    for name, expected in hashes.items():
        if observed_hashes.get(name) != expected:
            invalid.append(f"{name} identity mismatch")

    kinds: Counter[str] = Counter()
    last_family: dict | None = None
    backend_hook: dict | None = None
    context_hooks: list[dict] = []
    outside_events = 0
    bad_begins = 0
    with (capture / "events.jsonl").open(encoding="utf-8") as events:
        for number, line in enumerate(events, start=1):
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid trace JSON at line {number}") from error
            kind = event.get("kind")
            kinds[kind] += 1
            if kind == "family_summary":
                last_family = event
            elif kind == "backend_d3d9_hook":
                backend_hook = event
            elif kind == "context_hooked":
                context_hooks.append(event)
            elif kind == "dust_gpu_outside_scratch" and event.get(
                "same_dust_resource"
            ) is True:
                outside_events += 1
            elif kind == "begin_result" and event.get("result") != 0:
                bad_begins += 1

    if last_family is None:
        invalid.append("no dust family summary")
        last_family = {}
    if kinds["hook_bytes"] or kinds["selector_entry"]:
        invalid.append("proxy selector instrumentation can change the caller")
    if (kinds["backend_d3d9_hook"] != 1 or kinds["ready"] != 1 or
            not kinds["context_hooked"]):
        invalid.append("required backend D3D9/D3D11 trace hooks were not confirmed")
    if not backend_hook or backend_hook.get("selector_observation") != (
            "backend_only_no_selector_hook") or not str(
                backend_hook.get("backend", "")).lower().endswith(
                    "dgvoodoo_d3d9.dll"):
        invalid.append("D3D9 hook was not confirmed on the original backend")
    if any(event.get("hook_site") != "backend_original_draw_indexed"
           for event in context_hooks):
        invalid.append("D3D11 target hook was not confirmed after the bridge")
    if kinds["scope_error"]:
        invalid.append("trace hook reported a scope error")
    family_coverage = last_family.get("family_total", 0)
    candidate_events = kinds["d3d9_dust_color_candidate"]
    if candidate_events and family_coverage < 0.95 * candidate_events:
        invalid.append("last family summary misses more than 5% of candidate calls")
    selected = last_family.get("phase_selected", {})
    unselected = last_family.get("phase_unselected", {})
    if sum(selected.values()) + sum(unselected.values()) != family_coverage:
        invalid.append("dust-family phase accounting is incomplete")
    gray_selected = selected.get("gray", 0)
    brown_selected = selected.get("brown", 0)
    gray_unselected = unselected.get("gray", 0)
    scratch = last_family.get("scratch_dust", 0)
    gpu_dust = last_family.get("gpu_dust", 0)
    other_dust = last_family.get("other_dust", 0)
    outside = last_family.get("outside_same_resource", 0)
    if not invalid:
        if gray_selected < min_phase:
            failures.append(f"gray exposure/selection {gray_selected} < {min_phase}")
        if brown_selected < min_phase:
            failures.append(f"brown exposure/selection {brown_selected} < {min_phase}")
        if gray_unselected:
            failures.append(f"{gray_unselected} gray draws bypassed the bridge")
        if last_family.get("family_unselected", 0):
            failures.append("dust-family draw bypassed the bridge")
        if outside or outside_events:
            failures.append(
                f"dust-resource D3D11 draws outside RGB565: "
                f"summary={outside}, events={outside_events}"
            )
        if scratch < min_scratch:
            failures.append(f"RGB565 dust GPU exposure {scratch} < {min_scratch}")
        if gpu_dust != scratch or other_dust:
            failures.append(
                f"dust-shader GPU draws missed RGB565: total={gpu_dust}, "
                f"scratch={scratch}, other={other_dust}"
            )
        if bad_begins:
            failures.append(f"{bad_begins} failed RGB565 bridge begins")

    result = "invalid" if invalid else "fail" if failures else "pass"
    return {
        "schema": "mtw-dust-rgb565-operation-gate-v1",
        "capture": str(capture.resolve()),
        "installed_hashes": observed_hashes,
        "expected_hashes": hashes,
        "phase_selected": selected,
        "phase_unselected": unselected,
        "scratch_dust_gpu_draws": scratch,
        "total_dust_gpu_draws": gpu_dust,
        "other_dust_gpu_draws": other_dust,
        "outside_same_resource_summary": outside,
        "outside_same_resource_events": outside_events,
        "failed_bridge_begins": bad_begins,
        "event_counts": dict(kinds),
        "requirements": {"min_each_phase": min_phase,
                         "min_rgb565_gpu_draws": min_scratch},
        "result": result,
        "invalid_reasons": invalid,
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--exe-sha256", type=exact_hash, required=True)
    parser.add_argument("--proxy-sha256", type=exact_hash, required=True)
    parser.add_argument("--backend-sha256", type=exact_hash, required=True)
    parser.add_argument("--min-each-phase", type=int, default=100)
    parser.add_argument("--min-rgb565-gpu-draws", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.min_each_phase < 1 or args.min_rgb565_gpu_draws < 1:
        parser.error("minimum exposure counts must be positive")
    hashes = {
        "Medieval_TW.exe": args.exe_sha256,
        "D3D9.dll": args.proxy_sha256,
        "dgVoodoo_D3D9.dll": args.backend_sha256,
    }
    report = inspect(args.capture, hashes, args.min_each_phase,
                     args.min_rgb565_gpu_draws)
    wire = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(wire, encoding="utf-8")
    print(wire, end="")
    return {"pass": 0, "fail": 1, "invalid": 2}[report["result"]]


if __name__ == "__main__":
    raise SystemExit(main())
