from pathlib import Path

import pytest

import registry_isolation as isolation


def test_cleanup_rejects_paths_outside_disposable_root(tmp_path: Path, monkeypatch):
    def forbidden_read(name):
        pytest.fail("Must refuse before inspecting the registry")

    monkeypatch.setattr(isolation, "read_registration", forbidden_read)
    with pytest.raises(AssertionError, match="outside"):
        isolation.remove_test_registration(tmp_path.parent / "real game", tmp_path)
    with pytest.raises(AssertionError, match="outside"):
        isolation.remove_test_registration(tmp_path, tmp_path)


def test_cleanup_preserves_entry_with_different_owner(tmp_path: Path, monkeypatch):
    monkeypatch.setattr(isolation, "read_registration", lambda name: {
        "InstallLocation": (str(tmp_path / "other game"), isolation.winreg.REG_SZ),
    })
    with pytest.raises(AssertionError, match="another path"):
        isolation.remove_test_registration(tmp_path / "disposable game", tmp_path)


def test_cleanup_deletes_only_exact_disposable_key(tmp_path: Path):
    game = tmp_path / "disposable game"
    name = isolation.registration_name(game)
    assert isolation.read_registration(name) is None
    path = isolation.UNINSTALL_PARENT + "\\" + name
    with isolation.winreg.CreateKeyEx(isolation.winreg.HKEY_CURRENT_USER, path, 0,
                                     isolation.winreg.KEY_WRITE | isolation.winreg.KEY_WOW64_32KEY) as key:
        isolation.winreg.SetValueEx(key, "InstallLocation", 0, isolation.winreg.REG_SZ, str(game))
    isolation.remove_test_registration(game, tmp_path)
    assert isolation.read_registration(name) is None
    assert name not in isolation.LEGACY_NAMES


@pytest.mark.parametrize("contents", ["empty", "value", "subkey"])
def test_empty_container_observation_and_cleanup_preserve_unknown_state(tmp_path: Path, contents):
    game = tmp_path / "disposable game"
    name = isolation.registration_name(game)
    assert isolation.read_registration(name) is None
    path = isolation.UNINSTALL_PARENT + "\\" + name
    winreg = isolation.winreg
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, path, 0,
                           winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_32KEY) as key:
        if contents == "value":
            winreg.SetValueEx(key, "Personal", 0, winreg.REG_SZ, "keep")
        elif contents == "subkey":
            with winreg.CreateKeyEx(key, "PersonalChild") as child:
                winreg.SetValueEx(child, "Personal", 0, winreg.REG_SZ, "keep")
    try:
        if contents == "empty":
            assert isolation.read_registration(name) == {}
            isolation.assert_empty_registration(name)
            isolation.remove_test_registration(game, tmp_path)
            assert isolation.read_registration(name) is None
        else:
            with pytest.raises(AssertionError):
                isolation.assert_empty_registration(name)
            with pytest.raises(AssertionError):
                isolation.remove_test_registration(game, tmp_path)
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0,
                                winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_32KEY) as key:
                if contents == "value":
                    assert winreg.QueryValueEx(key, "Personal") == ("keep", winreg.REG_SZ)
                    winreg.DeleteValue(key, "Personal")
                else:
                    with winreg.OpenKey(key, "PersonalChild", 0, winreg.KEY_ALL_ACCESS) as child:
                        assert winreg.QueryValueEx(child, "Personal") == ("keep", winreg.REG_SZ)
                        winreg.DeleteValue(child, "Personal")
                    winreg.DeleteKey(key, "PersonalChild")
    finally:
        isolation.remove_test_registration(game, tmp_path)
