import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "vendor" / "runtime"
R185 = ROOT / "vendor" / "r185"

EXPECTED_RUNTIME = {
    "D3D9.dll": (159232, "300373700D0868CF2B1BA94762132A781E70918A01DB873DA3E8666FCD73B8F1"),
    "dgVoodoo_D3D9.dll": (482304, "6A0CA214784BE04B7C8B547105AA9D79ACF4DC26C0B6F8702B437DDCA54058B2"),
    "ddraw.dll": (255488, "612A24408A090A3C6F3886557FA18034EE742E94AD0A40EBDF854D2816176C2E"),
    "D3DImm.dll": (208384, "93C534F2D17419EA78F15551F7E0AAC78B3C503733A840914FA063708A5AFE8E"),
    "dgVoodoo.conf": (21971, "8B6068BDF5404BCA6424E42CDC6E8EA91BD084503523170C7198B9F3A9C224D0"),
}

LOCKED_CONFIG = {
    "Resampling": "lanczos-3",
    "ScalingMode": "stretched_ar",
    "FullscreenAttributes": "fake",
    "FastVideoMemoryAccess": "true",
    "FPSLimit": "0",
}

OFFICIAL_DGVOODOO_2875 = {
    "dgVoodoo_D3D9.dll": "6A0CA214784BE04B7C8B547105AA9D79ACF4DC26C0B6F8702B437DDCA54058B2",
    "ddraw.dll": "612A24408A090A3C6F3886557FA18034EE742E94AD0A40EBDF854D2816176C2E",
    "D3DImm.dll": "93C534F2D17419EA78F15551F7E0AAC78B3C503733A840914FA063708A5AFE8E",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def test_runtime_payload_is_exact_and_complete() -> None:
    manifest = json.loads((RUNTIME / "payload-manifest.json").read_text(encoding="utf-8"))
    assert manifest["component"] == "Terrain Movement Fix"
    actual_names = {path.name for path in RUNTIME.iterdir() if path.is_file()}
    assert actual_names == set(EXPECTED_RUNTIME) | {
        "payload-manifest.json", "payload-manifest-scroll-off.json",
        "payload-manifest-sprite-off.json", "payload-manifest-scroll-sprite-off.json",
        "payload-manifest-scroll-sprite-on.json",
        "D3D9-scroll-off.dll", "D3D9-sprite-off.dll",
        "D3D9-scroll-sprite-off.dll", "D3D9-scroll-sprite-on.dll", "VERSION.txt",
    }
    assert set(manifest["files"]) == set(EXPECTED_RUNTIME)
    for name, (length, digest) in EXPECTED_RUNTIME.items():
        path = RUNTIME / name
        assert path.stat().st_size == length
        assert sha256(path) == digest
        assert manifest["files"][name]["length"] == length
        assert manifest["files"][name]["sha256"] == digest


def test_scroll_off_payload_differs_only_in_the_proxy() -> None:
    enabled = json.loads((RUNTIME / "payload-manifest-scroll-sprite-on.json").read_text(encoding="utf-8"))
    disabled = json.loads((RUNTIME / "payload-manifest-scroll-off.json").read_text(encoding="utf-8"))
    assert enabled["scroll_fix_enabled"] is True
    assert disabled["scroll_fix_enabled"] is False
    assert disabled["target_executable"] == enabled["target_executable"]
    assert disabled["locked_settings"] == enabled["locked_settings"]
    assert disabled["dgvoodoo_version"] == enabled["dgvoodoo_version"]
    assert set(disabled["files"]) == set(enabled["files"])
    for name in enabled["files"]:
        if name == "D3D9.dll":
            proxy = RUNTIME / "D3D9-scroll-off.dll"
            assert disabled["files"][name]["sha256"] == sha256(proxy)
            assert disabled["files"][name]["length"] == proxy.stat().st_size
            assert disabled["files"][name]["sha256"] != enabled["files"][name]["sha256"]
        else:
            assert disabled["files"][name] == enabled["files"][name]


def test_historical_proxy_variants_remain_identifiable_for_migration() -> None:
    choices = {
        (True, True): ("payload-manifest-scroll-sprite-on.json", "D3D9-scroll-sprite-on.dll"),
        (False, True): ("payload-manifest-scroll-off.json", "D3D9-scroll-off.dll"),
        (True, False): ("payload-manifest-sprite-off.json", "D3D9-sprite-off.dll"),
        (False, False): ("payload-manifest-scroll-sprite-off.json", "D3D9-scroll-sprite-off.dll"),
    }
    reference = json.loads((RUNTIME / "payload-manifest.json").read_text(encoding="utf-8"))
    hashes = set()
    for (scroll, sprite), (manifest_name, binary_name) in choices.items():
        manifest = json.loads((RUNTIME / manifest_name).read_text(encoding="utf-8"))
        binary = RUNTIME / binary_name
        assert manifest["scroll_fix_enabled"] is scroll
        assert manifest["sprite_fix_enabled"] is sprite
        assert manifest["target_executable"] == reference["target_executable"]
        assert manifest["locked_settings"] == reference["locked_settings"]
        assert manifest["files"]["D3D9.dll"]["sha256"] == sha256(binary)
        assert manifest["files"]["D3D9.dll"]["length"] == binary.stat().st_size
        for name in reference["files"].keys() - {"D3D9.dll"}:
            assert manifest["files"][name] == reference["files"][name]
        hashes.add(sha256(binary))
    assert len(hashes) == 4
    assert (RUNTIME / "D3D9.dll").read_bytes() == (RUNTIME / "D3D9-scroll-sprite-off.dll").read_bytes()
    assert (RUNTIME / "payload-manifest.json").read_bytes() == (RUNTIME / "payload-manifest-scroll-sprite-off.json").read_bytes()


def test_official_dgvoodoo_2875_x86_payload_is_pinned() -> None:
    enabled = json.loads((RUNTIME / "payload-manifest.json").read_text(encoding="utf-8"))
    assert enabled["dgvoodoo_version"] == "2.87.5"
    for name, digest in OFFICIAL_DGVOODOO_2875.items():
        assert sha256(RUNTIME / name) == digest
        assert enabled["files"][name]["sha256"] == digest


def test_historical_dust_control_backend_is_build_only_and_pinned() -> None:
    control = R185 / "accepted-dust-source" / "dgVoodoo_D3D9-2.87.2-control.dll"
    assert sha256(control) == "E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6"
    assert control.stat().st_size == 485888
    assert control.name not in {path.name for path in RUNTIME.iterdir()}


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
        "source/campaign_pan_core.c",
        "source/campaign_pan_core.h",
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
        "accepted-dust-source/dgVoodoo_D3D9-2.87.2-control.dll",
        "tests/smoke_loader.c",
        "tests/focus_plane_shadow_tests.c",
        "tests/primary_origin_guard_tests.c",
        "tests/presentation_input_core_tests.c",
        "tests/resolution_filter_tests.c",
        "tests/window_transition_guard_tests.c",
        "tests/mapper_activation_tests.c",
        "tests/mapper_shader_clone_tests.c",
        "tests/campaign_pan_core_tests.c",
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
