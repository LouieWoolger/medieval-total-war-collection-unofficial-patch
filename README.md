# Unofficial Medieval: Total War Collection Patch
[![Downloads](https://img.shields.io/github/downloads/LouieWoolger/medieval-total-war-collection-unofficial-patch/total?style=for-the-badge)](https://github.com/LouieWoolger/medieval-total-war-collection-unofficial-patch/releases)
[![Release](https://img.shields.io/github/v/release/LouieWoolger/medieval-total-war-collection-unofficial-patch?style=for-the-badge)](https://github.com/LouieWoolger/medieval-total-war-collection-unofficial-patch/releases/latest)
[![Discord](https://img.shields.io/discord/1505490825889579018?style=for-the-badge&logo=discord&label=Discord&color=5865F2)](https://discord.gg/zKbDADqWRC)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-FF5F5F?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/louiewoolger)

An installer for Medieval: Total War Collection on GOG and Steam. It patches your existing game folder and lets you choose the fixes you want.

The installer looks for `Medieval_TW.exe`, makes a backup when it needs to change a file, and applies the selected options to your own install.

## Included Fixes

**Recommended**:

- Terrain Movement Fix - installs dgVoodoo2 to fix click-to-move and drag-formation issues on modern Windows systems.
- Campaign Map Scroll Fix - Fixes campaign-map scrolling speed at high frame rates.
- Pre-battle Screen Crash Fix - Fixes a crash that can occur on the pre-battle screen during the campaign.

## Requirements

- Windows XP through Windows 11
- Medieval: Total War Collection from GOG or Steam
- A game folder containing `Medieval_TW.exe`

The Terrain Movement Fix requires Windows 7 or later.

## Usage

Download the latest installer from the [Releases](https://github.com/LouieWoolger/medieval-total-war-collection-unofficial-patch/releases/latest) page.

Run:

```text
Unofficial Medieval Total War Collection Patch.exe
```

The installer will try to find your Steam or GOG install automatically. If it picks the wrong folder, browse to the folder that contains `Medieval_TW.exe`.

## Uninstalling

Run `Uninstall Unofficial Medieval Patch.exe` in your game folder.

Uninstalling the patch will not delete your game or saved games.

## Building from Source

Build requirements:

- Python 3.9 or newer with `pytest`, `Pillow`, and `pefile`
- NSIS 3.11 or newer
- 7-Zip
- w64devkit, or another MinGW-w64 toolchain that provides `i686-w64-mingw32` GCC and `windres.exe`
- Visual Studio 2026 Build Tools with x86 C++ tools, plus LLVM `clang-cl`

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The installer is written to `dist\Unofficial Medieval Total War Collection Patch.exe`.
