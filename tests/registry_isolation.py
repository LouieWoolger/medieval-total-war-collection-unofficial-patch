"""Registry helpers restricted to explicitly tracked disposable game folders."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import winreg


UNINSTALL_PARENT = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
LEGACY_NAMES = (
    "Unofficial Medieval Total War Collection Patch",
    "Unofficial Medieval Total War Patch",
)
OWNED_NAMES = {
    "displayname", "displayversion", "publisher", "installlocation", "displayicon",
    "uninstallstring", "quietuninstallstring", "nomodify", "norepair", "urlinfoabout",
    "productid", "installationid", "ownersid",
}


def registration_name(game: Path) -> str:
    canonical = str(game.resolve()).rstrip("\\").lower()
    return "UnofficialMedievalPatch-" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:32]


def read_registration(name: str) -> dict | None:
    """Physical values: None means no key; {} means a key with no values.

    This values-only view says nothing about children. Use the assertion below
    before interpreting an empty values dictionary as an inert container.
    """
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_PARENT + "\\" + name,
                            0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
            values = {}
            for index in range(winreg.QueryInfoKey(key)[1]):
                value_name, value, kind = winreg.EnumValue(key, index)
                values[value_name] = (value, kind)
            return values
    except FileNotFoundError:
        return None


def assert_empty_registration(name: str) -> None:
    """Require a physically retained, inert key, including no child keys."""
    assert read_registration(name) == {}, "Expected a retained key with no values"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_PARENT + "\\" + name,
                       0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as key:
        assert winreg.QueryInfoKey(key)[:2] == (0, 0), "Registry container has unrelated contents"


def remove_test_registration(game: Path, test_root: Path) -> None:
    """Clean an exact, initially absent task key; never erase unknown contents."""
    canonical = game.resolve()
    if canonical == test_root.resolve() or not canonical.is_relative_to(test_root.resolve()):
        raise AssertionError("Registry cleanup target is outside its disposable test root")
    name = registration_name(canonical)
    snapshot = read_registration(name)
    if snapshot is None:
        return
    if snapshot:
        location = snapshot.get("InstallLocation", (None, None))[0]
        if not isinstance(location, str) or os.path.normcase(os.path.abspath(location)) != os.path.normcase(str(canonical)):
            raise AssertionError("Registry cleanup refused an entry belonging to another path")
        if not {name.lower() for name in snapshot} <= OWNED_NAMES:
            raise AssertionError("Registry cleanup refused unknown values")
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_PARENT + "\\" + name, 0,
                        winreg.KEY_READ | winreg.KEY_WRITE | winreg.KEY_WOW64_32KEY) as key:
        assert winreg.QueryInfoKey(key)[0] == 0, "Registry cleanup refused child keys"
        # These fields were path-bound and allowlisted above; do not delete any
        # newly arriving or unknown value while retiring an interrupted fixture.
        for value_name in snapshot:
            assert winreg.QueryValueEx(key, value_name) == snapshot[value_name]
            winreg.DeleteValue(key, value_name)
        assert winreg.QueryInfoKey(key)[:2] == (0, 0), "Registry cleanup refused changed contents"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_PARENT, 0,
                        winreg.KEY_WRITE | winreg.KEY_WOW64_32KEY) as parent:
        # Only isolated, initially absent task keys use whole-key cleanup.
        # Product code leaves the empty container in place.
        winreg.DeleteKey(parent, name)
