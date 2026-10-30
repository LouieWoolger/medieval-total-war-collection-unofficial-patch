"""Reproducibly build all 15 feature-only PE32 proxies for the XP SP3 API surface.

Outputs and compiler scratch are external. No Terrain implementation is linked.
The current-Windows smoke checks forwarding and an unsupported-host no-op; it
does not establish execution on XP or correctness of game-specific hooks.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import pefile

from runtime_platform import FEATURE_VARIANTS, audit_feature_proxy, file_record

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_SOURCES = (
    "vendor/r185/source/campaign_fast_moves.c",
    "vendor/r185/source/battle_icon_atlas_fix.c",
    "vendor/r185/source/credits_flash_fix.c",
    "vendor/dust-fidelity/minhook/src/buffer.c",
    "vendor/dust-fidelity/minhook/src/hook.c",
    "vendor/dust-fidelity/minhook/src/trampoline.c",
    "vendor/dust-fidelity/minhook/src/hde/hde32.c",
)


def build(output: Path, clang: Path, gcc: Path, verify_packaged: bool) -> dict:
    output = output.resolve()
    if output.is_relative_to(ROOT):
        raise ValueError("Feature runtime build output must be outside the source checkout.")
    for folder in (output, output / "objects", output / "payloads", output / "temp", output / "logs"):
        folder.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEMP=str(output / "temp"), TMP=str(output / "temp"),
               PYTHONDONTWRITEBYTECODE="1",
               PATH=str(gcc.parent) + os.pathsep + os.environ.get("PATH", ""))
    commands = []
    included_headers = set()

    def run(arguments: list, name: str) -> str:
        args = [str(value) for value in arguments]
        result = subprocess.run(args, cwd=output, env=env, capture_output=True, text=True, timeout=120)
        commands.append({"name": name, "command": args, "exit": result.returncode})
        (output / "logs" / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        for line in result.stderr.splitlines():
            match = re.match(r"^\.+ (.+)$", line)
            if match:
                header = Path(match.group(1)).resolve()
                if header.is_file():
                    included_headers.add(header)
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}): {result.stdout}{result.stderr}")
        return result.stdout.strip()

    triple = run([gcc, "-dumpmachine"], "gcc-target")
    if triple != "i686-w64-mingw32":
        raise ValueError("Feature runtime requires the i686 MinGW compiler and libraries.")
    gcc_version = run([gcc, "-dumpfullversion", "-dumpversion"], "gcc-version")
    clang_version = run([clang, "--version"], "clang-version").splitlines()[0]
    sysroot = gcc.parent.parent
    flags = ["--target=i686-w64-windows-gnu", "-std=c99", "-fms-extensions", "-fasm-blocks",
             "-D_M_IX86", "-DMTW_XP_RUNTIME=1", "-D_WIN32_WINNT=0x0501", "-DWINVER=0x0501",
             "-Os", "-Wall", "-Wextra", "-Werror", "-H"]
    common = [clang, *flags, "--sysroot=" + str(sysroot), "-B", gcc.parent,
              "-ffile-prefix-map=" + str(ROOT) + "=.", "-fmacro-prefix-map=" + str(ROOT) + "=."]
    objects = {}
    for relative in RUNTIME_SOURCES:
        path = ROOT / relative
        target = output / "objects" / (path.stem + ".o")
        vendor_flags = ["-Wno-unused-parameter"] if "/minhook/" in relative else []
        run([*common, *vendor_flags, "-c", path, "-o", target], path.stem + "-compile")
        objects[path.stem] = target
    variants = []
    for name, (fast, icon, zoom, credits) in FEATURE_VARIANTS.items():
        bridge = output / "objects" / (name + ".o")
        definitions = [f"-DMTW_ENABLE_FAST_MOVES={fast}", f"-DMTW_ENABLE_BATTLE_ICON_FIX={icon}",
                       f"-DMTW_ENABLE_ZOOM={zoom}", f"-DMTW_ENABLE_CREDITS_FLASH_FIX={credits}"]
        run([*common, *definitions, "-c", ROOT / "vendor/r185/source/campaign_fast_moves_proxy.c",
             "-o", bridge], name + "-bridge")
        link_objects = [bridge]
        if fast or zoom:
            link_objects += [objects[key] for key in ("campaign_fast_moves", "buffer", "hook", "trampoline", "hde32")]
        if icon:
            link_objects.append(objects["battle_icon_atlas_fix"])
        if credits:
            link_objects.append(objects["credits_flash_fix"])
        binary = output / "payloads" / f"D3D9-{name}.dll"
        run([clang, "--target=i686-w64-windows-gnu", "--sysroot=" + str(sysroot), "-B", gcc.parent,
             "-shared", "-static-libgcc", *link_objects, ROOT / "vendor/r185/source/combined_proxy.def",
             "-Wl,--subsystem,windows:5.1", "-Wl,--no-insert-timestamp", "-ladvapi32", "-luser32",
             "-o", binary], name + "-link")
        record = file_record(binary)
        if verify_packaged and record != file_record(ROOT / "vendor/runtime" / binary.name):
            raise ValueError(f"Feature source build differs from packaged DLL: {name}")
        variants.append({"name": name, "filename": binary.name, "terrain": False,
                         "fast": bool(fast), "icon": bool(icon), "zoom": bool(zoom), "credits": bool(credits),
                         **record, "platform": audit_feature_proxy(binary.read_bytes())})
    smoke = output / "factory_smoke.exe"
    run([gcc, "-std=c99", "-D_WIN32_WINNT=0x0501", "-municode", "-static", "-static-libgcc", "-H",
         ROOT / "tests/native_feature_proxy_smoke.c", "-Wl,--subsystem,console:5.1",
         "-Wl,--no-insert-timestamp", "-o", smoke], "factory-smoke-compile")
    for variant in variants:
        run([smoke, output / "payloads" / variant["filename"]], variant["name"] + "-smoke")
        variant["factory_smoke_exit"] = 0
    sources = set(RUNTIME_SOURCES) | {
        "tools/build-feature-payloads.py", "tools/runtime_platform.py", "tests/native_feature_proxy_smoke.c",
        "include/mtw_exe_variants.h", "vendor/r185/source/campaign_fast_moves_core.h",
        "vendor/r185/source/campaign_fast_moves_proxy.c", "vendor/r185/source/combined_proxy.def",
        "vendor/r185/source/battle_icon_atlas_fix.h", "vendor/r185/source/battle_icon_atlas_core.h",
        "vendor/r185/source/credits_flash_fix.h",
    }
    sources.update(path.relative_to(ROOT).as_posix() for path in
                   (ROOT / "vendor/dust-fidelity/minhook").rglob("*.h"))
    headers = {}
    for path in sorted(included_headers):
        if path.is_relative_to(ROOT):
            sources.add(path.relative_to(ROOT).as_posix())
        elif path.is_relative_to(sysroot):
            headers["mingw32/" + path.relative_to(sysroot).as_posix()] = file_record(path)
        elif path.is_relative_to(clang.parent.parent):
            headers["llvm/" + path.relative_to(clang.parent.parent).as_posix()] = file_record(path)
        else:
            raise ValueError(f"A compiler header is outside the selected source/toolchain: {path}")
    libraries = {}
    for name in ("dllcrt2.o", "crtbegin.o", "crtend.o", "libgcc.a", "libmingw32.a", "libmingwex.a",
                 "libmoldname.a", "libmsvcrt.a", "libkernel32.a", "libadvapi32.a", "libuser32.a", "libshell32.a"):
        path = Path(run([gcc, "-print-file-name=" + name], "library-" + name))
        if not path.is_file():
            raise ValueError(f"Missing MinGW source-build input: {name}")
        libraries[name] = file_record(path)
    linker = clang.with_name("ld.lld.exe")
    support = {}
    system_dependencies = set()
    pending_tools = [clang, linker, gcc, gcc.parent / "as.exe", gcc.parent / "ld.exe",
                     Path(run([gcc, "-print-prog-name=cc1"], "gcc-cc1"))]
    visited = set()
    while pending_tools:
        path = pending_tools.pop().resolve()
        if path in visited:
            continue
        if not path.is_file():
            raise ValueError(f"A compiler support program is missing: {path}")
        visited.add(path)
        prefix = "llvm" if path.is_relative_to(clang.parent.parent) else "mingw32"
        support[prefix + "/" + path.name] = file_record(path)
        with pefile.PE(str(path), fast_load=False) as pe:
            for table in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
                for entry in getattr(pe, table, []):
                    name = entry.dll.decode("ascii")
                    local = next((folder / name for folder in (path.parent, gcc.parent, clang.parent)
                                  if (folder / name).is_file()), None)
                    if local:
                        pending_tools.append(local)
                    else:
                        system_dependencies.add(name.lower())
    report = {"schema": "mtw-feature-runtime-build-v1", "status": "pass", "target": triple,
              "compiler": {"filename": clang.name, "version": clang_version, **file_record(clang)},
              "gcc": {"filename": gcc.name, "version": gcc_version, **file_record(gcc)},
              "linker": {"filename": linker.name, **file_record(linker)}, "libraries": libraries,
              "compiler_support": support, "compiler_system_dependencies": sorted(system_dependencies),
              "header_inputs": headers,
              "compile_flags": flags, "link_flags": ["-shared", "-static-libgcc", "--subsystem,windows:5.1", "--no-insert-timestamp"],
              "source_inputs": {name: file_record(ROOT / name) for name in sorted(sources)},
              "factory_smoke": {**file_record(smoke), "operating_system": "current Windows host",
                                "xp_execution": "not-run", "gameplay": "not-run"}, "variants": variants}
    (output / "feature-build-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clang", type=Path, default=Path(shutil.which("clang.exe") or r"C:\Program Files\LLVM\bin\clang.exe"))
    parser.add_argument("--gcc", type=Path, default=Path(os.environ.get("MTW_CC") or shutil.which("i686-w64-mingw32-gcc.exe") or r"C:\msys64\mingw32\bin\i686-w64-mingw32-gcc.exe"))
    parser.add_argument("--verify-packaged", action="store_true")
    args = parser.parse_args()
    report = build(args.output, args.clang.resolve(), args.gcc.resolve(), args.verify_packaged)
    print(f"Built and import-audited {len(report['variants'])} PE32 feature proxies; current-Windows factory smoke passed. XP execution not run.")


if __name__ == "__main__":
    main()
