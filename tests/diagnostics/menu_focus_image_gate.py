"""Compare captured 2560x1440 menu title/footer across Alt+Tab returns.

The fixed rectangles are static art in the recorded Steam control and candidate.
Moving background art and the menu cursor are outside these rectangles. This
gate consumes game screenshots; it does not synthesize the dust/menu renderer.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


REGIONS = {
    "title": (950, 65, 1610, 390),
    "footer": (905, 1240, 1655, 1380),
}


def evaluate(directory: Path) -> dict:
    result = json.loads((directory / "result.json").read_text(encoding="utf-8"))
    events = result["events"]
    if not events or events[0]["label"] != "settled":
        raise ValueError(f"Missing clean settled frame: {directory}")
    reference = np.asarray(Image.open(directory / events[0]["image"]).convert("RGB"))
    if reference.shape != (1440, 2560, 3):
        raise ValueError(f"Expected 2560x1440 game capture: {reference.shape}")
    returns = [event for event in events if "return-" in event["label"]]
    if not returns:
        raise ValueError(f"No Alt+Tab return frames: {directory}")
    comparisons = []
    for event in returns:
        frame = np.asarray(Image.open(directory / event["image"]).convert("RGB"))
        if frame.shape != reference.shape:
            raise ValueError(f"Capture size changed: {event['image']}")
        entry = {"label": event["label"]}
        for name, (left, top, right, bottom) in REGIONS.items():
            clean = reference[top:bottom, left:right]
            observed = frame[top:bottom, left:right]
            difference = np.abs(clean.astype(np.int16) - observed.astype(np.int16))
            entry[name] = {
                "mean_absolute_error": float(difference.mean()),
                "exact_pixel_fraction": float(np.all(clean == observed, axis=2).mean()),
            }
        comparisons.append(entry)
    return {"directory": str(directory), "status": result["status"],
            "return_frames": len(comparisons), "comparisons": comparisons}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bad-capture", type=Path, required=True)
    parser.add_argument("--fixed-capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    bad = evaluate(args.bad_capture)
    fixed = evaluate(args.fixed_capture)
    bad_errors = [entry[region]["mean_absolute_error"]
                  for entry in bad["comparisons"] for region in REGIONS]
    fixed_errors = [entry[region]["mean_absolute_error"]
                    for entry in fixed["comparisons"] for region in REGIONS]
    report = {"regions": REGIONS, "bad": bad, "fixed": fixed,
              "bad_min_mae": min(bad_errors), "fixed_max_mae": max(fixed_errors),
              "pass": bad["status"] == fixed["status"] == "captured"
                      and bad["return_frames"] >= 10
                      and fixed["return_frames"] >= 40
                      and min(bad_errors) >= 20.0
                      and max(fixed_errors) <= 2.0}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"pass": report["pass"],
                      "bad_return_frames": bad["return_frames"],
                      "fixed_return_frames": fixed["return_frames"],
                      "bad_min_mae": report["bad_min_mae"],
                      "fixed_max_mae": report["fixed_max_mae"]}))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
