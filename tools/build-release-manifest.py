from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ASSET_NAMES = (
    "medieval.ico",
    "welcome-finish.bmp",
    "compatibility.bmp",
    "discord-badge.bmp",
    "discord-badge-hover.bmp",
    "kofi-badge.bmp",
    "kofi-badge-hover.bmp",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def git_revision() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "uncommitted"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the path-neutral release manifest.")
    parser.add_argument(
        "--installer",
        type=Path,
        default=ROOT / "dist" / "Unofficial Medieval Total War Collection Patch.exe",
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "dist" / "RELEASE_MANIFEST.json"
    )
    parser.add_argument(
        "--compiled-matrix", choices=("pass", "not-run"), default="not-run"
    )
    args = parser.parse_args()

    installer = args.installer.resolve()
    if not installer.is_file():
        raise SystemExit(f"Installer does not exist: {installer}")

    product = load_json(ROOT / "config" / "product.json")
    payload = load_json(ROOT / "vendor" / "runtime" / "payload-manifest.json")
    assets = {
        name: {
            "sha256": sha256(ROOT / "assets" / name),
            "length": (ROOT / "assets" / name).stat().st_size,
        }
        for name in ASSET_NAMES
    }

    manifest = {
        "schema": "unofficial-medieval-total-war-patch-release-v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "product": {
            "name": product["product_name"],
            "version": product["version"],
            "company": product["company_name"],
        },
        "installer": {
            "filename": installer.name,
            "length": installer.stat().st_size,
            "sha256": sha256(installer),
            "signature": "not-signed",
        },
        "supported_executable": payload["target_executable"]["name"],
        "supported_executable_sha256": payload["target_executable"]["sha256"],
        "runtime": {
            "identity": payload["runtime_identity"],
            "dgvoodoo_version": payload["dgvoodoo_version"],
            "files": payload["files"],
            "locked_settings": payload["locked_settings"],
        },
        "assets": assets,
        "validation": {
            "r185_reproducible_builds": 1,
            "r185_native_component_tests_per_build": 8,
            "project_contract_tests": 29,
            "compiled_installer_scenarios": 8,
            "compiled_installer_matrix": args.compiled_matrix,
        },
        "source": {
            "revision": git_revision(),
        },
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=False) + "\n",
        encoding="utf-8",
    )
    print(f"Generated {args.output.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
