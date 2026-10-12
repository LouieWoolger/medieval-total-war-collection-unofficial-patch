"""Game-backed operation gate for the returned menu UpdateSurface conversion.

Run after the two bounded staged captures produced by
``notes/trace_menu_chain_noop.py`` and ``notes/trace_menu_chain_swap.py`` in
the investigation's G: artifact root::

    python tests/diagnostics/menu_update_surface_gate.py \
      --bad-capture G:\\...\\menu-update-surface-chain-noop-1 \
      --fixed-capture G:\\...\\menu-update-surface-chain-swap-1 \
      --output G:\\...\\analysis\\menu-operation-gate.json

Raw files 0008/0012/0013 are real 800x600 game menu resources sampled at
stable postfocus UpdateSurface #40: first RGBA input, third Draw RGBA input,
and third Draw R16 output. No synthetic picture is used in this gate.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


WIDTH = 800
HEIGHT = 600
RGBA_PITCH = WIDTH * 4
R16_PITCH = 1664


def score(source: bytes, destination: bytes) -> dict[str, float | int]:
    if len(source) != RGBA_PITCH * HEIGHT:
        raise ValueError(f"RGBA source length {len(source)}")
    if len(destination) != R16_PITCH * HEIGHT:
        raise ValueError(f"R16 output length {len(destination)}")
    exact = shifted = 0
    count = (WIDTH - 2) * (HEIGHT - 2)
    for y in range(1, HEIGHT - 1):
        for x in range(1, WIDTH - 1):
            output_at = y * R16_PITCH + x * 2
            actual = destination[output_at] | (destination[output_at + 1] << 8)
            same_at = y * RGBA_PITCH + x * 4
            shifted_at = (y + 1) * RGBA_PITCH + (x + 1) * 4
            for offset, which in ((same_at, 0), (shifted_at, 1)):
                expected = ((source[offset] >> 3) << 11 |
                            (source[offset + 1] >> 2) << 5 |
                            (source[offset + 2] >> 3))
                if actual == expected:
                    if which == 0:
                        exact += 1
                    else:
                        shifted += 1
    return {"pixels": count, "same_coordinate_ratio": exact / count,
            "plus_one_x_y_ratio": shifted / count}


def capture(directory: Path) -> dict[str, object]:
    result = json.loads((directory / "result.json").read_text(encoding="utf-8"))
    if result["status"] != "captured" or result["exe_sha256"] != (
        "982921CEFDED31C298F249742B8A001BB1A823F77FB3965E01892A34ED12E627"
    ):
        raise ValueError(f"Unexpected game capture: {directory}")
    first = (directory / "raw-0008-input.bin").read_bytes()
    third = (directory / "raw-0012-input.bin").read_bytes()
    output = (directory / "raw-0013-output.bin").read_bytes()
    return {"path": str(directory), "stats": result["stats"],
            "third_draw": score(third, output),
            "whole_update_surface": score(first, output)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bad-capture", type=Path, required=True)
    parser.add_argument("--fixed-capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    bad = capture(args.bad_capture)
    fixed = capture(args.fixed_capture)
    bad_draw = bad["third_draw"]
    good_draw = fixed["third_draw"]
    bad_net = bad["whole_update_surface"]
    good_net = fixed["whole_update_surface"]
    passed = (
        bad["stats"]["patched"] == 0 and fixed["stats"]["patched"] > 0 and
        bad_draw["plus_one_x_y_ratio"] >= .9999 and
        bad_draw["same_coordinate_ratio"] < .9 and
        good_draw["same_coordinate_ratio"] >= .9999 and
        bad_net["same_coordinate_ratio"] < .9 and
        good_net["same_coordinate_ratio"] >= .9999
    )
    report = {"bad": bad, "fixed": fixed, "pass": passed}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
