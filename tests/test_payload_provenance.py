import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
R185 = ROOT / "vendor" / "r185"

EXPECTED_RUNTIME = {
    "D3D9.dll": (158720, "CBB6A16CE535640B4FDB6526F42E575EF882E4CFE232BA8CF8BAAF8735E8596A"),
    "dgVoodoo_D3D9.dll": (485888, "E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6"),
    "ddraw.dll": (258560, "81325E9B5C71F544B9A28AE4C375AF38E12535E8AC57C8F33B5456A342AE1465"),
    "D3DImm.dll": (210432, "FBE72EF46AE87DC80F5AEB3D8FC12F97F9D9B2274C4887C70BA65651458D5BF2"),
    "dgVoodoo.conf": (21910, "EF8DF4EBA5AF028891678A641304D297F3D759A7EBF4FBE8FFB735B9808E5A97"),
}

LOCKED_CONFIG = {
    "Resampling": "lanczos-3",
    "ScalingMode": "stretched_ar",
    "FullscreenAttributes": "fake",
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
        "source/mapper_activation_core.c",
        "source/mapper_activation_core.h",
        "source/primary_origin_guard_core.c",
        "source/primary_origin_guard_core.h",
        "source/presentation_input_core.c",
        "source/presentation_input_core.h",
        "source/resolution_filter_core.c",
        "source/resolution_filter_core.h",
        "source/window_transition_guard_core.c",
        "source/window_transition_guard_core.h",
        "source/build_scaffold.ps1",
        "accepted-dust-source/d3d9_proxy.c",
        "accepted-dust-source/d3d9_proxy.def",
        "accepted-dust-source/dust_cadence.h",
        "accepted-dust-source/build_release.ps1",
        "tests/smoke_loader.c",
        "tests/focus_plane_shadow_tests.c",
        "tests/primary_origin_guard_tests.c",
        "tests/presentation_input_core_tests.c",
        "tests/resolution_filter_tests.c",
        "tests/window_transition_guard_tests.c",
        "tests/mapper_activation_tests.c",
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


def test_loading_capture_disarms_from_semantic_game_state_before_copying() -> None:
    source = (R185 / "source" / "frontend_fix.c").read_text(encoding="utf-8")
    match = re.search(
        r"__declspec\(noinline\) static void capture_loading_plane\(void\) "
        r"\{(?P<body>.*?)\n\}\n\n__declspec\(noinline\) static void "
        r"restore_loading_plane_after_lock",
        source,
        re.DOTALL,
    )
    assert match is not None
    body = match.group("body")
    synchronize = body.index("(void)synchronize_mapper_with_game_mode();")
    begin_shadow = body.index("begin_loading_shadow_operation()")
    capture = body.index("loading_shadow_capture(&loading_shadow, &surface)")
    assert synchronize < begin_shadow < capture


def test_primary_origin_guard_notifies_owner_without_resolution_heuristics() -> None:
    source = (R185 / "source" / "frontend_fix.c").read_text(encoding="utf-8")
    core = (R185 / "source" / "primary_origin_guard_core.c").read_text(
        encoding="utf-8"
    )
    header = (R185 / "source" / "primary_origin_guard_core.h").read_text(
        encoding="utf-8"
    )
    build = (R185 / "source" / "build_scaffold.ps1").read_text(
        encoding="utf-8-sig"
    )

    stub = re.search(
        r"primary_origin_guard_hook_stub\(void\) "
        r"\{(?P<body>.*?)\n\}",
        source,
        re.DOTALL,
    )
    assert stub is not None
    body = stub.group("body")
    assert "test eax, eax" in body
    assert "push dword ptr [esi + 0x28]" in body
    assert "push dword ptr [0x00F44098]" in body
    assert "push dword ptr [esi + 0x04]" in body
    assert "call mtw_primary_origin_guard_touch" in body
    assert "MTW_FRONTEND_WIDTH" not in body
    assert "MTW_FRONTEND_HEIGHT" not in body

    assert "locked_surface == current_primary_staging_surface" in core
    assert "VirtualQuery(bits" in core
    assert "PAGE_GUARD" in core
    assert "volatile unsigned char" in core
    assert "*origin = value" in core
    assert "__try" in core
    assert "__except" in core
    assert "VirtualProtect" not in core

    assert "#define MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE 7u" in header
    assert "mtw_primary_origin_guard_hook_supported" in source
    assert "primary_origin_guard_installed = write_rel_jump" in source
    assert "restore_original(primary_origin_guard_target" in source
    assert "primary_origin_guard_core.obj" in build
    assert "primary_origin_guard_tests.exe" in build
    assert "primary_origin_guard_tests_exit = 0" in build
    assert build.count("'/EHa'") == 2
