"""C guard acceptance on disposable external fixtures, in both capability routes."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import pefile
import pytest

from test_lifecycle_native import directory_identity

ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(os.name != "nt", reason="native Win32 filesystem fixtures")


@pytest.fixture(scope="module")
def platform_exe(tmp_path_factory):
    directory = tmp_path_factory.mktemp("c-platform-build")
    cc = os.environ.get("MTW_CC") or shutil.which("i686-w64-mingw32-gcc.exe")
    assert cc, "Set MTW_CC to an i686 MinGW C99 compiler"
    triple = subprocess.check_output([cc, "-dumpmachine"], text=True).strip()
    assert triple == "i686-w64-mingw32"
    version = subprocess.check_output([cc, "-dumpfullversion", "-dumpversion"], text=True).strip()
    bindir = Path(cc).resolve().parent
    prefix = bindir.parent / "libexec/gcc" / triple / version
    output = directory / "native_platform.exe"
    args = [cc, "-B", str(prefix) + os.sep, "-std=c99", "-D_WIN32_WINNT=0x0501", "-Os",
            "-Wall", "-Wextra", "-Werror", "-municode", "-static", "-static-libgcc",
            "-Wl,--major-subsystem-version,5,--minor-subsystem-version,1",
            "-I", str(ROOT / "src"), str(ROOT / "tests/native_platform.c"), "-o", str(output)]
    env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ["PATH"])
    built = subprocess.run(args, capture_output=True, text=True, timeout=120, env=env)
    (directory / "compile.json").write_text(json.dumps({"command": args, "exit": built.returncode,
        "stdout": built.stdout, "stderr": built.stderr}, indent=2), encoding="utf-8")
    assert built.returncode == 0, built.stdout + built.stderr
    assert not built.stderr
    nm = bindir / "i686-w64-mingw32-nm.exe"
    symbols = subprocess.check_output([str(nm), "-C", str(output)], text=True)
    (directory / "symbols.txt").write_text(symbols, encoding="utf-8")
    assert not re.search(r"__cxa_|__gxx_|_Unwind_|std::|operator new|operator delete", symbols)
    return output


def run(exe, args, directory):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                            encoding="utf-8", errors="strict", timeout=90)
    (directory / "execution.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert not result.stderr
    return result.stdout


@pytest.mark.parametrize("route", ["modern", "legacy"])
def test_c_platform_guards_and_failure_contracts(platform_exe, tmp_path, route, record_testsuite_property):
    """Catch unsafe path-following, unbound mutation, and discarded recovery evidence."""
    import _winapi

    evidence = tmp_path / "evidence"
    evidence.mkdir()
    outside = evidence / "outside"
    (outside / "nested").mkdir(parents=True)
    (outside / "outside.bin").write_bytes(b"untouched")
    junction = evidence / "junction"
    _winapi.CreateJunction(str(outside), str(junction))
    try:
        output = run(platform_exe, ["--suite", tmp_path / "C Platform é Ω", evidence, route], tmp_path)
        summary = re.search(r"RESULT passed=(\d+) failed=(\d+)", output)
        assert summary and int(summary[1]) >= 90 and summary[2] == "0", output
        record_testsuite_property("c_platform_" + route + "_assertions", int(summary[1]))
        assert (outside / "outside.bin").read_bytes() == b"untouched"
        assert not (outside / "new").exists()
    finally:
        junction.rmdir()


@pytest.mark.parametrize("route", ["modern", "legacy"])
def test_c_platform_identity_and_key_compatibility(platform_exe, tmp_path, route, record_testsuite_property):
    """A C upgrade must retain the C++ invariant-lower UTF8 key and directory ID."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LCMapStringEx.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.LPCWSTR,
        ctypes.c_int, wintypes.LPWSTR, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t]
    kernel.LCMapStringEx.restype = ctypes.c_int
    for name in ("Game", "Gàme Σ İ ı K", "日本 U0001f600"):
        root = tmp_path / name
        root.mkdir()
        path = str(root)
        n = len(path.encode("utf-16-le")) // 2
        count = kernel.LCMapStringEx("", 0x100, path, n, None, 0, None, None, 0)
        assert count
        lower = ctypes.create_unicode_buffer(count + 1)
        assert kernel.LCMapStringEx("", 0x100, path, n, lower, count, None, None, 0) == count
        expected = hashlib.sha256(lower.value.encode("utf-8")).hexdigest().upper()
        output = run(platform_exe, ["--identity", path, route], tmp_path).splitlines()
        assert output == [directory_identity(root), expected, path]
        # Python full case folding can expand U+0130, which is a different name.
        # Only ASCII casing is changed here; Unicode ordinal rules are separate.
        mixed = "".join(char.swapcase() if ord(char) < 128 else char for char in path)
        alternate = run(platform_exe, ["--identity", mixed, route], tmp_path).splitlines()
        assert alternate[:2] == output[:2]
    kernel.GetShortPathNameW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    kernel.GetShortPathNameW.restype = wintypes.DWORD
    short = ctypes.create_unicode_buffer(32768)
    # The bulk fixture volume may have 8.3 name creation disabled. An existing
    # system directory may supply a read-only alias probe; no files are created
    # or changed there, and every output still stays in tmp_path.
    alias_found = False
    for candidate in (root, Path(os.environ["ProgramFiles"])):
        assert kernel.GetShortPathNameW(str(candidate), short, len(short))
        if short.value.lower() == str(candidate).lower():
            continue
        result = subprocess.run([str(platform_exe), "--identity", short.value, route], capture_output=True, text=True)
        (tmp_path / "short-path.json").write_text(json.dumps({"long": str(candidate), "short": short.value,
            "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr}, indent=2), encoding="utf-8")
        assert result.returncode == 1 and "unsafe_path" in result.stdout
        alias_found = True
        break
    record_testsuite_property("short_path_alias_exercised_" + route, alias_found)


@pytest.mark.parametrize("route", ["modern", "legacy"])
@pytest.mark.parametrize("extended", [False, True])
def test_c_platform_process_identity_is_conservative(platform_exe, tmp_path, route, extended):
    """Same executable names are distinguished by path; unknown identity is refused."""
    first = tmp_path / "Game é" / "Medieval_TW.exe"
    other = tmp_path / "Game Ω" / "Medieval_TW.exe"
    first.parent.mkdir()
    other.parent.mkdir()
    shutil.copy2(platform_exe, first)
    shutil.copy2(platform_exe, other)
    launch = "\\\\?\\" + str(first) if extended else str(first)
    child = subprocess.Popen([launch, "--wait"], creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        output = run(platform_exe, ["--process", child.pid, first, other, route], tmp_path)
        assert "RESULT passed=6 failed=0" in output
    finally:
        child.terminate()
        child.wait(timeout=10)


@pytest.mark.parametrize("route", ["modern", "legacy"])
def test_c_platform_symbolic_links_do_not_redirect(platform_exe, tmp_path, route):
    """Both file and directory symlinks must be rejected before mutation."""
    root = tmp_path / "root"
    outside = tmp_path / "outside"
    root.mkdir()
    outside.mkdir()
    source = outside / "outside.bin"
    source.write_bytes(b"untouched")
    try:
        (root / "file-link").symlink_to(source)
        (root / "directory-link").symlink_to(outside, target_is_directory=True)
    except OSError as exc:
        if exc.winerror == 1314:
            pytest.skip("symlink creation requires Developer Mode or symlink privilege")
        raise
    output = run(platform_exe, ["--links", root, route], tmp_path)
    assert "RESULT passed=4 failed=0" in output
    assert source.read_bytes() == b"untouched"


@pytest.mark.parametrize("route", ["modern", "legacy"])
@pytest.mark.parametrize("substitution", ["foreign", "missing"])
def test_c_owned_previous_slot_substitution_is_preserved(platform_exe, tmp_path, route, substitution):
    """A fresh previous-slot record cannot authorize deletion of substituted data."""
    root = tmp_path / "previous-substitution"
    root.mkdir()
    output = run(platform_exe, ["--previous-substitute", root, substitution, route], tmp_path)
    assert "RESULT passed=7 failed=0" in output
    assert (root / "journal.json").read_bytes() == b'{"revision":1}'
    assert (root / "retained-original-copy").read_bytes() == b'{"revision":1}'
    previous = root / "journal.json.mtw-previous"
    if substitution == "foreign":
        assert previous.read_bytes() == b"foreign"
    else:
        assert not previous.exists()


def test_c_platform_binary_imports(platform_exe):
    """Prevent accidental direct imports of APIs unavailable on the declared XP target."""
    with pefile.PE(str(platform_exe)) as pe:
        assert pe.FILE_HEADER.Machine == 0x14C
        assert (pe.OPTIONAL_HEADER.MajorSubsystemVersion, pe.OPTIONAL_HEADER.MinorSubsystemVersion) == (5, 1)
        assert not pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].VirtualAddress
        imports = {entry.dll.decode().lower(): [item.name.decode() for item in entry.imports if item.name]
                   for entry in pe.DIRECTORY_ENTRY_IMPORT}
        assert set(imports) <= {"kernel32.dll", "msvcrt.dll"}
        names = {name for values in imports.values() for name in values}
        assert not names & {"GetFinalPathNameByHandleW", "SetFileInformationByHandle", "CompareStringOrdinal",
            "LCMapStringEx", "QueryFullProcessImageNameW", "GetTickCount64", "InitializeCriticalSectionEx"}
        (platform_exe.parent / "imports.json").write_text(json.dumps(imports, indent=2), encoding="utf-8")


@pytest.mark.parametrize("name,key", [
    ("GOG Game", "07fb489b2d0526ba9a1382d90acde605"),
    ("gog game", "07fb489b2d0526ba9a1382d90acde605"),
    ("École", "16ea6c9c94f3523a014b5cfbdffeb91b"),
    ("école", "16ea6c9c94f3523a014b5cfbdffeb91b"),
    ("Straße", "042cbd244621ac519db7bed3afd4d1ed"),
    ("STRAẞE", "a6ed3aa19633578e677d381901acd7d1"),
    ("İıI", "918fa06ba0627b5a21392f0b871b98d7"),
    ("Σίσυφος", "a2de0faaf714e246ae7c5e2e26e40d4c"),
    ("中世紀", "92f7fa84b51f4b1a30c63797bb35fdb5"),
    ("\U00010400\U00010428", "43f55d59950b381901cf32734585336b"),
    ("Café", "f248ab90093a3bdb13a2e6d62b1641a2"),
    ("Cafe\u0301", "fe158e09d0d95c36e946c2d23f6521f8"),
    ("Kelvin", "fc23b0466e7e742265eea088b031d344"),
    ("Kelvin", "94b6f22cbf831f607509363c35431d51"),
])
def test_c_key_matches_frozen_cpp_golden_vectors(platform_exe, tmp_path, name, key):
    """Golden values came from frozen C++ source and independent Python SHA256."""
    result = run(platform_exe, ["--key", "F:\\Identity fixtures\\" + name], tmp_path)
    assert result.strip() == "UnofficialMedievalPatch-" + key
