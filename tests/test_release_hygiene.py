from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import pefile


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
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
        "docs",
    ):
        assert not (ROOT / relative).exists(), relative


def test_dist_does_not_retain_the_old_unreleased_installer_name() -> None:
    assert not LEGACY_UNRELEASED_INSTALLER.exists()


def test_root_build_pipeline_contains_every_release_gate() -> None:
    text = (ROOT / "build.ps1").read_text(encoding="utf-8")
    for required in (
        "generate-product-nsh.ps1",
        "build_scaffold.ps1",
        "pytest",
        "makensis",
        "run_installer_matrix.ps1",
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
    assert manifest["schema"] == "unofficial-medieval-total-war-patch-release-v1"
    assert manifest["product"]["version"] == "1.0.0"
    assert manifest["installer"]["filename"] == INSTALLER.name
    assert manifest["installer"]["sha256"] == sha256(INSTALLER)
    assert manifest["installer"]["length"] == INSTALLER.stat().st_size
    assert manifest["runtime"]["identity"] == "R185"
    assert manifest["validation"]["project_contract_tests"] == 36
    assert manifest["validation"]["compiled_installer_scenarios"] == 10
    assert manifest["runtime"]["files"]["D3D9.dll"]["sha256"] == "CBB6A16CE535640B4FDB6526F42E575EF882E4CFE232BA8CF8BAAF8735E8596A"
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
        "install-engine.ps1",
        "payload\\payload-manifest.json",
        "payload\\d3d9.dll",
        "payload\\dgvoodoo_d3d9.dll",
        "payload\\ddraw.dll",
        "payload\\d3dimm.dll",
        "payload\\dgvoodoo.conf",
        "compatibility.bmp",
        "uninstall.exe",
    ):
        assert required in lowered
    for forbidden in ("medieval_tw.exe", "medieval.cfg", "~tmp.vrp", ".pdb", ".vrp", "worklog", "capture"):
        assert forbidden not in lowered


def test_distributed_runtime_and_public_files_have_no_private_paths_or_shogun_branding() -> None:
    public_files = [
        ROOT / "installer.nsi",
        ROOT / "src" / "install-engine.ps1",
        ROOT / "config" / "product.json",
        ROOT / "README.md",
        ROOT / "build.ps1",
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
    text_suffixes = {".c", ".h", ".hlsl", ".json", ".nsi", ".nsh", ".ps1", ".py", ".txt"}
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
        ROOT / "src" / "install-engine.ps1",
        ROOT / "build.ps1",
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
