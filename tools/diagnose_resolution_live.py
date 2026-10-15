"""Read a bounded MTW I PE32 resolution snapshot without changing the game.

This is an investigation aid for the supported executable, not a generic
process scanner. Use it only against a staged copy and pass the exact expected
executable path so a reused PID cannot silently target another process.
"""

from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import json
import struct
from pathlib import Path


PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_VM_READ = 0x0010
ADAPTERS = 0x00E7A1E0
ADAPTER_SIZE = 96
RECORD_SIZE = 32
MAX_RECORDS = 512


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

    handle = kernel32.OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, args.pid
    )
    if not handle:
        raise OSError(ctypes.get_last_error(), "OpenProcess failed")
    try:
        name = ctypes.create_unicode_buffer(32768)
        length = ctypes.c_uint32(len(name))
        if not kernel32.QueryFullProcessImageNameW(
            handle, 0, name, ctypes.byref(length)
        ):
            raise OSError(ctypes.get_last_error(), "QueryFullProcessImageNameW failed")
        actual = Path(name.value).resolve()
        expected = args.expect_exe.resolve()
        if str(actual).casefold() != str(expected).casefold():
            raise ValueError(f"PID executable mismatch: {actual} != {expected}")

        def read(address: int, length: int) -> bytes:
            if length <= 0 or length > 32768:
                raise ValueError(f"Invalid read length {length}")
            buffer = ctypes.create_string_buffer(length)
            transferred = ctypes.c_size_t()
            if not kernel32.ReadProcessMemory(
                handle, ctypes.c_void_p(address), buffer, length,
                ctypes.byref(transferred),
            ) or transferred.value != length:
                raise OSError(ctypes.get_last_error(), f"ReadProcessMemory {address:#x}")
            return buffer.raw

        def dword(address: int) -> int:
            return struct.unpack("<I", read(address, 4))[0]

        adapter = dword(0x00895F8C)
        if adapter > 63:
            raise ValueError(f"Adapter index out of range: {adapter}")
        adapter_base = ADAPTERS + adapter * ADAPTER_SIZE
        record_count = dword(adapter_base + 0x28)
        strategy_count = dword(adapter_base + 0x24)
        battle_count = dword(adapter_base + 0x20)
        records_address = dword(adapter_base + 0x34)
        strategy_begin = dword(adapter_base + 0x54)
        strategy_end = dword(adapter_base + 0x58)
        battle_begin = dword(adapter_base + 0x44)
        battle_end = dword(adapter_base + 0x48)
        if not (0 < record_count <= MAX_RECORDS and
                0 < strategy_count <= MAX_RECORDS and
                0 < battle_count <= MAX_RECORDS and
                strategy_end - strategy_begin == 4 * strategy_count and
                battle_end - battle_begin == 4 * battle_count):
            raise ValueError("Adapter record/selector counts are inconsistent")

        records_blob = read(records_address, record_count * RECORD_SIZE)
        records = []
        for index in range(record_count):
            data = records_blob[index * RECORD_SIZE:(index + 1) * RECORD_SIZE]
            records.append({
                "index": index, "flags": list(data[:4]),
                "width": struct.unpack_from("<I", data, 4)[0],
                "height": struct.unpack_from("<I", data, 8)[0],
                "raw_hex": data.hex(),
            })

        def selectors(begin: int, count: int) -> list[dict]:
            indices = struct.unpack(f"<{count}I", read(begin, 4 * count))
            if any(index >= record_count for index in indices):
                raise ValueError("Selector references an invalid record")
            return [{"position": pos, **records[index]}
                    for pos, index in enumerate(indices)]

        snapshot = {
            "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "pid": args.pid,
            "executable": str(actual),
            "adapter_index": adapter,
            "strategy_dimensions": [dword(0x007A2788), dword(0x007A278C)],
            "battle_dimensions": [dword(0x007A2780), dword(0x007A2784)],
            "active_strategy_index": dword(0x00E7D1F0),
            "stored_strategy_index": dword(0x00E77B04),
            "strategy_mode_pick_hook_bytes": read(0x006DE981, 6).hex(),
            "record_count": record_count,
            "records": records,
            "strategy_selectors": selectors(strategy_begin, strategy_count),
            "battle_selectors": selectors(battle_begin, battle_count),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(snapshot, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({
            "output": str(args.output),
            "strategy_dimensions": snapshot["strategy_dimensions"],
            "active_strategy_index": snapshot["active_strategy_index"],
            "stored_strategy_index": snapshot["stored_strategy_index"],
            "matching_1920_modes": [
                entry for entry in snapshot["strategy_selectors"]
                if entry["width"] == 1920
            ],
        }))
        return 0
    finally:
        kernel32.CloseHandle(ctypes.c_void_p(handle))


if __name__ == "__main__":
    raise SystemExit(main())
