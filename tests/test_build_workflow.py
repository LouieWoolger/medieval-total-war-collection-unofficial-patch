"""Build/test boundaries: no implicit game input and no stale validation evidence."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"


def run_script(path: Path, *args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    env = dict(os.environ if env is None else env)
    # Python does not filter a PowerShell 7 parent's module paths when starting PS5.1.
    env["PSModulePath"] = str(POWERSHELL.parent / "Modules") + os.pathsep + str(
        Path(os.environ["ProgramFiles"]) / "WindowsPowerShell/Modules")
    return subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                           "-File", str(path), *args], cwd=ROOT, env=env,
                          capture_output=True, text=True, timeout=30)


def test_build_needs_tools_but_no_game_input(tmp_path: Path) -> None:
    env = dict(os.environ, MTW_TEST_GAME_EXE=str(tmp_path / "must-not-read.exe"),
               MTW_RUN_COMPILED_INSTALLER_TESTS="1", MTW_RUN_LIFECYCLE_FAULTS="1")
    result = run_script(ROOT / "build.ps1", "-NsisDirectory", str(tmp_path / "missing-nsis"),
                        "-PythonPath", str(POWERSHELL), env=env)
    assert result.returncode != 0
    assert "specified NSIS directory" in result.stderr
    assert "Supply -SupportedGameExecutable" not in result.stderr


def test_game_tests_require_explicit_game_input(tmp_path: Path) -> None:
    env = dict(os.environ, MTW_TEST_GAME_EXE=str(tmp_path / "inherited.exe"))
    result = run_script(ROOT / "test.ps1", env=env)
    assert result.returncode != 0
    assert "Supply -SupportedGameExecutable" in result.stderr


def test_game_tests_reject_unsupported_executable_before_loading_build(tmp_path: Path) -> None:
    game = tmp_path / "Medieval_TW.exe"
    game.write_bytes(b"unsupported game; must remain unchanged")
    result = run_script(ROOT / "test.ps1", "-SupportedGameExecutable", str(game),
                        "-BuildReport", str(tmp_path / "does-not-exist.json"))
    assert result.returncode != 0
    assert "supported game identity" in result.stderr
    assert game.read_bytes() == b"unsupported game; must remain unchanged"


@pytest.mark.parametrize("changed", [None, "installer", "nativeHelper", "sourceSnapshot", "project_contracts"])
def test_build_record_rejects_changed_inputs(tmp_path: Path, changed: str | None) -> None:
    record = {"schema": "unofficial-medieval-total-war-patch-build-v2", "result": "pass",
              "inputs": {}, "reports": {}}
    for name in ("installer", "nativeHelper", "nativeUnstripped", "uninstaller", "sourceSnapshot",
                 "toolReport", "nativeBuildReport", "audit", "releaseManifest",
                 "project_contracts", "native_guard", "release_hygiene"):
        path = tmp_path / (name + ".bin")
        path.write_bytes(("original " + name).encode())
        group = "reports" if name in {"project_contracts", "native_guard", "release_hygiene"} else "inputs"
        record[group][name] = {"path": str(path), "length": path.stat().st_size,
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest().upper()}
    report = tmp_path / "BUILD_RESULT.json"
    report.write_text(json.dumps(record), encoding="utf-8")
    if changed:
        (tmp_path / (changed + ".bin")).write_bytes(b"changed")
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    wrapper = tmp_path / "read-record.ps1"
    wrapper.write_text("$ErrorActionPreference='Stop'\n. " + quote(ROOT / "tools/build-support.ps1") +
                       "\ntry { $record=Read-BuildRecord " + quote(report) +
                       "; Write-Output $record.inputs.installer.sha256 } catch { Write-Error $_; exit 1 }\n",
                       encoding="utf-8-sig")
    result = run_script(wrapper)
    if changed:
        assert result.returncode != 0
        assert "Build input changed" in result.stderr
    else:
        assert result.returncode == 0, result.stderr
        assert record["inputs"]["installer"]["sha256"] in result.stdout


@pytest.mark.parametrize("sprite,lifecycle,package,faults,expected", [
    ("not-run", "not-run", "not-run", False, "not-run"),
    ("pass", "pass", "not-run", True, "incomplete"),
    ("not-run", "pass", "pass", True, "incomplete"),
    ("pass-with-skips", "pass", "pass", True, "incomplete"),
    ("pass", "pass", "pass", False, "incomplete"),
    ("pass", "pass-with-skips", "pass", True, "incomplete"),
    ("pass", "pass", "fail", True, "fail"),
    ("pass", "pass", "pass", True, "pass"),
])
def test_game_validation_requires_exact_sprite_and_both_suites_without_skips(
    sprite, lifecycle, package, faults, expected,
) -> None:
    spec = importlib.util.spec_from_file_location("manifest_workflow", ROOT / "tools/build-release-manifest.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    validation = {"sprite_exe": {"status": sprite}, "lifecycle": {"status": lifecycle},
                  "compiled_installer": {"status": package},
                  "legacy_migration": {"status": "not-run"}, "lifecycle_fault_tests_enabled": faults}
    assert module.game_test_result(validation)["status"] == expected


def test_runtime_build_gate_requires_the_solo_sprite_disabled_proxy(tmp_path: Path) -> None:
    spec = importlib.util.spec_from_file_location("manifest_workflow", ROOT / "tools/build-release-manifest.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    expected = {"D3D9-scroll-sprite-off.dll": "D3D9-scroll-sprite-off.dll"}
    fields = ("scroll_sprite_off_sha256",)
    report = tmp_path / "r185.json"
    evidence = dict(zip(fields, expected.values()))
    evidence.update(smoke_loader_exit=0, campaign_pan_tests_exit=0)
    report.write_text(json.dumps(evidence), encoding="utf-8")
    assert module.r185_result(report, expected)["status"] == "pass"
    evidence.pop("scroll_sprite_off_sha256")
    report.write_text(json.dumps(evidence), encoding="utf-8")
    assert module.r185_result(report, expected)["status"] == "fail"


@pytest.mark.parametrize("case_count", [0, 2])
def test_requested_empty_or_skipped_historical_suite_cannot_pass(tmp_path: Path, case_count: int) -> None:
    spec = importlib.util.spec_from_file_location("manifest_workflow", ROOT / "tools/build-release-manifest.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    report = tmp_path / "historical.junit.xml"
    cases = '<testcase name="historical"><skipped message="missing fixture" /></testcase>' * case_count
    report.write_text("<testsuite>" + cases + "</testsuite>", encoding="utf-8")
    historical = module.junit_result(report)
    assert historical["tests"] == historical["skipped"] == case_count
    assert historical["report_sha256"] == hashlib.sha256(report.read_bytes()).hexdigest().upper()
    validation = {"sprite_exe": {"status": "pass"}, "lifecycle": {"status": "pass"},
                  "compiled_installer": {"status": "pass"},
                  "legacy_cpp": historical, "lifecycle_fault_tests_enabled": True}
    assert module.game_test_result(validation)["status"] == "incomplete"


@pytest.mark.parametrize("lock_second", [False, True])
def test_validation_publication_restores_metadata_if_second_replace_fails(tmp_path: Path, lock_second: bool) -> None:
    candidate, distribution = tmp_path / "candidate", tmp_path / "dist"
    candidate.mkdir()
    distribution.mkdir()
    for name in ("RELEASE_MANIFEST.json", "SHA256SUMS.txt"):
        (distribution / name).write_bytes(("old " + name).encode())
        (candidate / name).write_bytes(("new " + name).encode())
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    script = "$ErrorActionPreference='Stop'\n. " + quote(ROOT / "tools/build-support.ps1") + "\n"
    if lock_second:
        script += "$lock=[IO.File]::Open(" + quote(distribution / "SHA256SUMS.txt") + ", 'Open', 'Read', 'None')\n"
    script += "try { Publish-ValidationFiles " + quote(candidate) + " " + quote(distribution) + " } finally { "
    script += "$lock.Dispose()" if lock_second else ""
    script += " }\n"
    wrapper = tmp_path / "publish.ps1"
    wrapper.write_text(script, encoding="utf-8-sig")
    result = run_script(wrapper)
    assert (result.returncode != 0) == lock_second, result.stderr
    if lock_second:
        assert "Validation metadata publication failed after 1 replacement" in " ".join(result.stderr.split())
    for name in ("RELEASE_MANIFEST.json", "SHA256SUMS.txt"):
        expected = ("old " if lock_second else "new ") + name
        assert (distribution / name).read_bytes() == expected.encode(), result.stderr
    assert set(p.name for p in distribution.iterdir()) == {"RELEASE_MANIFEST.json", "SHA256SUMS.txt"}


@pytest.mark.parametrize("locked", [False, True])
def test_build_record_update_keeps_old_record_on_write_failure(tmp_path: Path, locked: bool) -> None:
    record = tmp_path / "record.json"
    record.write_text('{"result":"old"}', encoding="utf-8")
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    script = "$ErrorActionPreference='Stop'\n. " + quote(ROOT / "tools/build-support.ps1") + "\n"
    if locked:
        script += "$lock=[IO.File]::Open(" + quote(record) + ", 'Open', 'Read', 'None')\n"
    script += "try { Write-BuildJson " + quote(record) + " @{result='new'} } finally { "
    script += "$lock.Dispose()" if locked else ""
    script += " }\n"
    wrapper = tmp_path / "write-record.ps1"
    wrapper.write_text(script, encoding="utf-8-sig")
    result = run_script(wrapper)
    assert (result.returncode != 0) == locked, result.stderr
    assert json.loads(record.read_text(encoding="utf-8"))["result"] == ("old" if locked else "new")
    assert not list(tmp_path.glob("*.tmp"))
