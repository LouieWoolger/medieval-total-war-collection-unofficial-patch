"""Fail a real-game GPU trace if translated dust escapes the RGB565 target.

Input is the JSONL emitted by the bounded x86 backend-original-draw probe.
This gate is intentionally separate from source arithmetic and load checks.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def read_jsonl(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()]


def check_trace(events: list[dict], status: list[dict] | None = None) -> dict:
    kinds = {row.get("kind") for row in events}
    summaries = [row for row in events if row.get("kind") == "summary"]
    small_scopes = [row for row in events if row.get("kind") == "small_candidate_scope"]
    deferred = [row for row in events if row.get("kind") == "deferred_gpu_on_scratch"]
    outside = [row for row in events if row.get("kind") == "dust_gpu_outside_scratch"
               and row.get("same_dust_resource") is True]
    errors = [row for row in events if row.get("kind") in ("scope_error", "frida_error")]
    last = summaries[-1] if summaries else {}
    delta_no_incall = None
    if status is not None and len(status) >= 2:
        delta_no_incall = (status[-1]["render"]["no_gpu_draws"] -
                           status[0]["render"]["no_gpu_draws"])
    failures = []
    if "ready" not in kinds or "context_hooked" not in kinds:
        failures.append("backend original draw was not hooked")
    if not summaries or last.get("gpu_dust", 0) < 1 or last.get("scratch_dust", 0) < 1:
        failures.append("no corrected game dust GPU draws were observed")
    if last.get("d3d9_small_candidates", 0) < 1:
        failures.append("small dust batches were not exercised")
    if not small_scopes or any(row.get("selected_during_call") is not True
                               for row in small_scopes):
        failures.append("an observed small dust batch was not selected")
    if not deferred or any(row.get("same_dust_resource") is not True
                           for row in deferred):
        failures.append("no verified deferred dust GPU draw reached scratch")
    if (last.get("gpu_dust", 0) != last.get("scratch_dust", 0) or
            last.get("other_dust", 0) != 0):
        failures.append("the summary counts dust GPU draws outside scratch")
    if status is not None and (delta_no_incall is None or delta_no_incall < 1):
        failures.append("fully deferred or culled D3D9 dust calls were not exercised")
    if status is not None and len(status) >= 2:
        for field in ("failed_draws", "begin_failures", "end_failures",
                      "tail_missing", "d3d11_context_misses", "d3d11_thread_misses"):
            if status[-1]["render"][field] != status[0]["render"][field]:
                failures.append(f"{field} increased during the trace")
    if errors:
        failures.append(f"trace had {len(errors)} instrumentation error(s)")
    if outside:
        first = outside[0]
        failures.append(f"{len(outside)} dust GPU draw(s) escaped RGB565; "
                        f"first hostlong={first.get('hostlong')} "
                        f"format={first.get('target_format')} "
                        f"indices={first.get('index_count')}")
    return {"result": "fail" if failures else "pass", "failures": failures,
            "gpu_dust_draws": last.get("gpu_dust", 0),
            "scratch_dust_draws": last.get("scratch_dust", 0),
            "small_d3d9_batches": last.get("d3d9_small_candidates", 0),
            "small_selected_samples": sum(row.get("selected_during_call") is True
                                          for row in small_scopes),
            "deferred_scratch_samples": len(deferred),
            "no_incall_draw_delta": delta_no_incall,
            "outside_same_dust_resource": len(outside)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", type=Path, required=True)
    parser.add_argument("--status", type=Path)
    args = parser.parse_args()
    result = check_trace(read_jsonl(args.events),
                         read_jsonl(args.status) if args.status else None)
    print(json.dumps(result, indent=2))
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
