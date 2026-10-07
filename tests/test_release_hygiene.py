from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import pefile


ROOT = Path(__file__).resolve().parents[1]
DIST = Path(os.environ.get("MTW_RELEASE_DIRECTORY", str(ROOT / "dist")))
INSTALLER = DIST / "Unofficial Medieval Total War Collection Patch.exe"
LEGACY_UNRELEASED_INSTALLER = DIST / "Unofficial Medieval Total War Patch.exe"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def test_public_root_preserves_only_the_existing_community_documents() -> None:
    assert (ROOT / "README.md").is_file()
    assert (ROOT / "LICENSE").is_file()
    for relative in (
        "THIRD_PARTY_NOTICES.md",
        "CHANGELOG.md",
        "REFERENCE_INSTALLER_AUDIT.md",
        "FINAL_INSTALLER_VERIFICATION.md",
        "RESUME_STATE.md",
        "NEXT_ACTIONS.md",
    ):
        assert not (ROOT / relative).exists(), relative
    # Ignored local notes may remain; they must not become public source inputs.
    public_docs = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", "docs"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    )
    assert not public_docs.stdout.strip()


def test_dist_does_not_retain_obsolete_files() -> None:
    assert not LEGACY_UNRELEASED_INSTALLER.exists()
    assert not (DIST / "SOURCE_ATTRIBUTION.md").exists()


def test_build_and_test_pipeline_contains_every_release_gate() -> None:
    text = "\n".join((ROOT / name).read_text(encoding="utf-8") for name in
                     ("build.ps1", "test.ps1", "tools/build-support.ps1"))
    for required in (
        "generate-product-nsh.ps1",
        "build_scaffold.ps1",
        "pytest",
        "makensis",
        "test_compiled_installer.py",
        "build-release-manifest.py",
        "audit-installer.ps1",
        "SHA256SUMS.txt",
    ):
        assert required in text
    assert "build-assets" not in text
    assert "git push" not in text.lower()
    assert "gh release" not in text.lower()


def test_release_manifest_and_checksums_match_dist() -> None:
    manifest_path = DIST / "RELEASE_MANIFEST.json"
    checksums_path = DIST / "SHA256SUMS.txt"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["schema"] == "unofficial-medieval-total-war-patch-release-v2"
    product = json.loads((ROOT / "config" / "product.json").read_text(encoding="utf-8"))
    assert manifest["product"]["version"] == product["version"] == "1.0.0"
    assert manifest["installer"]["filename"] == INSTALLER.name
    assert manifest["installer"]["sha256"] == sha256(INSTALLER)
    assert manifest["installer"]["length"] == INSTALLER.stat().st_size
    assert manifest["runtime"]["identity"] == "R186"
    helper = manifest["native_helper_build"]
    assert helper["language"] == "C99"
    assert helper["compiler"]["role"] == "native-gcc"
    assert helper["cplusplus_symbols_absent"] is True
    assert "-std=c99" in helper["flags"]
    assert not any("stdc++" in record["filename"] for record in helper["static_libraries"])
    assert helper["target"] == "i686-w64-mingw32"
    assert helper["helper"]["machine"] == "i386"
    assert helper["helper"]["clr_header_absent"] is True
    assert helper["helper"]["external_runtime_required"] is False
    assert manifest["uninstaller"]["bundled_engine"] == ["medieval_fix_patcher.exe"]
    audit = json.loads((DIST / "INSTALLER_AUDIT.json").read_text(encoding="utf-8-sig"))
    assert audit["native_helper"]["sha256"] == helper["helper"]["sha256"]
    assert audit["uninstaller"]["native_helper"]["sha256"] == helper["helper"]["sha256"]
    assert manifest["uninstaller"]["sha256"] == audit["uninstaller"]["embedded_sha256"]
    assert helper["helper"]["subsystem_version"] == [5, 1]
    assert helper["helper"]["named_imports"] and helper["helper"]["delay_imports"] == {}
    assert "src/medieval_fix_patcher.c" in helper["source_inputs"]
    assert not any(name.endswith(".cpp") for name in helper["source_inputs"])
    assert audit["uninstaller"]["engine_and_payload_match_source"] is True
    for name, record in helper["source_inputs"].items():
        assert record == manifest["source"]["files"][name]
    if os.environ.get("MTW_TEST_NATIVE_HELPER"):
        assert sha256(Path(os.environ["MTW_TEST_NATIVE_HELPER"])) == helper["helper"]["sha256"]
    report_directory = os.environ.get("MTW_TEST_REPORT_DIRECTORY")
    for stage in ("project_contracts", "native_guard", "lifecycle", "compiled_installer", "legacy_migration",
                  "legacy_cpp", "legacy_v2", "historical_state", "release_hygiene"):
        record = manifest["validation"][stage]
        assert record["status"] in {"pass", "pass-with-skips", "not-run", "incomplete", "fail"}
        count_fields = ("tests", "passed", "skipped", "failures", "errors")
        if record["status"] == "not-run":
            assert all(record[key] is None for key in (*count_fields, "report", "report_sha256"))
            assert record["reason"]
            continue
        assert all(type(record[key]) is int and record[key] >= 0 for key in count_fields)
        assert record["tests"] == sum(record[key] for key in count_fields[1:])
        if record["status"] in {"pass", "pass-with-skips"}:
            assert record["failures"] == record["errors"] == 0
            assert record["passed"] > 0
        if record["status"] == "pass":
            assert record["skipped"] == 0
        if record["status"] == "incomplete":
            assert record["passed"] == record["failures"] == record["errors"] == 0
        assert Path(record["report"]).name == record["report"]
        assert re.fullmatch(r"[0-9A-F]{64}", record["report_sha256"])
        if report_directory:
            assert sha256(Path(report_directory) / record["report"]) == record["report_sha256"]
    assert isinstance(manifest["source"]["working_tree_dirty"], bool)
    for relative, record in manifest["source"]["files"].items():
        assert not Path(relative).is_absolute() and ".." not in Path(relative).parts
        assert sha256(ROOT / relative) == record["sha256"]
        assert (ROOT / relative).stat().st_size == record["length"]
    assert re.fullmatch(r"[0-9A-F]{64}", manifest["source"]["aggregate_sha256"])
    assert manifest["runtime"]["files"]["D3D9.dll"]["sha256"] == "AD7E922E1F160C045325E75107E507E54807F426BFD8102A1808E969AD67CFCA"
    assert manifest["runtime"]["dgvoodoo_version"] == "2.87.5"
    assert manifest["supported_executable_sha256"] == "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
    sums = checksums_path.read_text(encoding="utf-8").splitlines()
    parsed = {line.split("  ", 1)[1]: line.split("  ", 1)[0] for line in sums if "  " in line}
    assert parsed[INSTALLER.name] == sha256(INSTALLER)
    assert parsed["RELEASE_MANIFEST.json"] == sha256(manifest_path)


def version_strings(path: Path) -> dict[str, str]:
    pe = pefile.PE(str(path), fast_load=False)
    values: dict[str, str] = {}
    for file_info in getattr(pe, "FileInfo", []):
        for entry in file_info:
            if entry.Key == b"StringFileInfo":
                for table in entry.StringTable:
                    values.update({key.decode(): value.decode() for key, value in table.entries.items()})
    return values


def test_installer_pe_metadata_is_complete() -> None:
    values = version_strings(INSTALLER)
    assert values["ProductName"] == "Unofficial Medieval: Total War Collection Patch"
    assert values["FileDescription"] == "Unofficial Medieval: Total War Collection Patch Setup"
    assert values["CompanyName"] == "Louie Woolger"
    assert values["FileVersion"] == "1.0.0"
    assert values["ProductVersion"] == "1.0.0"
    assert values["LegalCopyright"] == "Copyright 2026 Louie Woolger"


def test_embedded_archive_contains_only_declared_runtime_and_ui_material() -> None:
    seven_zip = os.environ.get("SEVENZIP_EXE")
    if not seven_zip:
        raise AssertionError("SEVENZIP_EXE is required for the embedded-content audit")
    result = subprocess.run([seven_zip, "l", "-slt", str(INSTALLER)], text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, result.stderr
    paths = [line.split(" = ", 1)[1] for line in result.stdout.splitlines() if line.startswith("Path = $PLUGINSDIR")]
    lowered = "\n".join(paths).lower()
    for required in (
        "medieval_fix_patcher.exe",
        "payload\\payload-manifest.json",
        "payload\\d3d9.dll",
        "payload\\dgvoodoo_d3d9.dll",
        "payload\\ddraw.dll",
        "payload\\d3dimm.dll",
        "payload\\dgvoodoo.conf",
        "payload-scroll-off\\payload-manifest.json",
        "payload-scroll-off\\d3d9.dll",
        "payload-scroll-off\\dgvoodoo_d3d9.dll",
        "payload-scroll-off\\ddraw.dll",
        "payload-scroll-off\\d3dimm.dll",
        "payload-scroll-off\\dgvoodoo.conf",
        "compatibility.bmp",
    ):
        assert required in lowered
    for forbidden in ("medieval_tw.exe", "medieval.cfg", "~tmp.vrp", ".pdb", ".vrp", "worklog", "capture", ".ps1", ".cs", "powershell"):
        assert forbidden not in lowered


def test_distributed_runtime_and_public_files_have_no_private_paths_or_shogun_branding() -> None:
    public_files = [
        ROOT / "installer.nsi",
        ROOT / "installer-support.nsh",
        ROOT / "installer-uninstall.nsh",
        *sorted((ROOT / "src").glob("*.ps1")),
        *sorted((ROOT / "src").glob("*.cs")),
        *sorted((ROOT / "src").glob("*.c")), *sorted((ROOT / "src").glob("*.cpp")),
        *sorted((ROOT / "src").glob("*.h")),
        *sorted((ROOT / "src").glob("*.rc")),
        *sorted((ROOT / "src").glob("*.manifest")),
        ROOT / "config" / "product.json",
        ROOT / "README.md",
        ROOT / "build.ps1",
        ROOT / "test.ps1",
        ROOT / "tools" / "audit-installer.ps1",
        ROOT / "tools" / "build-release-manifest.py",
        DIST / "RELEASE_MANIFEST.json",
    ]
    combined = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in public_files).lower()
    private_root = str(ROOT).lower()
    private_home = str(Path.home()).lower()
    for forbidden in (private_root, private_home, "shogunm.exe", "total war shogun"):
        assert forbidden not in combined

    binary = INSTALLER.read_bytes()
    for forbidden in (
        str(ROOT).encode(),
        str(ROOT).encode("utf-16le"),
        str(Path.home()).encode(),
        str(Path.home()).encode("utf-16le"),
    ):
        assert forbidden not in binary


def test_public_tree_has_no_ai_tooling_attribution() -> None:
    forbidden = (
        "chat" + "gpt",
        "open" + "ai",
        "co" + "dex",
        "clau" + "de",
        "co" + "pilot",
        "language " + "model",
        "generated by " + "ai",
        "super" + "powers",
    )
    excluded = {Path(__file__).resolve(), ROOT / "README.md", ROOT / "LICENSE"}
    text_suffixes = {".c", ".cpp", ".h", ".hlsl", ".json", ".nsi", ".nsh", ".ps1", ".py", ".txt", ".rc", ".manifest"}
    for path in ROOT.rglob("*"):
        if (
            not path.is_file()
            or path.resolve() in excluded
            or ".git" in path.parts
            or "dist" in path.parts
            or "artifacts" in path.parts
            or path.suffix.lower() not in text_suffixes
        ):
            continue
        text = path.read_text(encoding="utf-8", errors="replace").lower()
        for marker in forbidden:
            assert marker not in text, f"{marker}: {path.relative_to(ROOT)}"


def test_no_remote_publish_or_install_time_download_commands() -> None:
    source_files = [
        ROOT / "installer.nsi",
        ROOT / "installer-support.nsh",
        ROOT / "installer-uninstall.nsh",
        *sorted((ROOT / "src").glob("*.ps1")),
        *sorted((ROOT / "src").glob("*.cs")),
        *sorted((ROOT / "src").glob("*.c")), *sorted((ROOT / "src").glob("*.cpp")),
        *sorted((ROOT / "src").glob("*.h")),
        *sorted((ROOT / "src").glob("*.rc")),
        *sorted((ROOT / "src").glob("*.manifest")),
        ROOT / "build.ps1",
        ROOT / "test.ps1",
        *sorted((ROOT / "tools").glob("*.ps1")),
        *sorted((ROOT / "tools").glob("*.py")),
    ]
    combined = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in source_files).lower()
    for forbidden in (
        "git push",
        "gh repo create",
        "gh release",
        "invoke-webrequest",
        "downloadfile",
        "start-bitstransfer",
        "nsisdl::",
        "inetc::",
    ):
        assert forbidden not in combined


def test_setup_and_uninstaller_embed_exact_selfcontained_native_helper(tmp_path: Path) -> None:
    import importlib.util

    helper_path = os.environ.get("MTW_TEST_NATIVE_HELPER")
    assert helper_path, "MTW_TEST_NATIVE_HELPER must identify the compiled input"
    seven_zip = os.environ.get("SEVENZIP_EXE")
    assert seven_zip, "SEVENZIP_EXE is required"
    expected = sha256(Path(helper_path))
    definition = importlib.util.spec_from_file_location("release_manifest", ROOT / "tools/build-release-manifest.py")
    module = importlib.util.module_from_spec(definition)
    definition.loader.exec_module(module)
    setup_root = tmp_path / "setup"
    subprocess.run([seven_zip, "x", str(INSTALLER), "-o" + str(setup_root), "-y"],
                   capture_output=True, text=True, check=True, timeout=60)
    plugins = setup_root / "$PLUGINSDIR"
    uninstaller = plugins / "Uninstall Unofficial Medieval Patch.exe"
    removal_root = tmp_path / "removal"
    subprocess.run([seven_zip, "x", str(uninstaller), "-o" + str(removal_root), "-y"],
                   capture_output=True, text=True, check=True, timeout=60)
    for directory in (plugins, removal_root / "$PLUGINSDIR"):
        native = directory / "medieval_fix_patcher.exe"
        assert sha256(native) == expected
        report = module.audit_native_helper(native)
        assert report["execution_level"] == "asInvoker"
        assert report["file_version"] == "1.0.0"
        assert report["clr_header_absent"] and not report["external_runtime_required"]
        assert report["subsystem_version"] == [5, 1]
        assert report["named_imports"] and report["delay_imports"] == {}
        assert len(list(directory.rglob("medieval_fix_patcher.exe"))) == 1
        assert not list(directory.rglob("*.ps1"))
        assert not list(directory.rglob("*.cs"))
        assert sha256(directory / "MinGW-w64-runtime.txt") == sha256(ROOT / "licenses/MinGW-w64-runtime.txt")


def test_native_helper_audit_rejects_wrong_machine_clr_import_and_elevation(tmp_path: Path) -> None:
    import importlib.util
    import struct
    import pytest

    helper = Path(os.environ["MTW_TEST_NATIVE_HELPER"])
    definition = importlib.util.spec_from_file_location("release_manifest_negative", ROOT / "tools/build-release-manifest.py")
    module = importlib.util.module_from_spec(definition)
    definition.loader.exec_module(module)
    original = helper.read_bytes()
    with pefile.PE(data=original) as pe:
        machine_offset = pe.FILE_HEADER.get_field_absolute_offset("Machine")
        clr_offset = pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].get_field_absolute_offset("VirtualAddress")
        import_offset = pe.get_offset_from_rva(pe.DIRECTORY_ENTRY_IMPORT[0].struct.Name)
    mutations = {}
    wrong_machine = bytearray(original)
    struct.pack_into("<H", wrong_machine, machine_offset, 0x8664)
    mutations["machine"] = wrong_machine
    clr = bytearray(original)
    struct.pack_into("<I", clr, clr_offset, 1)
    mutations["clr"] = clr
    dependency = bytearray(original)
    dependency[import_offset:import_offset + 9] = b"evil.dll\0"
    mutations["dependency"] = dependency
    assert b'asInvoker' in original
    mutations["elevation"] = original.replace(b'asInvoker', b'requireAd')
    for name, data in mutations.items():
        changed = tmp_path / (name + ".exe")
        changed.write_bytes(data)
        with pytest.raises(ValueError):
            module.audit_native_helper(changed)
