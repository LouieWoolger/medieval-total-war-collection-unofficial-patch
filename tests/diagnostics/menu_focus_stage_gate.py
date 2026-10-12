"""Check an exact staged main-menu focus capture against its own clean frame.

The capture harness first checks its settled frame against known clean title
artwork. This gate checks both first and settled focus returns over all cycles.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


REGIONS = {"title": (950, 65, 1610, 390),
           "footer": (905, 1240, 1655, 1380)}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-cycles", type=int, default=20)
    parser.add_argument("--max-mae", type=float, default=2.0)
    args = parser.parse_args()
    capture = args.capture.resolve()
    result = json.loads((capture / "result.json").read_text(encoding="utf-8"))
    events = result["events"]
    if result.get("status") != "captured" or not events or events[0]["label"] != "settled":
        raise ValueError("Capture did not finish on a settled menu baseline")
    if result.get("alive_at_last_focus_frame") is not True:
        raise ValueError("Game was not alive at the last focus frame")
    reference_path = Path(result["menu_reference"])
    if sha(reference_path) != result["menu_reference_sha256"]:
        raise ValueError("Known-clean menu reference changed after capture")
    if result.get("baseline_title_reference_mae", float("inf")) > 2.0:
        raise ValueError("Capture baseline did not match known menu title")
    cycles = result["cycles_requested"]
    if cycles < args.minimum_cycles or len(events) != 1 + 3 * cycles:
        raise ValueError(f"Incomplete cycle count: cycles={cycles}, events={len(events)}")

    baseline = np.asarray(Image.open(capture / events[0]["image"]).convert("RGB"))
    if baseline.shape != (1440, 2560, 3):
        raise ValueError(f"Unexpected baseline shape: {baseline.shape}")
    comparisons = []
    for cycle in range(1, cycles + 1):
        trio = events[1 + (cycle - 1) * 3:1 + cycle * 3]
        labels = [entry["label"] for entry in trio]
        if labels != [f"cycle{cycle}-away", f"cycle{cycle}-return-first",
                      f"cycle{cycle}-return-settled"]:
            raise ValueError(f"Unexpected cycle {cycle} labels: {labels}")
        for entry in trio[1:]:
            if entry["foreground_pid"] != result["pid"]:
                raise ValueError(f"Game was not foreground in {entry['label']}")
            path = capture / entry["image"]
            if sha(path) != entry["sha256"]:
                raise ValueError(f"Capture image changed: {path}")
            frame = np.asarray(Image.open(path).convert("RGB"))
            if frame.shape != baseline.shape:
                raise ValueError(f"Capture shape changed: {entry['image']}")
            comparison = {"label": entry["label"]}
            for name, (left, top, right, bottom) in REGIONS.items():
                clean = baseline[top:bottom, left:right]
                actual = frame[top:bottom, left:right]
                difference = np.abs(clean.astype(np.int16) - actual.astype(np.int16))
                comparison[name] = {"mean_absolute_error": float(difference.mean()),
                    "exact_pixel_fraction": float(np.all(clean == actual, axis=2).mean())}
            comparisons.append(comparison)

    maximum = max(row[region]["mean_absolute_error"]
                  for row in comparisons for region in REGIONS)
    cleanup = result.get("cleanup", {})
    report = {"capture": str(capture), "cycles": cycles,
              "return_frames": len(comparisons),
              "baseline_title_reference_mae": result["baseline_title_reference_mae"],
              "regions": REGIONS, "comparisons": comparisons,
              "max_return_mae": maximum, "max_allowed_mae": args.max_mae,
              "cleanup": cleanup,
              "focus_pass": maximum <= args.max_mae,
              "pass": maximum <= args.max_mae and not cleanup.get("abnormal_WM_CLOSE_exit", True)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: report[key] for key in
          ("pass", "focus_pass", "cycles", "return_frames", "baseline_title_reference_mae",
           "max_return_mae", "max_allowed_mae", "cleanup")}))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
