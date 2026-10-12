"""Live PE32 regression test for the dgVoodoo2 deferred dust draw.

Run only on a disposable stage containing the pinned replay. The game is
spawned, observed from hostlong 1174 through 1177, then stopped. The trace
checks actual D3D11 target formats and draw ordering, not source text.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import threading
import time

import frida


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def verify(events: list[dict]) -> dict:
    calls: list[dict] = []
    active: dict | None = None
    awaiting: dict | None = None
    for event in events:
        kind = event.get("kind")
        if kind == "d3d9_dust_enter":
            active = {"call": event["call"], "inside": [], "tail": None}
            calls.append(active)
            awaiting = None
        elif kind == "d3d9_dust_leave":
            if active is None or active["call"] != event["call"]:
                raise AssertionError("Unpaired game-facing dust call")
            awaiting, active = active, None
        elif kind == "dust_gpu_draw":
            if active is not None and event["active_d3d9_call"] == active["call"]:
                active["inside"].append(event)
            elif awaiting is not None and awaiting["inside"] and awaiting["tail"] is None:
                first = awaiting["inside"][0]
                if (event["shader"], event["texture_srv"]) == (
                        first["shader"], first["texture_srv"]):
                    awaiting["tail"] = event
                    awaiting = None
    completion = next((e for e in events if e.get("kind") == "scope_complete"), None)
    if completion is None or len(calls) < 20 or completion["d3d9_calls"] != len(calls):
        raise AssertionError("Incomplete bounded battle trace")
    for call in calls:
        if not call["inside"] or call["tail"] is None:
            raise AssertionError(f"Dust call {call['call']} lacks its translated tail")
        first = call["inside"][0]
        if any(d["target_format"] != 85 or d["target_rtv"] != first["target_rtv"]
               for d in call["inside"]):
            raise AssertionError(f"Dust call {call['call']} did not render on one RGB565 target")
        tail = call["tail"]
        if tail["target_format"] != 85 or tail["target_rtv"] != first["target_rtv"]:
            raise AssertionError(f"Dust call {call['call']} escaped RGB565 after API return")
    return {"d3d9_calls": len(calls),
            "in_call_gpu_draws": sum(len(c["inside"]) for c in calls),
            "deferred_tails_on_same_rgb565_target": len(calls),
            "deferred_tail_indices": sum(c["tail"]["index_count"] for c in calls)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-exe-sha256", required=True)
    parser.add_argument("--expected-replay-sha256", required=True)
    parser.add_argument("--expected-proxy-sha256", required=True)
    parser.add_argument("--expected-backend-sha256", required=True)
    parser.add_argument("--timeout", type=float, default=185)
    args = parser.parse_args()
    root, game, output = (p.resolve() for p in
                          (args.artifact_root, args.game, args.output))
    if not game.is_relative_to(root / "staged-games") or game.parent != root / "staged-games":
        raise ValueError("Game must be a direct task-owned staged copy")
    if not output.is_relative_to(root / "captures") or output.exists():
        raise ValueError("Output must be a fresh artifact capture directory")
    expected = {"Medieval_TW.exe": args.expected_exe_sha256,
                r"SaveGames\Battles\~tmp.vrp": args.expected_replay_sha256,
                "D3D9.dll": args.expected_proxy_sha256,
                "dgVoodoo_D3D9.dll": args.expected_backend_sha256}
    for name, digest in expected.items():
        actual = sha256(game / name)
        if actual != digest.upper():
            raise ValueError(f"Staged input identity mismatch: {name}: {actual}")
    source_path = Path(__file__).with_suffix(".js")
    source = source_path.read_text(encoding="utf-8")
    output.mkdir(parents=True)
    events: list[dict] = []
    errors: list[object] = []
    done = threading.Event()
    device = frida.get_local_device()
    pid = device.spawn([str(game / "Medieval_TW.exe"), "-replay", "~tmp"], cwd=str(game))
    session = device.attach(pid)
    script = None
    try:
        def on_message(message: dict, _data: bytes | None) -> None:
            if message.get("type") != "send":
                errors.append(message)
                done.set()
                return
            event = message["payload"]
            events.append(event)
            if event.get("kind") == "scope_error":
                errors.append(event)
            if event.get("kind") in ("scope_error", "scope_complete"):
                done.set()

        script = session.create_script(source)
        script.on("message", on_message)
        script.load()
        device.resume(pid)
        if not done.wait(args.timeout):
            errors.append("Timed out before hostlong 1177")
        time.sleep(0.15)
        ready = next((e for e in events if e.get("kind") == "ready"), {})
        if Path(ready.get("proxy", "")).resolve() != (game / "D3D9.dll").resolve():
            errors.append("Wrong live D3D9 proxy module")
        result = "failed"
        summary = None
        if not errors:
            try:
                summary = verify(events)
                result = "success"
            except AssertionError as exc:
                errors.append(str(exc))
        (output / "events.jsonl").write_text(
            "".join(json.dumps(e, sort_keys=True) + "\n" for e in events), encoding="utf-8")
        record = {"schema": "mtw-dust-deferred-tail-regression-v1",
                  "result": result, "game": str(game), "pid": pid,
                  "inputs_sha256": {name: sha256(game / name) for name in expected},
                  "script_sha256": sha256(source_path),
                  "events_count": len(events), "summary": summary, "errors": errors}
        (output / "capture.json").write_text(json.dumps(record, indent=2) + "\n")
        print(json.dumps(record, indent=2))
        return 0 if result == "success" else 1
    finally:
        device.kill(pid)
        if script is not None:
            try:
                script.unload()
            except Exception:
                pass
        session.detach()


if __name__ == "__main__":
    raise SystemExit(main())
