"""Observer-free Alt+Tab menu capture on one exact, task-owned game stage.

Example:
  py tests/diagnostics/menu_focus_stage_capture.py \
    --artifact-root <artifact-root> \
    --game <artifact-root>/staged-games/Steam-installed \
    --exe-sha256 <sha256> --proxy-sha256 <sha256> \
    --menu-reference <artifact-root>/captures/<known-clean-main-menu>.png \
    --output <artifact-root>/captures/Steam-menu-20

This changes no game files. The known-clean title artwork must match before the
first focus cycle; then run menu_focus_stage_gate.py on the capture.
"""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time

import mss
import numpy as np
import psutil
from PIL import Image


USER = ctypes.windll.user32
VK_ESCAPE, VK_MENU, VK_TAB = 0x1B, 0x12, 0x09
KEYUP = 2
TITLE = (950, 65, 1610, 390)
MAX_REFERENCE_TITLE_MAE = 2.0


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def foreground_pid() -> int:
    pid = ctypes.c_ulong()
    USER.GetWindowThreadProcessId(USER.GetForegroundWindow(), ctypes.byref(pid))
    return int(pid.value)


def windows(pid: int) -> list[int]:
    found: list[int] = []
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def callback(hwnd: int, _unused: int) -> bool:
        owner = ctypes.c_ulong()
        USER.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and USER.IsWindowVisible(hwnd):
            found.append(int(hwnd))
        return True

    USER.EnumWindows(callback_type(callback), 0)
    return found


def force_foreground(hwnd: int) -> None:
    foreground = USER.GetForegroundWindow()
    current_thread = ctypes.windll.kernel32.GetCurrentThreadId()
    foreground_thread = USER.GetWindowThreadProcessId(foreground, None) if foreground else 0
    target_thread = USER.GetWindowThreadProcessId(hwnd, None)
    attached_foreground = False
    attached_target = False
    try:
        if foreground_thread and foreground_thread != current_thread:
            attached_foreground = bool(USER.AttachThreadInput(current_thread, foreground_thread, True))
        if target_thread and target_thread != current_thread:
            attached_target = bool(USER.AttachThreadInput(current_thread, target_thread, True))
        USER.ShowWindow(hwnd, 5)
        USER.BringWindowToTop(hwnd)
        USER.SetForegroundWindow(hwnd)
        USER.SetFocus(hwnd)
    finally:
        if attached_target:
            USER.AttachThreadInput(current_thread, target_thread, False)
        if attached_foreground:
            USER.AttachThreadInput(current_thread, foreground_thread, False)


def tap(vk: int) -> None:
    USER.keybd_event(vk, 0, 0, 0)
    time.sleep(.08)
    USER.keybd_event(vk, 0, KEYUP, 0)


def alt_tab() -> None:
    USER.keybd_event(VK_MENU, 0, 0, 0)
    time.sleep(.08)
    USER.keybd_event(VK_TAB, 0, 0, 0)
    time.sleep(.10)
    USER.keybd_event(VK_TAB, 0, KEYUP, 0)
    time.sleep(.08)
    USER.keybd_event(VK_MENU, 0, KEYUP, 0)


def matching_processes(exe: Path) -> list[psutil.Process]:
    return [p for p in psutil.process_iter(("exe",))
            if p.info["exe"] and Path(p.info["exe"]).resolve() == exe.resolve()]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--proxy-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--menu-reference", type=Path, required=True,
                        help="Known-clean main-menu screenshot at 2560x1440")
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--intro-escapes", type=int, default=6)
    parser.add_argument("--monitor", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100 or not 0 <= args.intro_escapes <= 20:
        parser.error("cycles must be 1..100 and intro-escapes 0..20")
    args.exe_sha256 = args.exe_sha256.upper()
    args.proxy_sha256 = args.proxy_sha256.upper()
    if any(len(value) != 64 or any(ch not in "0123456789ABCDEF" for ch in value)
           for value in (args.exe_sha256, args.proxy_sha256)):
        parser.error("Both SHA-256 values must be 64 hex characters")
    return args


def main() -> int:
    args = parse_args()
    root, game, out, reference_path = (path.resolve() for path in
        (args.artifact_root, args.game, args.output, args.menu_reference))
    exe, proxy = game / "Medieval_TW.exe", game / "D3D9.dll"
    if not game.is_relative_to(root / "staged-games"):
        raise ValueError("Game must be a task-owned staged copy under artifact-root/staged-games")
    if not out.is_relative_to(root / "captures"):
        raise ValueError("Output must be under artifact-root/captures")
    if not reference_path.is_relative_to(root) or not reference_path.is_file():
        raise ValueError("Menu reference must be an existing artifact from this run")
    if out.exists():
        raise FileExistsError(out)
    if sha(exe) != args.exe_sha256 or sha(proxy) != args.proxy_sha256:
        raise ValueError("Exact EXE or proxy hash does not match requested stage")
    if matching_processes(exe):
        raise RuntimeError("Exact staged executable is already running")

    out.mkdir(parents=True)
    result = {"stage": str(game), "exe_sha256": args.exe_sha256,
              "proxy_sha256": args.proxy_sha256, "cycles_requested": args.cycles,
              "intro_escapes": args.intro_escapes,
              "menu_reference": str(reference_path),
              "menu_reference_sha256": sha(reference_path), "events": []}
    proc = subprocess.Popen([str(exe)], cwd=game)
    result["pid"] = proc.pid
    try:
        deadline = time.monotonic() + 50
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError(f"Game exited during startup: {proc.returncode}")
            game_windows = windows(proc.pid)
            if game_windows and foreground_pid() != proc.pid:
                force_foreground(game_windows[0])
            if game_windows and foreground_pid() == proc.pid:
                break
            time.sleep(.2)
        else:
            raise RuntimeError("Staged game did not become foreground")
        for _ in range(args.intro_escapes):
            if proc.poll() is not None:
                raise RuntimeError(f"Game exited while skipping intro: {proc.returncode}")
            tap(VK_ESCAPE)
            time.sleep(1.5)
        time.sleep(4)
        with mss.MSS() as screen:
            if args.monitor >= len(screen.monitors):
                raise ValueError(f"Monitor {args.monitor} does not exist")
            monitor = screen.monitors[args.monitor]

            def record(label: str) -> None:
                filename = f"{len(result['events']):02d}-{label}.png"
                screen.shot(mon=args.monitor, output=str(out / filename))
                result["events"].append({"label": label, "utc": now(),
                    "foreground_pid": foreground_pid(), "windows": windows(proc.pid),
                    "monitor": monitor, "image": filename,
                    "sha256": sha(out / filename)})
                (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

            record("settled")
            reference = np.asarray(Image.open(reference_path).convert("RGB"))
            baseline = np.asarray(Image.open(out / result["events"][0]["image"]).convert("RGB"))
            if reference.shape != baseline.shape or baseline.shape != (1440, 2560, 3):
                raise RuntimeError("Known menu reference and baseline must both be 2560x1440")
            left, top, right, bottom = TITLE
            title_diff = np.abs(reference[top:bottom, left:right].astype(np.int16)
                                - baseline[top:bottom, left:right].astype(np.int16))
            result["baseline_title_reference_mae"] = float(title_diff.mean())
            if result["baseline_title_reference_mae"] > MAX_REFERENCE_TITLE_MAE:
                raise RuntimeError("Baseline is not the known main menu title artwork: "
                                   f"MAE={result['baseline_title_reference_mae']:.3f}")
            for cycle in range(1, args.cycles + 1):
                if proc.poll() is not None:
                    raise RuntimeError(f"Game exited before cycle {cycle}: {proc.returncode}")
                alt_tab()
                time.sleep(.5)
                record(f"cycle{cycle}-away")
                game_windows = windows(proc.pid)
                if not game_windows or proc.poll() is not None:
                    raise RuntimeError(f"Game lost window after focus departure in cycle {cycle}")
                force_foreground(game_windows[0])
                time.sleep(.10)
                record(f"cycle{cycle}-return-first")
                time.sleep(1.0)
                record(f"cycle{cycle}-return-settled")
        target = psutil.Process(proc.pid)
        result["modules"] = sorted({str(item.path) for item in target.memory_maps()
            if Path(item.path).name.lower() in
            ("d3d9.dll", "dgvoodoo_d3d9.dll", "d3d11.dll")})
        result["alive_at_last_focus_frame"] = proc.poll() is None
        result["status"] = "captured"
    except Exception as exc:
        result["status"] = "failed"
        result["error"] = repr(exc)
        raise
    finally:
        cleanup = {"method": "already_exited" if proc.poll() is not None else "WM_CLOSE",
                   "normal_exit_verified": False}
        if proc.poll() is None:
            for hwnd in windows(proc.pid):
                USER.PostMessageW(hwnd, 0x0010, 0, 0)
            try:
                proc.wait(timeout=5)
                cleanup["normal_exit_verified"] = proc.returncode == 0
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(timeout=5)
                cleanup["method"] = "forced_terminate_after_WM_CLOSE_timeout"
        result["returncode"] = proc.returncode
        cleanup["returncode"] = proc.returncode
        cleanup["abnormal_WM_CLOSE_exit"] = (cleanup["method"] == "WM_CLOSE"
                                                and proc.returncode != 0)
        result["cleanup"] = cleanup
        (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({"status": result["status"], "pid": proc.pid,
                      "events": len(result["events"]), "modules": result["modules"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
