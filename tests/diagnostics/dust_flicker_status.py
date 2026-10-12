"""Read the R187 dust status blocks without injecting into the game.

The two existing mappings are process-local and versioned. This sampler rejects
unknown layouts and writes one timestamped JSON object per line, so a fixed
camera capture can be aligned with source-side hook/render decisions.
"""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import time

import psutil


SIM_FIELDS = (
    "magic", "version", "pid", "executable_accepted", "child_hook_installed",
    "eligibility_hook_installed", "install_complete", "cadence_step_microseconds",
    "virtual_manager_fps", "normalized_child_updates", "normalized_type0_updates",
    "normalized_type1_updates", "producer_updates_executed", "producer_updates_skipped",
    "cadence_frames_allowed", "cadence_frames_skipped", "eligibility_hook_hits",
    "eligibility_overrides", "elapsed_min_ms", "elapsed_max_ms", "last_battle_tick",
    "dense_mode", "dense_entries", "dense_exits",
)
RENDER_FIELDS = (
    "magic", "version", "pid", "selected_draws", "corrected_draws",
    "failed_draws", "translated_draws", "last_error", "last_target_format",
    "d3d11_hook_calls", "d3d11_pending_calls", "d3d11_context_misses",
    "d3d11_thread_misses", "registered_context", "no_gpu_draws",
    "begin_failures", "end_failures", "last_total_primitives",
    "last_dust_primitives", "estimated_step_us", "last_total_vertices",
    "last_dust_vertices", "last_draw_calls", "scratch_bound_translated",
    "scratch_unbound_translated", "last_dust_area_pixels", "tail_corrected",
    "tail_missing", "tail_target_changes",
)
SIM_MAGIC, SIM_VERSION = 0x4457544D, 2
RENDER_MAGIC, RENDER_VERSION = 0x5244574D, 8


def decode_status(blob: bytes, fields: tuple[str, ...], magic: int,
                  version: int, pid: int) -> dict[str, int]:
    size = 4 * len(fields)
    if len(blob) != size:
        raise ValueError(f"Expected {size} status bytes, got {len(blob)}")
    row = dict(zip(fields, struct.unpack(f"<{len(fields)}I", blob), strict=True))
    if (row["magic"], row["version"], row["pid"]) != (magic, version, pid):
        raise ValueError(f"Unsupported or mismatched status mapping: "
                         f"{row['magic']:08X}/v{row['version']}/pid{row['pid']}")
    return row


def read_mapping(pid: int, suffix: str, fields: tuple[str, ...],
                 magic: int, version: int) -> dict[str, int] | None:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenFileMappingW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
    kernel.OpenFileMappingW.restype = ctypes.c_void_p
    kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t]
    kernel.MapViewOfFile.restype = ctypes.c_void_p
    kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    name = f"Local\\MTW_DUST_{suffix}_{pid}"
    handle = kernel.OpenFileMappingW(4, 0, name)
    if not handle:
        return None
    try:
        view = kernel.MapViewOfFile(handle, 4, 0, 0, 4 * len(fields))
        if not view:
            return None
        try:
            return decode_status(ctypes.string_at(view, 4 * len(fields)),
                                 fields, magic, version, pid)
        finally:
            kernel.UnmapViewOfFile(view)
    finally:
        kernel.CloseHandle(handle)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=180)
    parser.add_argument("--interval", type=float, default=.25)
    args = parser.parse_args()
    if not 0 < args.seconds <= 900 or not .1 <= args.interval <= 5:
        raise ValueError("Sampling bounds exceeded")
    target = (args.game.resolve(strict=True) / "Medieval_TW.exe").resolve(strict=True)
    process = psutil.Process(args.pid)
    if Path(process.exe()).resolve(strict=True) != target:
        raise ValueError("PID does not belong to the selected game")
    output = args.output.resolve()
    if output.exists() or not output.parent.is_dir():
        raise ValueError("Output must be a new file in an existing directory")
    start = time.monotonic()
    samples = 0
    with output.open("x", encoding="utf-8", buffering=1) as stream:
        while time.monotonic() - start < args.seconds and process.is_running():
            if Path(process.exe()).resolve() != target:
                raise RuntimeError("PID identity changed")
            sim = read_mapping(args.pid, "STATUS", SIM_FIELDS, SIM_MAGIC, SIM_VERSION)
            render = read_mapping(args.pid, "RENDER_STATUS", RENDER_FIELDS,
                                  RENDER_MAGIC, RENDER_VERSION)
            if sim is not None and render is not None:
                row = {"utc": datetime.now(timezone.utc).isoformat(),
                       "perf_counter_ns": time.perf_counter_ns(),
                       "elapsed_seconds": round(time.monotonic() - start, 6),
                       "pid": args.pid, "simulation": sim, "render": render}
                stream.write(json.dumps(row, separators=(",", ":")) + "\n")
                samples += 1
            time.sleep(args.interval)
    if not samples:
        raise RuntimeError("No pair of R187 status mappings was observed")
    print(json.dumps({"samples": samples, "output": str(output)}))


if __name__ == "__main__":
    main()
