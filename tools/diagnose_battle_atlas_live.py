"""Read the bounded battle-icon atlas state from a staged MTW I process."""

from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import json
import struct
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--expect-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    kernel32.ReadProcessMemory.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
    ]
    kernel32.QueryFullProcessImageNameW.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_wchar_p,
        ctypes.POINTER(ctypes.c_uint32),
    ]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel32.OpenProcess(0x1010, False, args.pid)
    if not handle:
        raise OSError(ctypes.get_last_error(), "OpenProcess failed")
    try:
        name = ctypes.create_unicode_buffer(32768)
        length = ctypes.c_uint32(len(name))
        if not kernel32.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(length)):
            raise OSError(ctypes.get_last_error(), "QueryFullProcessImageNameW failed")
        actual = Path(name.value).resolve()
        expected = args.expect_exe.resolve()
        if str(actual).casefold() != str(expected).casefold():
            raise ValueError(f"PID executable mismatch: {actual} != {expected}")

        def read(address: int, size: int) -> bytes:
            if not 0 < size <= 16:
                raise ValueError("invalid bounded read")
            buffer = ctypes.create_string_buffer(size)
            transferred = ctypes.c_size_t()
            if not kernel32.ReadProcessMemory(
                handle, ctypes.c_void_p(address), buffer, size,
                ctypes.byref(transferred),
            ) or transferred.value != size:
                raise OSError(ctypes.get_last_error(), f"ReadProcessMemory {address:#x}")
            return buffer.raw

        def dword(address: int) -> int:
            return struct.unpack("<I", read(address, 4))[0]

        scene = dword(0x00B4275C)
        surface_record = dword(0x00B43678)
        state = {
            "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "pid": args.pid,
            "executable": str(actual),
            "mode": dword(0x00E77610),
            "render_root": dword(0x00E6F5D0),
            "scene": scene,
            "cache_first": dword(0x007B9354),
            "surface_record": surface_record,
            "surface": dword(surface_record) if surface_record else 0,
            "dirty": read(scene + 0x94C, 1)[0] if scene else None,
            "init_callsite": read(0x00423E20, 5).hex(),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(state))
        return 0
    finally:
        kernel32.CloseHandle(ctypes.c_void_p(handle))


if __name__ == "__main__":
    raise SystemExit(main())
