"""Audit real PE mutations so an incompatible helper cannot pass a release gate."""
import importlib.util
import os
from pathlib import Path
import struct

import pefile
import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def manifest_tool():
    spec = importlib.util.spec_from_file_location("package_manifest", ROOT / "tools/build-release-manifest.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("mutation", ["subsystem", "modern_import", "ordinal_import", "unparsed_delay_import"])
def test_helper_audit_refuses_incompatible_platform_imports(manifest_tool, tmp_path, mutation):
    helper = Path(os.environ["MTW_TEST_NATIVE_HELPER"])
    data = bytearray(helper.read_bytes())
    with pefile.PE(data=bytes(data)) as pe:
        if mutation == "subsystem":
            offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("MajorSubsystemVersion")
            struct.pack_into("<HH", data, offset, 6, 1)
        elif mutation == "modern_import":
            entry = next(i for dll in pe.DIRECTORY_ENTRY_IMPORT for i in dll.imports
                         if i.name == b"InitializeCriticalSection")
            offset = pe.get_offset_from_rva(entry.hint_name_table_rva) + 2
            size = len(entry.name) + 1
            data[offset:offset + size] = b"InitializeSRWLock\0".ljust(size, b"\0")
        elif mutation == "ordinal_import":
            entry = pe.DIRECTORY_ENTRY_IMPORT[0].imports[0]
            struct.pack_into("<I", data, entry.thunk_offset, 0x80000001)
        else:
            offset = pe.OPTIONAL_HEADER.DATA_DIRECTORY[13].get_field_absolute_offset("VirtualAddress")
            struct.pack_into("<II", data, offset, pe.OPTIONAL_HEADER.SizeOfImage - 16, 32)
    changed = tmp_path / (mutation + ".exe")
    changed.write_bytes(data)
    with pytest.raises(ValueError):
        manifest_tool.audit_native_helper(changed)


def test_junit_keeps_c_fixture_counts_separate_from_test_totals(manifest_tool, tmp_path):
    report = tmp_path / "tests.xml"
    report.write_text('''<testsuites><testsuite tests="1"><properties>
        <property name="c_native_assertions" value="52"/>
        <property name="c_platform_modern_assertions" value="120"/>
        <property name="c_python_hash_vectors" value="13"/>
        </properties><testcase name="fixture"/></testsuite></testsuites>''', encoding="utf-8")
    result = manifest_tool.junit_result(report)
    assert result["passed"] == result["tests"] == 1
    assert result.get("c_fixture_assertions") == {"c_native_assertions": 52, "c_platform_modern_assertions": 120}
    assert result.get("c_python_hash_vectors") == 13
    assert "native_assertions" not in result
