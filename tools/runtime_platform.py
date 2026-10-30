"""Bounded XP SP3 load contract for feature-only PE32 proxies.

Import inspection cannot establish execution on XP or in-game behavior.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pefile

# fast, icon, zoom, credits; Terrain is absent from all these payloads.
FEATURE_VARIANTS = {
    "fast-only": (1, 0, 0, 0),
    "icon-only": (0, 1, 0, 0),
    "fast-icon": (1, 1, 0, 0),
    "zoom-only": (0, 0, 1, 0),
    "fast-only-zoom": (1, 0, 1, 0),
    "icon-only-zoom": (0, 1, 1, 0),
    "fast-icon-zoom": (1, 1, 1, 0),
    "credits-only": (0, 0, 0, 1),
    "fast-only-credits": (1, 0, 0, 1),
    "icon-only-credits": (0, 1, 0, 1),
    "fast-icon-credits": (1, 1, 0, 1),
    "zoom-only-credits": (0, 0, 1, 1),
    "fast-only-zoom-credits": (1, 0, 1, 1),
    "icon-only-zoom-credits": (0, 1, 1, 1),
    "fast-icon-zoom-credits": (1, 1, 1, 1),
}

# Deliberately bounded named imports. New CRT/OS dependencies require review.
XP_RUNTIME_IMPORTS = {
    "advapi32.dll": set("""CryptAcquireContextW CryptCreateHash CryptDestroyHash
        CryptGetHashParam CryptHashData CryptReleaseContext RegCloseKey
        RegCreateKeyExW RegOpenKeyExW RegQueryValueExW RegSetValueExW""".split()),
    "kernel32.dll": set("""CloseHandle CreateFileW CreateToolhelp32Snapshot
        DeleteCriticalSection EnterCriticalSection FlushInstructionCache FreeLibrary
        GetCurrentProcess GetCurrentProcessId GetCurrentThreadId GetFileSizeEx
        GetLastError GetModuleFileNameW GetModuleHandleA GetModuleHandleW GetProcAddress
        GetSystemDirectoryW GetThreadContext HeapAlloc HeapCreate HeapDestroy HeapFree
        HeapReAlloc InitializeCriticalSection LeaveCriticalSection LoadLibraryA LoadLibraryW
        OpenThread OutputDebugStringA QueryPerformanceCounter QueryPerformanceFrequency
        ReadFile ReadProcessMemory ResumeThread SetThreadContext Sleep SuspendThread
        Thread32First Thread32Next TlsGetValue VirtualAlloc VirtualFree VirtualProtect
        VirtualQuery WriteProcessMemory""".split()),
    "user32.dll": {"GetForegroundWindow", "GetWindowThreadProcessId"},
    "msvcrt.dll": set("""__p__iob _amsg_exit _initterm _lock _unlock abort calloc
        fprintf free memcmp memcpy memset realloc strcmp strlen strncmp vfprintf wcsrchr""".split()),
}


def file_record(path: Path) -> dict:
    data = path.read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest().upper(), "length": len(data)}


def audit_feature_proxy(data: bytes) -> dict:
    with pefile.PE(data=data) as pe:
        subsystem = [pe.OPTIONAL_HEADER.MajorSubsystemVersion,
                     pe.OPTIONAL_HEADER.MinorSubsystemVersion]
        if (pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B or
                not pe.FILE_HEADER.Characteristics & 0x2000 or subsystem != [5, 1]):
            raise ValueError("Feature runtime must be a PE32/i386 DLL with subsystem 5.1.")
        if pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].VirtualAddress:
            raise ValueError("Feature runtime cannot depend on the CLR.")
        imports = {}
        for table, index in (("DIRECTORY_ENTRY_IMPORT", 1), ("DIRECTORY_ENTRY_DELAY_IMPORT", 13)):
            directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[index]
            entries = getattr(pe, table, [])
            if (directory.VirtualAddress or directory.Size) and not entries:
                raise ValueError("Feature runtime has an unreadable import directory.")
            for entry in entries:
                dll = entry.dll.decode("ascii").lower()
                if dll not in XP_RUNTIME_IMPORTS:
                    raise ValueError(f"Unreviewed XP SP3 runtime library: {dll}")
                names = imports.setdefault(dll, set())
                for item in entry.imports:
                    if not item.name:
                        raise ValueError("Ordinal runtime imports cannot satisfy the XP SP3 audit.")
                    name = item.name.decode("ascii")
                    if name not in XP_RUNTIME_IMPORTS[dll]:
                        raise ValueError(f"Unreviewed XP SP3 runtime import: {dll}!{name}")
                    names.add(name)
        if not imports:
            raise ValueError("Feature runtime has no readable imports.")
        exports = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
        if (exports is None or len(exports.symbols) != 1 or
                exports.symbols[0].name != b"Direct3DCreate9" or
                exports.symbols[0].ordinal != 16 or exports.symbols[0].forwarder):
            raise ValueError("Feature runtime must export Direct3DCreate9 at ordinal 16.")
        return {"machine": "I386", "subsystem_version": subsystem,
                "named_imports": {dll: sorted(names) for dll, names in sorted(imports.items())},
                "export": {"name": "Direct3DCreate9", "ordinal": 16},
                "policy": "XP SP3 static import audit; execution and gameplay unverified"}


def audit_packaged_features(runtime: Path) -> dict:
    records = {}
    for name in FEATURE_VARIANTS:
        manifest = json.loads((runtime / f"payload-manifest-{name}.json").read_text(encoding="utf-8"))
        binary = runtime / f"D3D9-{name}.dll"
        record = file_record(binary)
        if set(manifest["files"]) != {"D3D9.dll"} or any(
                manifest["files"]["D3D9.dll"][key] != value for key, value in record.items()):
            raise ValueError(f"Feature manifest differs from packaged DLL: {name}")
        records[name] = {**record, **audit_feature_proxy(binary.read_bytes())}
    return {"status": "pass", "variant_count": len(records), "variants": records}
