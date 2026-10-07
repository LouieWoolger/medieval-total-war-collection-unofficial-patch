from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
ASSET_NAMES = (
    "medieval.ico", "welcome-finish.bmp", "compatibility.bmp", "campaign-scrolling.bmp",
    "sprite-clipping.bmp",
    "discord-badge.bmp", "discord-badge-hover.bmp", "kofi-badge.bmp",
    "kofi-badge-hover.bmp",
)
ENGINE_NAMES = ("medieval_fix_patcher.exe",)
STAGES = ("project_contracts", "native_guard", "sprite_exe", "lifecycle", "compiled_installer", "legacy_migration",
          "legacy_cpp", "legacy_v2", "historical_state", "release_hygiene")

# Deliberately bounded XP SP3 import surface for the installer helper. New imports
# require review; dynamic newer capabilities retain their guarded C fallbacks.
# This static audit does not establish execution or game compatibility on XP.
XP_HELPER_IMPORTS = {
    "advapi32.dll": set("""ConvertSidToStringSidW GetTokenInformation OpenProcessToken
        RegCloseKey RegCreateKeyExW RegDeleteKeyW RegDeleteValueW RegEnumValueW
        RegFlushKey RegOpenKeyExW RegQueryInfoKeyW RegSetValueExW""".split()),
    "kernel32.dll": set("""CloseHandle CreateDirectoryW CreateFileW CreateMutexW
        CreateToolhelp32Snapshot DeleteCriticalSection EnterCriticalSection FindClose
        FindFirstFileW FindNextFileW FlushFileBuffers FreeLibrary GetCPInfo GetCurrentProcess
        GetCurrentProcessId GetDiskFreeSpaceExW GetDriveTypeW GetEnvironmentVariableW
        GetFileAttributesW GetFileInformationByHandle GetFullPathNameW GetLastError
        GetLongPathNameW GetModuleHandleA GetModuleHandleW GetProcAddress GetStdHandle
        GetSystemDirectoryW GetSystemTime GetSystemTimeAsFileTime GetTickCount GetVersionExW
        InitializeCriticalSection LCMapStringW LeaveCriticalSection LoadLibraryW LocalFree
        MultiByteToWideChar OpenProcess Process32FirstW Process32NextW QueryDosDeviceW
        ReadFile ReleaseMutex SetDllDirectoryW SetErrorMode SetFilePointerEx SetLastError
        SetUnhandledExceptionFilter Sleep TerminateProcess TlsGetValue VirtualProtect
        VirtualQuery WaitForSingleObject WideCharToMultiByte WriteFile""".split()),
    "msvcrt.dll": set("""__p__iob __lc_codepage __p___mb_cur_max __p___winitenv
        __p__commode __p__fmode __set_app_type __setusermatherr __wgetmainargs _amsg_exit
        _cexit _errno _initterm _snwprintf _strnicmp _wcsicmp atexit abort calloc exit
        fflush fprintf fputc free localeconv malloc memchr memcmp memcpy setvbuf signal
        strchr strcmp strcpy strerror strlen strncmp strpbrk strstr vfprintf wcscat
        wcschr wcscmp wcscpy wcslen wcsncmp wcsrchr""".split()),
    "rpcrt4.dll": {"UuidCreate"},
    "version.dll": {"GetFileVersionInfoSizeW", "GetFileVersionInfoW", "VerQueryValueW"},
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def file_record(path: Path) -> dict:
    return {"sha256": sha256(path), "length": path.stat().st_size}


def git(*arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True, check=False)


def source_snapshot() -> dict:
    revision = git("rev-parse", "HEAD")
    status = git("status", "--porcelain=v1", "-z")
    listing = git("ls-files", "--cached", "--others", "--exclude-standard", "-z")
    if any(result.returncode for result in (revision, status, listing)):
        raise ValueError("Source provenance requires a readable Git working tree.")
    names = {name.decode("utf-8") for name in listing.stdout.split(b"\0") if name}
    names.add("include/product.nsh")
    files, missing = {}, []
    for name in sorted(names):
        path = ROOT / name
        if path.resolve().is_relative_to(ROOT) and path.is_file():
            files[name.replace("\\", "/")] = file_record(path)
        elif not path.exists():
            missing.append(name)
        else:
            raise ValueError(f"Unsupported source entry: {name}")
    canonical = json.dumps(files, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {
        "revision": revision.stdout.decode().strip(),
        "working_tree_dirty": bool(status.stdout),
        "working_tree_status_sha256": hashlib.sha256(status.stdout).hexdigest().upper(),
        "aggregate_sha256": hashlib.sha256(canonical).hexdigest().upper(),
        "files": files,
        "deleted_files": missing,
    }


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def not_run(reason: str) -> dict:
    return {
        "status": "not-run", "tests": None, "passed": None, "skipped": None,
        "failures": None, "errors": None, "report": None, "report_sha256": None,
        "reason": reason,
    }


def junit_result(path: Path) -> dict:
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    counts = {"tests": len(cases), "passed": 0, "skipped": 0, "failures": 0, "errors": 0}
    for case in cases:
        kind = next((key for key, tag in (("errors", "error"), ("failures", "failure"),
                                         ("skipped", "skipped")) if case.find(tag) is not None), "passed")
        counts[kind] += 1
    state = "fail" if counts["errors"] or counts["failures"] else (
        "incomplete" if not counts["passed"] else "pass-with-skips" if counts["skipped"] else "pass")
    result = {"status": state, **counts, "report": path.name, "report_sha256": sha256(path)}
    for property_ in root.iter("property"):
        name = property_.get("name", "")
        if name.startswith("c_") and name.endswith("_assertions"):
            result.setdefault("c_fixture_assertions", {})[name] = int(property_.get("value"))
        elif name == "c_python_hash_vectors":
            result[name] = int(property_.get("value"))
    return result


def r185_result(path: Path | None, variant_hashes: dict[str, str]) -> dict:
    if path is None:
        return {"status": "not-run", "reason": "No R185 rebuild evidence was supplied."}
    evidence = load_json(path)
    tests = {name.removesuffix("_exit"): value for name, value in evidence.items()
             if name.endswith("_tests_exit")}
    fields = {
        "D3D9-scroll-sprite-off.dll": "scroll_sprite_off_sha256",
    }
    variants = {name: {"sha256": evidence.get(field),
                       "matches_packaged_runtime": evidence.get(field, "").upper() == variant_hashes[name].upper()}
                for name, field in fields.items()}
    passed = bool(tests) and all(value == 0 for value in tests.values())
    smoke = evidence.get("smoke_loader_exit") == 0
    return {
        "status": "pass" if all(item["matches_packaged_runtime"] for item in variants.values()) and passed and smoke else "fail",
        "report": path.name, "report_sha256": sha256(path),
        "runtime_sha256": evidence.get("scroll_sprite_off_sha256"),
        "matches_packaged_runtime": variants["D3D9-scroll-sprite-off.dll"]["matches_packaged_runtime"], "native_component_tests": tests,
        "runtime_variants": variants,
        "native_component_test_count": len(tests), "smoke_loader_exit": evidence.get("smoke_loader_exit"),
    }


def game_test_result(validation: dict) -> dict:
    """A basic build or a skipped game suite cannot establish game validation."""
    states = [validation[name]["status"] for name in ("sprite_exe", "lifecycle", "compiled_installer")]
    for name in ("legacy_migration", "legacy_cpp", "legacy_v2", "historical_state"):
        optional = validation.get(name, {"status": "not-run"})["status"]
        if optional != "not-run":
            states.append(optional)
    if all(state == "not-run" for state in states):
        status, reason = "not-run", "Run test.ps1 with a supported game executable."
    elif "fail" in states:
        status, reason = "fail", "A game test suite failed."
    elif any(state != "pass" for state in states) or not validation["lifecycle_fault_tests_enabled"]:
        status, reason = "incomplete", "Required game tests were missing, skipped, or ran without fault tests."
    else:
        status, reason = "pass", "Exact Sprite EXE, lifecycle and packaged installer suites passed with fault tests enabled."
    return {"status": status, "reason": reason}


def ensure_path_neutral(value: object) -> None:
    if isinstance(value, str):
        if re.search(r'(?:^|[\s"])[A-Za-z]:[\\/]|\\\\[^\\]', value):
            raise ValueError("Release metadata contains an absolute filesystem path.")
        if str(ROOT).lower() in value.lower() or str(Path.home()).lower() in value.lower():
            raise ValueError("Release metadata contains a private development path.")
    elif isinstance(value, dict):
        for key, item in value.items():
            ensure_path_neutral(key)
            ensure_path_neutral(item)
    elif isinstance(value, list):
        for item in value:
            ensure_path_neutral(item)


def audit_native_helper(path: Path) -> dict:
    import pefile

    product = load_json(ROOT / "config/product.json")
    with pefile.PE(str(path)) as pe:
        if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
            raise ValueError("Native helper must be PE32/i386.")
        if pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].VirtualAddress:
            raise ValueError("Native helper contains a CLR runtime header.")
        subsystem_version = (pe.OPTIONAL_HEADER.MajorSubsystemVersion, pe.OPTIONAL_HEADER.MinorSubsystemVersion)
        if subsystem_version != (5, 1):
            raise ValueError("Native helper must target subsystem version 5.1.")
        import_tables = {}
        for label, directory_index, attribute in (("direct", 1, "DIRECTORY_ENTRY_IMPORT"),
                                                  ("delay", 13, "DIRECTORY_ENTRY_DELAY_IMPORT")):
            entries = getattr(pe, attribute, [])
            if pe.OPTIONAL_HEADER.DATA_DIRECTORY[directory_index].VirtualAddress and not entries:
                raise ValueError(f"Unparsed native helper {label} import directory.")
            named = {}
            for entry in entries:
                dll = entry.dll.decode("ascii").lower()
                if dll not in XP_HELPER_IMPORTS:
                    raise ValueError(f"Unexpected native helper runtime dependency: {dll}")
                for item in entry.imports:
                    if not item.name:
                        raise ValueError("Ordinal helper imports cannot satisfy the platform audit.")
                    name = item.name.decode("ascii")
                    if name not in XP_HELPER_IMPORTS[dll]:
                        raise ValueError(f"Unreviewed XP SP3 helper import: {dll}!{name}")
                    named.setdefault(dll, set()).add(name)
            import_tables[label] = {dll: sorted(names) for dll, names in sorted(named.items())}
        imports = sorted(set(import_tables["direct"]) | set(import_tables["delay"]))
        if not imports:
            raise ValueError("Native helper has no readable imports.")
        versions = {}
        for group in getattr(pe, "FileInfo", []):
            for entry in group:
                if entry.Key == b"StringFileInfo":
                    for table in entry.StringTable:
                        versions.update({k.decode(): v.decode() for k, v in table.entries.items()})
        for key in ("FileVersion", "ProductVersion"):
            if versions.get(key) != product["version"]:
                raise ValueError("Native helper version differs from product configuration.")
        if versions.get("ProductName") != product["product_name"]:
            raise ValueError("Native helper branding differs from product configuration.")
        manifests = []
        for kind in getattr(pe, "DIRECTORY_ENTRY_RESOURCE").entries:
            if kind.id == 24:
                for resource in kind.directory.entries:
                    for language in resource.directory.entries:
                        data = language.data.struct
                        manifests.append(pe.get_data(data.OffsetToData, data.Size).decode("utf-8"))
        if len(manifests) != 1:
            raise ValueError("Expected one native helper application manifest.")
        tree = ET.fromstring(manifests[0])
        elevation = tree.find(".//{urn:schemas-microsoft-com:asm.v3}requestedExecutionLevel")
        if elevation is None or elevation.get("level") != "asInvoker" or elevation.get("uiAccess") != "false":
            raise ValueError("Native helper must request asInvoker without uiAccess.")
        record = {"filename": path.name, **file_record(path), "machine": "i386", "format": "PE32",
                  "clr_header_absent": True, "imports": imports, "execution_level": "asInvoker",
                  "file_version": versions["FileVersion"], "product_name": versions["ProductName"],
                  "subsystem_version": list(subsystem_version),
                  "named_imports": import_tables["direct"], "delay_imports": import_tables["delay"],
                  "platform_import_policy": "XP SP3 allowlist; runtime execution unverified",
                  "external_runtime_required": False}
        ensure_path_neutral(record)
        return record


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a path-neutral manifest from actual build evidence.")
    parser.add_argument("--installer", type=Path, default=ROOT / "dist/Unofficial Medieval Total War Collection Patch.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/RELEASE_MANIFEST.json")
    parser.add_argument("--write-source-snapshot", type=Path)
    parser.add_argument("--source-snapshot", type=Path)
    parser.add_argument("--verify-source-snapshot", type=Path)
    parser.add_argument("--tools", type=Path)
    parser.add_argument("--native-build", type=Path)
    parser.add_argument("--audit-native-helper", type=Path)
    parser.add_argument("--audit", type=Path)
    parser.add_argument("--r185-build-manifest", type=Path)
    parser.add_argument("--test-result", action="append", default=[], metavar="STAGE=JUNIT_XML")
    parser.add_argument("--lifecycle-fault-tests", action="store_true")
    args = parser.parse_args()
    if args.audit_native_helper:
        write_json(args.output, audit_native_helper(args.audit_native_helper))
        print("Native PE32 helper audit passed.")
        return 0
    if args.test_result and not args.source_snapshot:
        raise ValueError("Test evidence requires the source snapshot captured before validation.")
    current = source_snapshot()
    if args.write_source_snapshot:
        write_json(args.write_source_snapshot, current)
        print("Source inputs recorded.")
        return 0
    baseline = args.verify_source_snapshot or args.source_snapshot
    if baseline and current != load_json(baseline):
        raise ValueError("Source inputs changed during the build; do not label this candidate validated.")
    if args.verify_source_snapshot:
        print("Source inputs remain unchanged.")
        return 0
    installer = args.installer.resolve()
    if not installer.is_file():
        raise ValueError("Installer input is missing.")
    product = load_json(ROOT / "config/product.json")
    scroll_sprite_off_payload = load_json(ROOT / "vendor/runtime/payload-manifest-scroll-sprite-off.json")
    payload = scroll_sprite_off_payload
    validation = {stage: not_run("No JUnit evidence was supplied.") for stage in STAGES}
    supplied = set()
    for argument in args.test_result:
        stage, separator, filename = argument.partition("=")
        if not separator or stage not in validation or stage in supplied:
            raise ValueError("Expected one STAGE=JUNIT_XML argument for each known stage.")
        supplied.add(stage)
        validation[stage] = junit_result(Path(filename))
        validation[stage]["source_aggregate_sha256"] = current["aggregate_sha256"]
        if stage in ("compiled_installer", "legacy_migration", "release_hygiene"):
            validation[stage]["installer_sha256"] = sha256(installer)
    validation["lifecycle_fault_tests_enabled"] = args.lifecycle_fault_tests
    validation["game_tests"] = game_test_result(validation)
    validation["r185_build"] = r185_result(
        args.r185_build_manifest,
        {name: variant["files"]["D3D9.dll"]["sha256"] for name, variant in (
            ("D3D9-scroll-sprite-off.dll", scroll_sprite_off_payload),
        )},
    )
    audit = load_json(args.audit) if args.audit else None
    if audit and (audit.get("result") != "pass" or audit.get("sha256") != sha256(installer)):
        raise ValueError("Installer audit is not a passing result for this candidate.")
    tools = load_json(args.tools) if args.tools else {"status": "not-recorded", "executables": [], "python_packages": {}}
    native_build = load_json(args.native_build) if args.native_build else None
    if (not native_build or native_build.get("target") != "i686-w64-mingw32"
            or native_build.get("language") != "C99"
            or native_build.get("compiler", {}).get("role") != "native-gcc"
            or not native_build.get("cplusplus_symbols_absent")):
        raise ValueError("Native C99/GCC helper build provenance and symbol audit are required.")
    required_sources = {"src/medieval_fix_patcher.c", "src/medieval_fix_patcher.rc", "src/medieval_fix_patcher.manifest"}
    required_sources.update("src/" + path.name for path in (ROOT / "src").glob("*.h"))
    if set(native_build.get("source_inputs", {})) != required_sources:
        raise ValueError("Native helper provenance must identify all and only the C backend inputs.")
    flags = native_build.get("flags", [])
    if (not {"-std=c99", "-D_WIN32_WINNT=0x0501", "-static-libgcc"}.issubset(flags)
            or any("c++" in flag for flag in flags)
            or any("stdc++" in library.get("filename", "") for library in native_build.get("static_libraries", []))):
        raise ValueError("Native helper compiler or runtime inputs violate the C build contract.")
    if not audit or audit.get("native_helper", {}).get("sha256") != native_build.get("helper", {}).get("sha256"):
        raise ValueError("Packaged native helper does not match its build provenance.")
    for name, record in native_build.get("source_inputs", {}).items():
        if current["files"].get(name) != record:
            raise ValueError("Native helper build sources differ from the release snapshot.")
    documentation = {"LICENSE.txt": ROOT / "LICENSE"}
    for name, source in documentation.items():
        if sha256(args.output.parent / name) != sha256(source):
            raise ValueError(f"Distributed documentation differs from source: {name}")
    manifest = {
        "schema": "unofficial-medieval-total-war-patch-release-v2",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "product": {"name": product["product_name"], "version": product["version"],
                    "company": product["company_name"], "component": product["component_name"],
                    "scroll_component": product["scroll_component_name"],
                    "sprite_component": product["sprite_component_name"]},
        "installer": {"filename": installer.name, **file_record(installer),
                      "signature": audit["authenticode"] if audit else "not-verified"},
        "uninstaller": {"filename": product["uninstaller_filename"], "location": "game-root",
                        "sha256": audit["uninstaller"]["embedded_sha256"],
                        "helper_sha256": audit["uninstaller"]["native_helper"]["sha256"],
                        "bundled_engine": list(ENGINE_NAMES), "requires_original_installer": False},
        "supported_executable": payload["target_executable"]["name"],
        "supported_executable_sha256": payload["target_executable"]["sha256"],
        "runtime": {"identity": payload["runtime_identity"], "dgvoodoo_version": payload["dgvoodoo_version"],
                    "files": payload["files"], "locked_settings": payload["locked_settings"]},
        "direct_scroll_executable": {"stock_sha256": "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5",
                                     "patched_sha256": "50829CD084355D81EC94D6F4489D1F60E2EF7FA92983D0EAD07D43832DEEF15B",
                                     "shipped_game_executable": False},
        "direct_sprite_executable": {"sprite_only_sha256": "72A42C3635ED808E87CF85BAF24D39601376E4B0143B109B0A63771541B662D0",
                                      "scroll_and_sprite_sha256": "982921CEFDED31C298F249742B8A001BB1A823F77FB3965E01892A34ED12E627",
                                      "sprite_delivery": "direct-exe", "shipped_game_executable": False},
        "assets": {name: file_record(ROOT / "assets" / name) for name in ASSET_NAMES},
        "documentation": {name: file_record(args.output.parent / name) for name in documentation},
        "validation": validation,
        "source": current,
        "tools": tools,
        "native_helper_build": native_build,
        "installer_audit": ({"status": "pass", "report": args.audit.name, "report_sha256": sha256(args.audit)}
                            if audit else {"status": "not-run"}),
    }
    ensure_path_neutral(manifest)
    write_json(args.output, manifest)
    print(f"Generated {args.output.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
