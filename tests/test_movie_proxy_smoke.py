"""Compile the independent PE32 movie proxy and test its fail-closed loader."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="Win32 movie proxy")


def test_movie_proxy_fails_closed_on_unsupported_executable(tmp_path: Path) -> None:
    clang = shutil.which("clang-cl.exe") or str(
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) /
        "LLVM/bin/clang-cl.exe")
    assert Path(clang).is_file()
    src = ROOT / "vendor/movie-fix"
    dll = tmp_path / "D3D9.dll"
    build = subprocess.run([
        clang, "/nologo", "/TC", "--target=i686-pc-windows-msvc",
        "/O2", "/W4", "/WX", "/LD", "/Fe" + str(dll),
        str(src / "d3d9_movie_proxy.c"), str(src / "movie_backing_fix.c"),
        "/link", "/DEF:" + str(src / "d3d9_movie_proxy.def"),
        "/MACHINE:X86", "/BREPRO", "/INCREMENTAL:NO", "bcrypt.lib", "user32.lib",
    ], capture_output=True, text=True, timeout=120)
    assert build.returncode == 0, build.stdout + build.stderr
    exe = tmp_path / "movie_proxy_smoke.exe"
    compile_loader = subprocess.run([
        clang, "/nologo", "/TC", "--target=i686-pc-windows-msvc",
        "/O2", "/W4", "/WX", "/Fe" + str(exe),
        str(ROOT / "tests/native_movie_proxy_smoke.c"),
        "/link", "/MACHINE:X86", "/SUBSYSTEM:CONSOLE", "/BREPRO", "/INCREMENTAL:NO",
    ], capture_output=True, text=True, timeout=120)
    assert compile_loader.returncode == 0, compile_loader.stdout + compile_loader.stderr
    run = subprocess.run([str(exe), str(dll)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "unsupported-exe guard and system forwarding passed" in run.stdout
