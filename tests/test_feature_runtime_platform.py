"""Load-time compatibility of every independently installable feature proxy.

This checks the XP SP3 import contract, not execution on that operating system.
The regression catches a modern static CRT re-entering the shipped DLLs.
"""
from __future__ import annotations

import json
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess

import pefile
import pytest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
NAMES = (
    "fast-only", "icon-only", "fast-icon", "zoom-only", "fast-only-zoom",
    "icon-only-zoom", "fast-icon-zoom", "credits-only", "fast-only-credits",
    "icon-only-credits", "fast-icon-credits", "zoom-only-credits",
    "fast-only-zoom-credits", "icon-only-zoom-credits", "fast-icon-zoom-credits",
)


@pytest.fixture(scope="module")
def platform_tool():
    spec = importlib.util.spec_from_file_location("feature_platform", ROOT / "tools/runtime_platform.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("name", NAMES)
def test_feature_proxy_can_load_with_the_xp_sp3_api_surface(name):
    manifest = json.loads((RUNTIME / f"payload-manifest-{name}.json").read_text())
    assert set(manifest["files"]) == {"D3D9.dll"}
    with pefile.PE(str(RUNTIME / f"D3D9-{name}.dll")) as pe:
        assert pe.FILE_HEADER.Machine == 0x14C
        assert pe.OPTIONAL_HEADER.Magic == 0x10B
        assert (pe.OPTIONAL_HEADER.MajorSubsystemVersion,
                pe.OPTIONAL_HEADER.MinorSubsystemVersion) == (5, 1)
        imports = {item.name.decode("ascii") for dll in pe.DIRECTORY_ENTRY_IMPORT
                   for item in dll.imports if item.name}
        assert not {"InitOnceExecuteOnce", "InitializeCriticalSectionEx", "FlsAlloc",
                    "FlsFree", "FlsGetValue", "FlsSetValue", "wcscpy_s", "wcscat_s"} & imports
        assert [(item.name, item.ordinal) for item in pe.DIRECTORY_ENTRY_EXPORT.symbols] == [
            (b"Direct3DCreate9", 16)]


def test_release_platform_audit_checks_all_independent_features(platform_tool):
    result = platform_tool.audit_packaged_features(RUNTIME)
    assert result["status"] == "pass"
    assert set(result["variants"]) == set(NAMES)


@pytest.mark.parametrize("mutation", ["subsystem", "modern_import", "ordinal_import", "unparsed_delay_import", "export"])
def test_release_platform_audit_refuses_incompatible_runtime(platform_tool, mutation):
    data = bytearray((RUNTIME / "D3D9-fast-only.dll").read_bytes())
    with pefile.PE(data=bytes(data)) as pe:
        if mutation == "subsystem":
            offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("MajorSubsystemVersion")
            struct.pack_into("<HH", data, offset, 6, 0)
        elif mutation == "modern_import":
            entry = next(item for dll in pe.DIRECTORY_ENTRY_IMPORT for item in dll.imports
                         if item.name == b"InitializeCriticalSection")
            offset = pe.get_offset_from_rva(entry.hint_name_table_rva) + 2
            data[offset:offset + len(entry.name) + 1] = b"InitOnceExecuteOnce\0".ljust(len(entry.name) + 1, b"\0")
        elif mutation == "ordinal_import":
            entry = pe.DIRECTORY_ENTRY_IMPORT[0].imports[0]
            struct.pack_into("<I", data, entry.thunk_offset, 0x80000001)
        elif mutation == "unparsed_delay_import":
            offset = pe.OPTIONAL_HEADER.DATA_DIRECTORY[13].get_field_absolute_offset("VirtualAddress")
            struct.pack_into("<II", data, offset, pe.OPTIONAL_HEADER.SizeOfImage - 16, 32)
        else:
            offset = pe.DIRECTORY_ENTRY_EXPORT.symbols[0].name_offset
            data[offset:offset + 14] = b"MissingFactory"
    with pytest.raises(ValueError):
        platform_tool.audit_feature_proxy(bytes(data))


@pytest.fixture(scope="module")
def factory_smoke(tmp_path_factory):
    compiler = os.environ.get("MTW_CC") or shutil.which("i686-w64-mingw32-gcc.exe")
    if not compiler:
        pytest.skip("MTW_CC must name the i686 compiler for the Windows factory smoke")
    folder = tmp_path_factory.mktemp("feature-factory")
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ["PATH"])
    binary = folder / "factory_smoke.exe"
    built = subprocess.run([compiler, "-std=c99", "-D_WIN32_WINNT=0x0501", "-municode", "-static",
                            "-static-libgcc", str(ROOT / "tests/native_feature_proxy_smoke.c"),
                            "-Wl,--subsystem,console:5.1", "-o", str(binary)],
                           env=env, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return binary, compiler, env, folder


@pytest.mark.parametrize("name", NAMES)
def test_x86_factory_matches_system_and_declines_unsupported_host(factory_smoke, name):
    binary, _, env, _ = factory_smoke
    result = subprocess.run([str(binary), str(RUNTIME / f"D3D9-{name}.dll")],
                            env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)


@pytest.mark.parametrize("case,expected", [("missing_export", 11), ("null_factory", 20), ("wrong_arch", 10)])
def test_x86_factory_smoke_rejects_broken_forwarding(factory_smoke, case, expected):
    binary, compiler, env, folder = factory_smoke
    target = folder / (case + ".dll")
    if case == "wrong_arch":
        data = bytearray((RUNTIME / "D3D9-fast-only.dll").read_bytes())
        with pefile.PE(data=bytes(data)) as pe:
            struct.pack_into("<H", data, pe.FILE_HEADER.get_field_absolute_offset("Machine"), 0x8664)
        target.write_bytes(data)
    else:
        source = folder / (case + ".c")
        source.write_text("__declspec(dllexport) int placeholder(void) { return 1; }" if case == "missing_export" else
                          "#include <windows.h>\nvoid *WINAPI Direct3DCreate9(unsigned int sdk) {(void)sdk; return 0;}")
        arguments = [compiler, "-shared", "-static", "-static-libgcc", str(source), "-o", str(target)]
        if case == "null_factory":
            arguments.append(str(ROOT / "vendor/r185/source/combined_proxy.def"))
        built = subprocess.run(arguments, env=env, capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(binary), str(target)], env=env, capture_output=True, timeout=20)
    assert result.returncode == expected, (case, result.returncode, result.stdout, result.stderr)
