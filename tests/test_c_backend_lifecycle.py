"""Real C lifecycle and frozen C++ upgrade/recovery contracts on independent games."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import winreg
import pefile

import pytest

from test_lifecycle import (games, digest, relevant, read_receipt, registry, run,
                            run_at_wait, registry_key, ROOT, PAYLOAD, STATE, TX, UNINSTALL, PRODUCT)
from native_helper import seal, canonical_json
from test_lifecycle_adversarial import tree_snapshot
from registry_isolation import assert_empty_registration, read_registration, registration_name


def build_c_engine(output: Path, *, isolated=False, source_name=None):
    source = ROOT / (source_name or ("tests/native_registry_override.c" if isolated else "src/medieval_fix_patcher.c"))
    assert source.is_file(), "The real C99 lifecycle entry point is not implemented"
    compiler = Path(os.environ["MTW_CC"])
    version = subprocess.check_output([str(compiler), "-dumpfullversion"], text=True).strip()
    prefix = compiler.parent.parent / "libexec/gcc/i686-w64-mingw32" / version
    env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ["PATH"])
    resource = output.with_suffix(".o")
    rc_command = [str(compiler.with_name("i686-w64-mingw32-windres.exe")), "--target=pe-i386",
                  "--preprocessor=" + str(compiler), "--preprocessor-arg=-E", "--preprocessor-arg=-xc",
                  "--preprocessor-arg=-DRC_INVOKED", "--preprocessor-arg=-B", "--preprocessor-arg=" + str(prefix) + os.sep,
                  "medieval_fix_patcher.rc", "-O", "coff", "-o", str(resource)]
    resource_build = subprocess.run(rc_command, cwd=ROOT / "src", env=env, capture_output=True, text=True, timeout=60)
    (output.parent / "resource.json").write_text(json.dumps(rc_command, indent=2))
    (output.parent / "resource.stdout").write_text(resource_build.stdout)
    (output.parent / "resource.stderr").write_text(resource_build.stderr)
    assert resource_build.returncode == 0 and not resource_build.stdout and not resource_build.stderr
    command = [str(compiler), "-B", str(prefix) + os.sep, "-std=c99", "-D_WIN32_WINNT=0x0501",
               "-DUNICODE", "-D_UNICODE", "-Os", "-Wall", "-Wextra", "-Werror", "-municode",
               "-static", "-static-libgcc", "-Wl,--major-subsystem-version,5,--minor-subsystem-version,1",
               str(source), str(resource), "-ladvapi32", "-lrpcrt4", "-lversion", "-o", str(output)]
    if isolated:
        command.insert(1, '-DMTW_REGISTRY_ENGINE_SOURCE="../src/medieval_fix_patcher.c"')
    built = subprocess.run(command, capture_output=True, text=True, timeout=120, env=env)
    (output.parent / "compile.json").write_text(json.dumps(command, indent=2))
    (output.parent / "compile.stdout").write_text(built.stdout)
    (output.parent / "compile.stderr").write_text(built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    assert not built.stdout and not built.stderr
    return output


@pytest.fixture(scope="module")
def c_engine(tmp_path_factory):
    if os.environ.get("MTW_TEST_C_BACKEND") == "1":
        path = Path(os.environ["MTW_TEST_NATIVE_HELPER"])
        assert path.is_file(), "MTW_TEST_NATIVE_HELPER must identify the actual built C helper"
        return path
    return build_c_engine(tmp_path_factory.mktemp("c-engine") / "medieval_fix_patcher.exe")


@pytest.fixture(scope="module")
def c_allocation_engine(tmp_path_factory):
    return build_c_engine(tmp_path_factory.mktemp("c-allocation-engine") / "medieval_fix_patcher.exe",
                          source_name="tests/native_lifecycle.c")


def call(helper, game, operation="Install", fault=""):
    uninstaller = game.parent / "comparison-uninstaller.bin"
    if not uninstaller.exists():
        uninstaller.write_bytes(b"independently owned lifecycle comparison uninstaller\x00\xff")
    command = [str(helper), "--operation", operation, "--target", str(game), "--payload", str(PAYLOAD),
               "--version", PRODUCT["version"], "--uninstaller", str(uninstaller)]
    env = dict(os.environ)
    if fault:
        (game / ".umtwp-test-fixture").write_text("disposable C compatibility fixture")
        env["MTW_ENABLE_LIFECYCLE_FAULTS"] = "1"
        command += ["--test-fault", fault]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", env=env, timeout=90)
    lines = [line for line in result.stdout.splitlines() if line.startswith("{")]
    return result, json.loads(lines[-1]) if lines else {}


def require_ok(result):
    process, report = result
    assert process.returncode == 0, (process.returncode, process.stdout, process.stderr)
    return report


def test_c_install_repair_restores_original_bytes_and_preserves_guid(c_engine, games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"personal original configuration\x00\xff")
    baseline = relevant(game)
    first = require_ok(call(c_engine, game))
    require_ok(call(c_engine, game, "Verify"))
    repaired = require_ok(call(c_engine, game))
    assert repaired["installation_id"] == first["installation_id"]
    assert repaired["repair_count"] == 1
    require_ok(call(c_engine, game, "Restore"))
    assert relevant(game) == baseline
    assert not registry(game)
    assert not (game / STATE).exists() and not (game / UNINSTALL).exists()


def test_c_retained_empty_container_supports_rollback_recovery_and_reinstall(c_engine, games):
    game = games()
    name = registration_name(game)
    baseline = relevant(game)
    assert read_registration(name) is None
    process, report = call(c_engine, game, fault="throw:after-registry")
    assert process.returncode == 2 and report["rollback"] == "verified", report
    assert relevant(game) == baseline
    assert_empty_registration(name)
    process, _ = call(c_engine, game, fault="crash:after-registry")
    assert process.returncode == 97
    require_ok(call(c_engine, game, "Install"))
    require_ok(call(c_engine, game, "Restore"))
    assert relevant(game) == baseline
    assert_empty_registration(name)
    require_ok(call(c_engine, game, "Install"))
    require_ok(call(c_engine, game, "Verify"))
    require_ok(call(c_engine, game, "Restore"))
    assert relevant(game) == baseline
    assert_empty_registration(name)


@pytest.fixture
def frozen_cpp():
    value = os.environ.get("MTW_LEGACY_CPP_HELPER")
    if not value:
        pytest.skip("MTW_LEGACY_CPP_HELPER explicitly enables real frozen C++ compatibility")
    path = Path(value)
    assert digest(path) == "89CF361AE34762CC856460797D4014AC81B2C2837919EBE95E912EB5900B5659"
    return path


def stable_receipt(receipt):
    volatile = {"installation_id", "target_directory", "directory_identity", "registration_key",
                "installed_utc", "completed_utc", "last_repaired_utc", "integrity_sha256"}
    return {key: value for key, value in receipt.items() if key not in volatile}


def test_c_and_frozen_cpp_independent_install_repair_and_removal_match(c_engine, frozen_cpp, games):
    old, new = games("C++ reference"), games("C candidate")
    for game in (old, new):
        (game / "dgVoodoo.conf").write_bytes(b"personal original\x00\xff")
    baselines = [relevant(game) for game in (old, new)]
    for helper, game in ((frozen_cpp, old), (c_engine, new)):
        require_ok(call(helper, game))
        require_ok(call(helper, game))
    assert stable_receipt(read_receipt(old)) == stable_receipt(read_receipt(new))
    assert registry(old)["ProductId"] == registry(new)["ProductId"]
    assert registry(old)["OwnerSid"] == registry(new)["OwnerSid"]
    for helper, game, baseline in zip((frozen_cpp, c_engine), (old, new), baselines):
        require_ok(call(helper, game, "Restore"))
        assert relevant(game) == baseline
        assert not registry(game) and not (game / UNINSTALL).exists()


@pytest.mark.skipif(os.environ.get("MTW_RUN_LIFECYCLE_FAULTS") != "1", reason="explicit crash-test opt-in")
@pytest.mark.parametrize("operation,point", [
    (operation, point) for operation in ("Install", "Restore")
    for point in ("after-file-0", "after-file-5", "after-file-6", "after-registry", "before-commit")
])
def test_c_recovers_real_frozen_cpp_interruption(c_engine, frozen_cpp, games, operation, point):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"earliest original; never patch bytes")
    baseline = relevant(game)
    original_id = require_ok(call(frozen_cpp, game))["installation_id"]
    process, _ = call(frozen_cpp, game, operation, "crash:" + point)
    assert process.returncode == 97, (process.stdout, process.stderr)
    assert (game / TX / "journal.json").is_file()
    require_ok(call(c_engine, game, "Install"))
    assert read_receipt(game)["installation_id"] == original_id
    require_ok(call(c_engine, game, "Restore"))
    assert relevant(game) == baseline
    assert not registry(game) and not (game / TX).exists()


@pytest.mark.parametrize("request_bytes", [
    "operation=Inspect\ntarget=X\x00Y\npayload=Z\nversion=1.0.0\n".encode("utf-16le"),
    "operation=Inspect\noperation=Restore\n".encode("utf-16le"), b"\xff", b"\x00\xd8",
])
def test_c_request_rejects_duplicate_embedded_nul_and_invalid_utf16(c_engine, tmp_path, request_bytes):
    request = tmp_path / "invalid-request.txt"
    request.write_bytes(request_bytes)
    result = subprocess.run([str(c_engine), "--request", str(request)], capture_output=True, text=True)
    assert result.returncode == 2
    assert json.loads(result.stdout)["code"] == "invalid_request"


@pytest.fixture
def c_faults(c_engine, monkeypatch):
    import test_lifecycle
    monkeypatch.setattr(test_lifecycle, "ENGINE", c_engine)


@pytest.mark.parametrize("change", ["status", "original", "installed", "guid"])
def test_staged_receipt_semantics_are_validated_before_recovery(c_engine, games, change):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"trusted original")
    require_ok(call(c_engine, game))
    process, _ = call(c_engine, game, fault="crash:after-file-0")
    assert process.returncode == 97
    journal_path = game / TX / "journal.json"
    journal = json.loads(journal_path.read_text())
    index, action = next((i, a) for i, a in enumerate(journal["actions"])
                         if a.get("relative") == STATE + "/install-manifest.json")
    stage = game / TX / "after" / f"{index:03d}.bin"
    staged = json.loads(stage.read_text())
    if change == "status": staged["status"] = "restored"
    elif change == "guid": staged["installation_id"] = "11111111-2222-4333-8444-555555555555"
    else: staged["files"]["dgVoodoo.conf"][change + "_sha256"] = "A" * 64
    seal(staged)
    stage.write_text(canonical_json(staged), encoding="utf-8")
    action["after"].update(sha256=digest(stage), length=stage.stat().st_size)
    seal(journal)
    journal_path.write_text(canonical_json(journal), encoding="utf-8")
    before, entry = tree_snapshot(game), registry(game)
    process, report = call(c_engine, game)
    assert process.returncode == 2 and report["code"] == "receipt_invalid", report
    assert tree_snapshot(game) == before and registry(game) == entry


def test_previous_slot_recovery_rebinds_new_primary_file_identity(c_engine, games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"trusted original before interrupted install")
    baseline = relevant(game)
    process, _ = call(c_engine, game, fault="crash:after-file-0")
    assert process.returncode == 97
    primary = game / TX / "journal.json"
    previous = primary.with_name("journal.json.mtw-previous")
    shutil.copyfile(primary, previous)
    primary.unlink()
    require_ok(call(c_engine, game))
    require_ok(call(c_engine, game, "Restore"))
    assert relevant(game) == baseline and not registry(game)


def test_pre_win7_install_refuses_before_mutation_but_inspect_and_restore_work(c_engine, games):
    game = games()
    (game / ".umtwp-test-fixture").write_text("disposable C compatibility fixture")
    before = tree_snapshot(game)
    process, report = call(c_engine, game, fault="os:pre-win7")
    assert process.returncode == 2 and report["code"] == "platform_unsupported", report
    assert tree_snapshot(game) == before and not registry(game)
    require_ok(call(c_engine, game, "Inspect", "os:pre-win7"))
    require_ok(call(c_engine, game))
    require_ok(call(c_engine, game, "Restore", "os:pre-win7"))


def test_refused_stdout_write_reports_completed_removal_as_warning(c_engine, games):
    game = games()
    baseline = relevant(game)
    require_ok(call(c_engine, game))
    sink = game.parent / "read-only-output.txt"
    sink.write_bytes(b"must remain unchanged")
    with sink.open("rb") as output:
        process = subprocess.run([str(c_engine), "--operation", "Restore", "--target", str(game),
                                  "--payload", str(PAYLOAD), "--version", "1.0.0"],
                                 stdout=output, stderr=subprocess.PIPE, text=True, timeout=60)
    assert process.returncode == 4, process.stderr
    assert "Diagnostic output failed" in process.stderr
    assert sink.read_bytes() == b"must remain unchanged"
    assert relevant(game) == baseline and not registry(game)
    assert not (game / UNINSTALL).exists() and not (game / TX).exists()


def test_changed_published_journal_is_never_adopted_as_our_journal(c_faults, games):
    game = games()
    baseline = relevant(game)
    replaced = []

    def replace():
        directories = list(game.glob(".unofficial-medieval-patch-preparing-*"))
        assert len(directories) == 1
        journal = directories[0] / "journal.json"
        journal.write_bytes(b"foreign content after journal publication")
        replaced.append(journal)

    code, report, stdout, stderr = run_at_wait("Install", game, "after-journal-write", replace)
    assert replaced, (code, stdout, stderr)
    assert code == 2 and report["code"] == "file_changed", report
    assert replaced[0].read_bytes() == b"foreign content after journal publication"
    assert relevant(game) == baseline and not registry(game)


def test_registry_change_immediately_before_write_is_preserved(c_faults, games):
    game = games()
    first = require_ok(run("Install", game))
    changed = []

    def replace():
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0, winreg.KEY_ALL_ACCESS) as key:
            winreg.SetValueEx(key, "PersonalValue", 0, winreg.REG_SZ, "latest external data")
        changed.append(True)

    code, report, stdout, stderr = run_at_wait("Install", game, "before-registry-write", replace)
    assert changed, (code, stdout, stderr)
    assert code == 2 and report["code"] == "concurrent_change", report
    assert registry(game)["PersonalValue"] == "latest external data"
    assert registry(game)["InstallationId"] == first["installation_id"]
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_key(game), 0, winreg.KEY_ALL_ACCESS) as key:
        winreg.DeleteValue(key, "PersonalValue")
    require_ok(run("Install", game))
    require_ok(run("Restore", game))


def test_changed_receipt_after_publication_keeps_committed_recovery_state(c_faults, games):
    game = games()
    changed = []

    def replace():
        receipt = game / STATE / "install-manifest.json"
        receipt.write_bytes(b"user content written after receipt publication")
        changed.append(receipt)

    code, report, stdout, stderr = run_at_wait("Install", game, "before-commit", replace)
    assert changed, (stdout, stderr)
    assert code == 2 and report["code"] == "recovery_conflict", report
    assert changed[0].read_bytes() == b"user content written after receipt publication"
    assert (game / TX / "journal.json").is_file()


@pytest.mark.parametrize("operation,point,persistence", [
    ("Install", "1", "once"), ("Install", "10", "once"), ("Install", "100", "persistent"),
    ("Install", "journal", "once"), ("Install", "journal", "persistent"),
    ("Install", "runtime", "once"), ("Install", "runtime", "persistent"),
    ("Restore", "runtime", "once"), ("Restore", "runtime", "persistent"),
    ("Restore", "removed", "persistent"),
])
def test_allocation_failure_releases_resources_and_retains_real_recovery(c_engine, c_allocation_engine, games,
                                                                        operation, point, persistence):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"earliest original survives allocation failure")
    baseline = relevant(game)
    uninstaller = game.parent / "comparison-uninstaller.bin"
    uninstaller.write_bytes(b"independently owned lifecycle comparison uninstaller\x00\xff")
    if operation == "Restore": require_ok(call(c_engine, game))
    process = subprocess.run([str(c_allocation_engine), operation, str(game), str(PAYLOAD), str(uninstaller),
                              point, persistence], capture_output=True, text=True, timeout=90)
    assert process.returncode == 0, (process.stdout, process.stderr)
    report = json.loads(process.stdout)
    assert report["triggered"] == 1 and report["ok"] == 0, report
    assert report["live"] == 0 and report["handles_after"] == report["handles_before"], report
    assert report["exit"] == (4 if point == "removed" else 2), report
    if point == "removed":
        assert relevant(game) == baseline and not (game / UNINSTALL).exists() and not registry(game)
    else:
        require_ok(call(c_engine, game, "Install"))
        require_ok(call(c_engine, game, "Restore"))
        assert relevant(game) == baseline and not registry(game)


def test_failed_publication_reports_held_original_path_before_cleanup(c_engine, c_allocation_engine, games):
    game = games()
    original = b"original retained by failed file publication"
    (game / "dgVoodoo.conf").write_bytes(original)
    baseline = relevant(game)
    uninstaller = game.parent / "comparison-uninstaller.bin"
    uninstaller.write_bytes(b"independently owned lifecycle comparison uninstaller\x00\xff")
    process = subprocess.run([str(c_allocation_engine), "Install", str(game), str(PAYLOAD), str(uninstaller),
                              "flush", "once"], capture_output=True, text=True, timeout=90)
    assert process.returncode == 0, process.stderr
    outcome, ledger = [json.loads(line) for line in process.stdout.splitlines()]
    assert ledger["triggered"] == 1 and ledger["live"] == 0, ledger
    assert outcome["code"] == "recovery_required" and outcome["rollback"] == "incomplete", outcome
    preserved = Path(outcome["recovery_file"])
    assert preserved.parent == game and preserved.read_bytes() == original
    assert (game / TX / "journal.json").is_file()
    require_ok(call(c_engine, game))
    require_ok(call(c_engine, game, "Restore"))
    assert relevant(game) == baseline and preserved.read_bytes() == original


def test_absent_stdout_handle_cannot_report_success(c_engine, c_allocation_engine, games):
    game = games()
    baseline = relevant(game)
    require_ok(call(c_engine, game))
    process = subprocess.run([str(c_allocation_engine), "NoStdout", str(game), str(PAYLOAD), "unused", "0", "once"],
                              capture_output=True, text=True, timeout=90)
    assert process.returncode == 4 and "Diagnostic output failed" in process.stderr, (process.stdout, process.stderr)
    assert relevant(game) == baseline and not registry(game) and not (game / UNINSTALL).exists()


def test_terminal_recovery_checks_live_receipt_semantics_when_stage_retired(c_engine, games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"original baseline")
    require_ok(call(c_engine, game))
    (game / STATE / "personal-note.txt").write_bytes(b"keep unknown metadata")
    process, report = call(c_engine, game, "Restore", "throw:cleanup-uninstaller")
    assert process.returncode == 3, report
    receipt_path = game / STATE / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text())
    receipt["files"]["dgVoodoo.conf"]["original_sha256"] = "A" * 64
    seal(receipt)
    receipt_path.write_text(canonical_json(receipt), encoding="utf-8")
    journal_path = game / TX / "journal.json"
    journal = json.loads(journal_path.read_text())
    index, action = next((i, a) for i, a in enumerate(journal["actions"])
                         if a.get("relative") == STATE + "/install-manifest.json")
    action["after"].update(sha256=digest(receipt_path), length=receipt_path.stat().st_size)
    seal(journal)
    journal_path.write_text(canonical_json(journal), encoding="utf-8")
    (game / TX / "after" / f"{index:03d}.bin").unlink()
    before, entry = tree_snapshot(game), registry(game)
    process, report = call(c_engine, game, "Restore")
    assert process.returncode == 3 and report["code"] == "cleanup_pending", report
    assert tree_snapshot(game) == before and registry(game) == entry


def test_valid_utf16_wizard_request_uses_full_unicode_path(c_engine, games):
    game = games("Unicode caf\u00e9 \U0001f600 & equals=folder")
    baseline = relevant(game)
    uninstaller = game.parent / "request-uninstaller.bin"
    uninstaller.write_bytes(b"real request supplied uninstaller bytes")
    request = game.parent / "wizard-request.txt"
    for operation in ("Inspect", "Install", "Verify", "Restore"):
        request.write_text(f"operation={operation}\r\ntarget={game}\r\npayload={PAYLOAD}\r\nversion=1.0.0\r\nuninstaller={uninstaller}\r\n",
                           encoding="utf-16", newline="")
        process = subprocess.run([str(c_engine), "--request", str(request)], capture_output=True, text=True, encoding="utf-8", timeout=60)
        assert process.returncode == 0, (process.stdout, process.stderr)
        assert json.loads(process.stdout)["target"] == str(game)
    assert relevant(game) == baseline and not registry(game)


def test_committed_install_with_retired_stage_preserves_mismatched_live_receipt(c_engine, games):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"original baseline")
    process, report = call(c_engine, game, "Install", "crash:before-commit")
    assert process.returncode == 97, report
    receipt_path = game / STATE / "install-manifest.json"
    receipt = json.loads(receipt_path.read_text())
    receipt["files"]["dgVoodoo.conf"]["original_sha256"] = "A" * 64
    seal(receipt)
    receipt_path.write_text(canonical_json(receipt), encoding="utf-8")
    journal_path = game / TX / "journal.json"
    journal = json.loads(journal_path.read_text())
    # Model a checksum-valid terminal journal with its receipt copy already
    # retired; its retained receipt still binds the actual original baseline.
    journal["phase"] = "committed"
    index, action = next((i, a) for i, a in enumerate(journal["actions"])
                         if a.get("relative") == STATE + "/install-manifest.json")
    action["after"].update(sha256=digest(receipt_path), length=receipt_path.stat().st_size)
    seal(journal)
    journal_path.write_text(canonical_json(journal), encoding="utf-8")
    (game / TX / "after" / f"{index:03d}.bin").unlink()
    before, entry = tree_snapshot(game), registry(game)
    process, report = call(c_engine, game, "Install")
    assert process.returncode == 2 and report["code"] == "receipt_invalid", report
    assert tree_snapshot(game) == before and registry(game) == entry


def test_real_c_helper_is_pe32_without_cpp_or_newer_static_imports(c_engine, tmp_path):
    with pefile.PE(str(c_engine)) as pe:
        assert pe.FILE_HEADER.Machine == 0x14C
        assert (pe.OPTIONAL_HEADER.MajorSubsystemVersion, pe.OPTIONAL_HEADER.MinorSubsystemVersion) == (5, 1)
        imports = {entry.dll.decode().lower(): [item.name.decode() for item in entry.imports if item.name]
                   for entry in pe.DIRECTORY_ENTRY_IMPORT}
        assert set(imports) <= {"kernel32.dll", "msvcrt.dll", "advapi32.dll", "rpcrt4.dll", "version.dll"}
        flat = {name for values in imports.values() for name in values}
        assert not flat & {"GetFinalPathNameByHandleW", "SetFileInformationByHandle", "CompareStringOrdinal",
                           "QueryFullProcessImageNameW", "RegDeleteKeyExW", "GetTickCount64"}
        stripped = pe.FILE_HEADER.NumberOfSymbols == 0
    symbols_source = c_engine
    if stripped:
        supplied = os.environ.get("MTW_TEST_NATIVE_UNSTRIPPED")
        assert supplied, "A stripped helper requires its source-built unstripped input"
        symbols_source = Path(supplied)
        stripped_copy = tmp_path / "stripped-helper.exe"
        strip = Path(os.environ["MTW_CC"]).with_name("i686-w64-mingw32-strip.exe")
        result = subprocess.run([str(strip), "-p", "-s", "-o", str(stripped_copy), str(symbols_source)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0 and not result.stdout and not result.stderr
        assert digest(stripped_copy) == digest(c_engine), "Symbols must belong to the exact packaged C helper"
    nm = Path(os.environ["MTW_CC"]).with_name("i686-w64-mingw32-nm.exe")
    result = subprocess.run([str(nm), str(symbols_source)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and not result.stderr
    symbols = result.stdout
    (tmp_path / "symbols.txt").write_text(symbols)
    assert symbols, "A stripped executable cannot establish the C++ symbol contract"
    assert not any(name in symbols for name in ("__cxa_", "__gxx_personality", "_Unwind_Resume", "_ZSt"))


@pytest.mark.parametrize("aliased_input", ["target", "payload", "installer", "uninstaller"])
def test_log_is_never_written_before_protected_alias_validation(c_engine, games, aliased_input):
    import _winapi

    game = games()
    payload = game.parent / "independent payload"
    shutil.copytree(PAYLOAD, payload)
    inputs = game.parent / "independent inputs"
    inputs.mkdir()
    supplied = inputs / "supplied-artifact.bin"
    supplied.write_bytes(b"protected installer or uninstaller bytes")
    alias = game.parent / "selected alias"
    destination = game if aliased_input == "target" else payload if aliased_input == "payload" else inputs
    _winapi.CreateJunction(str(destination), str(alias))
    log = game / "Medieval_TW.exe" if aliased_input == "target" else payload / "dgVoodoo.conf" if aliased_input == "payload" else supplied
    command = [str(c_engine), "--operation", "Inspect", "--version", "1.0.0",
               "--target", str(alias if aliased_input == "target" else game),
               "--payload", str(alias if aliased_input == "payload" else payload), "--log", str(log)]
    if aliased_input in ("installer", "uninstaller"):
        command += ["--" + aliased_input, str(alias / supplied.name)]
    before = tree_snapshot(game.parent)
    try:
        process = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=30)
        report = json.loads(process.stdout)
        assert tree_snapshot(game.parent) == before, report
        assert process.returncode == 2 and report["code"] in {"unsafe_path", "invalid_payload"}, report
        assert not process.stderr and not registry(game)
    finally:
        # This is the exact junction created above; rmdir never traverses it.
        alias.rmdir()


@pytest.mark.parametrize("failure", ["missing-uninstaller", "space:maximum", "throw:after-file-0"])
def test_new_install_failure_does_not_inherit_completed_removal_state(c_engine, games, failure):
    game = games()
    (game / "dgVoodoo.conf").write_bytes(b"earliest bytes restored by committed removal recovery")
    baseline = relevant(game)
    require_ok(call(c_engine, game))
    process, report = call(c_engine, game, "Restore", "throw:cleanup-uninstaller")
    assert process.returncode == 3 and report["restoration"] == "verified", report
    assert (game / TX / "journal.json").is_file() and (game / UNINSTALL).is_file() and registry(game)
    if failure == "missing-uninstaller":
        process = subprocess.run([str(c_engine), "--operation", "Install", "--target", str(game),
                                  "--payload", str(PAYLOAD), "--version", "1.0.0", "--uninstaller",
                                  str(game.parent / "missing-uninstaller.bin")], capture_output=True,
                                 text=True, encoding="utf-8", timeout=90)
        report = json.loads(process.stdout)
    else:
        process, report = call(c_engine, game, "Install", failure)
    assert process.returncode == 2 and report["operation"] == "Install", report
    assert report["restoration"] == "not-started" and report["removal_completed"] is False, report
    assert report["code"] == {"missing-uninstaller": "invalid_payload", "space:maximum": "insufficient_space",
                              "throw:after-file-0": "injected_failure"}[failure]
    assert report["rollback"] == ("verified" if failure == "throw:after-file-0" else "not-needed"), report
    assert relevant(game) == baseline and not registry(game)
    assert all(not (game / name).exists() for name in (STATE, TX, UNINSTALL))


@pytest.mark.parametrize("link_kind", ["hardlink", "junction"])
def test_linked_diagnostic_destination_keeps_protected_bytes(c_engine, games, link_kind):
    import _winapi

    game = games()
    directory = game.parent / "protected content"
    directory.mkdir()
    protected = directory / "personal-file.bin"
    protected.write_bytes(b"existing personal bytes must never become a diagnostic log")
    alias = game.parent / "diagnostic alias"
    if link_kind == "hardlink":
        os.link(protected, alias)
        log = alias
    else:
        _winapi.CreateJunction(str(directory), str(alias))
        log = alias / protected.name
    before = tree_snapshot(game.parent)
    try:
        process = subprocess.run([str(c_engine), "--operation", "Inspect", "--target", str(game),
                                  "--payload", str(PAYLOAD), "--version", "1.0.0", "--log", str(log)],
                                 capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert process.returncode == 2 and json.loads(process.stdout)["code"] == "unsafe_path", process.stdout
        assert tree_snapshot(game.parent) == before and not registry(game)
    finally:
        alias.unlink() if link_kind == "hardlink" else alias.rmdir()


@pytest.mark.parametrize("placement", ["external", "dedicated-game-folder"])
def test_guarded_diagnostics_still_append_operation_and_result(c_engine, games, placement):
    game = games()
    directory = game.parent / "diagnostics" if placement == "external" else game / "Unofficial Medieval Patch Logs"
    directory.mkdir()
    log = directory / "operation.log"
    prefix = b"existing diagnostic history\r\n"
    log.write_bytes(prefix)
    supplied = game.parent / "supplied-artifact.bin"
    supplied.write_bytes(b"separate installer and uninstaller input")
    before = relevant(game)
    process = subprocess.run([str(c_engine), "--operation", "Inspect", "--target", str(game),
                              "--payload", str(PAYLOAD), "--version", "1.0.0", "--log", str(log),
                              "--installer", str(supplied), "--uninstaller", str(supplied)],
                             capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert process.returncode == 0 and json.loads(process.stdout)["status"] == "ok", process.stdout
    saved = log.read_bytes()
    assert saved.startswith(prefix) and b" Inspect\r\n" in saved and b'"action":"inspect"' in saved
    assert supplied.read_bytes() == b"separate installer and uninstaller input"
    assert relevant(game) == before and not registry(game)
