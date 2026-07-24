import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
R185 = ROOT / "vendor" / "r185"

EXPECTED_RUNTIME = {
    "D3D9.dll": (133120, "3EE7EE33946F9F73A61559C23505AFCC27D45E61067644AF611B09F627297AD8"),
    "dgVoodoo_D3D9.dll": (485888, "E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6"),
    "ddraw.dll": (258560, "81325E9B5C71F544B9A28AE4C375AF38E12535E8AC57C8F33B5456A342AE1465"),
    "D3DImm.dll": (210432, "FBE72EF46AE87DC80F5AEB3D8FC12F97F9D9B2274C4887C70BA65651458D5BF2"),
    "dgVoodoo.conf": (21907, "23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA"),
}

LOCKED_CONFIG = {
    "Resampling": "lanczos-3",
    "ScalingMode": "stretched_ar",
    "FastVideoMemoryAccess": "true",
    "FPSLimit": "0",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def test_runtime_payload_is_exact_and_complete() -> None:
    manifest = json.loads((RUNTIME / "payload-manifest.json").read_text(encoding="utf-8"))
    assert manifest["component"] == "Terrain Movement Fix"
    actual_names = {path.name for path in RUNTIME.iterdir() if path.is_file()}
    assert actual_names == set(EXPECTED_RUNTIME) | {"payload-manifest.json", "VERSION.txt"}
    assert set(manifest["files"]) == set(EXPECTED_RUNTIME)
    for name, (length, digest) in EXPECTED_RUNTIME.items():
        path = RUNTIME / name
        assert path.stat().st_size == length
        assert sha256(path) == digest
        assert manifest["files"][name]["length"] == length
        assert manifest["files"][name]["sha256"] == digest


def test_config_has_exact_locked_values_and_is_portable() -> None:
    lines = (RUNTIME / "dgVoodoo.conf").read_text(encoding="utf-8-sig").splitlines()
    values: dict[str, list[str]] = {key: [] for key in LOCKED_CONFIG}
    for line in lines:
        if "=" not in line or line.lstrip().startswith(";"):
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        if key in values:
            values[key].append(value)
    assert values == {key: [value] for key, value in LOCKED_CONFIG.items()}
    text = "\n".join(lines)
    assert "F:\\" not in text
    assert "C:\\Users\\" not in text
    assert "DesktopResolution                    = \n" in text
    assert "Adapters                             = all" in text
    assert "AdapterIDType                       = \n" in text


def test_r185_source_bundle_is_complete_and_relative() -> None:
    required = {
        "source/combined_proxy.c",
        "source/combined_proxy.def",
        "source/frontend_fix.c",
        "source/build_scaffold.ps1",
        "accepted-dust-source/d3d9_proxy.c",
        "accepted-dust-source/d3d9_proxy.def",
        "accepted-dust-source/dust_cadence.h",
        "accepted-dust-source/build_release.ps1",
        "tests/smoke_loader.c",
        "tests/focus_plane_shadow_tests.c",
        "tests/mapper_shader_clone_tests.c",
        "tests/assets/mapper-lanczos3-ps-36116AC1.bin",
    }
    actual = {
        path.relative_to(R185).as_posix()
        for path in R185.rglob("*")
        if path.is_file() and "__pycache__" not in path.parts
    }
    assert required <= actual
    build_text = (R185 / "source" / "build_scaffold.ps1").read_text(encoding="utf-8-sig")
    assert not re.search(r"(?i)\b[a-z]:[\\/]", build_text)
    assert "$r185Root = Split-Path -Parent $PSScriptRoot" in build_text
    assert "$acceptedSource = Join-Path $r185Root 'accepted-dust-source'" in build_text
    assert "mapper_single_sample_linear_ps.hlsl" in build_text
    assert "mapper_single_sample_linear_ps.cso" in build_text
    for private_or_diagnostic in (
        "mapper_pixel_load_ps",
        "mapper_safe_lanczos3_ps",
        "mapper_stable_bilinear_ps",
        "known-states",
        "prebuilt",
    ):
        assert private_or_diagnostic not in build_text
    for private_path in (str(ROOT), str(Path.home())):
        assert private_path not in build_text


def test_private_r185_development_material_is_absent() -> None:
    for relative in ("build", "known-states", "prebuilt"):
        assert not (R185 / relative).exists()
    assert not list((R185 / "source").glob("*.asm.txt"))
    assert not list((R185 / "tests").glob("test_*.py"))


def test_no_development_only_runtime_payload() -> None:
    forbidden_suffixes = {".pdb", ".obj", ".lib", ".exp", ".vrp", ".mkv", ".png", ".jpg"}
    assert not [path for path in RUNTIME.rglob("*") if path.is_file() and path.suffix.lower() in forbidden_suffixes]
    assert not (RUNTIME / "Medieval.Cfg").exists()
    assert not (RUNTIME / "Medieval_TW.exe").exists()
