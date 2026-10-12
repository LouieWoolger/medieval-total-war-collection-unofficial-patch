"""Protect the game-trace gate from accepting an observer-created result."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path


GATE_PATH = Path(__file__).parent / "diagnostics/dust_rgb565_operation_gate.py"
SPEC = importlib.util.spec_from_file_location("dust_rgb565_operation_gate", GATE_PATH)
assert SPEC is not None and SPEC.loader is not None
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
HASHES = {name: "A" * 64 for name in
          ("Medieval_TW.exe", "D3D9.dll", "dgVoodoo_D3D9.dll")}


def capture(tmp_path: Path, *, observer_safe: bool = True,
            gray_selected: int = 101, other_dust: int = 0) -> Path:
    marker = ("backend_only_no_selector_hook" if observer_safe else
              "proxy_entry_onleave")
    manifest = {
        "schema": "mtw-dust-gray-candidate-backend-trace-v2",
        "selector_observation": marker,
        "result": "captured", "errors": [], "inputs_sha256": HASHES,
    }
    events = [
        {"kind": "backend_d3d9_hook", "selector_observation": marker,
         "backend": r"C:\Stage\dgVoodoo_D3D9.dll"},
        {"kind": "ready"},
        {"kind": "context_hooked", "hook_site": "backend_original_draw_indexed"},
        {"kind": "d3d9_dust_color_candidate"},
        {"kind": "begin_result", "result": 0},
        {"kind": "family_summary", "family_total": 202,
         "family_unselected": 0 if gray_selected else 101,
         "phase_selected": {"gray": gray_selected, "brown": 101},
         "phase_unselected": {} if gray_selected else {"gray": 101},
         "gpu_dust": 200, "scratch_dust": 200 - other_dust,
         "other_dust": other_dust,
         "outside_same_resource": other_dust},
    ]
    tmp_path.mkdir(parents=True, exist_ok=True)
    (tmp_path / "capture.json").write_text(json.dumps(manifest), encoding="utf-8")
    (tmp_path / "events.jsonl").write_text(
        "\n".join(json.dumps(event) for event in events) + "\n", encoding="utf-8")
    return tmp_path


def test_safe_complete_backend_capture_passes(tmp_path: Path) -> None:
    report = GATE.inspect(capture(tmp_path), HASHES, 100, 100)
    assert report["result"] == "pass"


def test_proxy_entry_observer_is_invalid(tmp_path: Path) -> None:
    report = GATE.inspect(capture(tmp_path, observer_safe=False), HASHES, 100, 100)
    assert report["result"] == "invalid"


def test_gray_bypass_fails(tmp_path: Path) -> None:
    report = GATE.inspect(capture(tmp_path, gray_selected=0), HASHES, 100, 100)
    assert report["result"] == "fail"


def test_wrong_target_fails(tmp_path: Path) -> None:
    report = GATE.inspect(capture(tmp_path, other_dust=1), HASHES, 100, 100)
    assert report["result"] == "fail"
