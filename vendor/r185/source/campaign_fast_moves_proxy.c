/* Feature-only D3D9 loader: forward to Windows' own D3D9, then install only
 * the independently selected hooks. No dgVoodoo2 backend is loaded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <string.h>
#include "credits_flash_fix.h"

#ifndef MTW_XP_RUNTIME
#define MTW_XP_RUNTIME 0
#endif

#ifndef MTW_ENABLE_FAST_MOVES
#define MTW_ENABLE_FAST_MOVES 1
#endif
#ifndef MTW_ENABLE_ZOOM
#define MTW_ENABLE_ZOOM 0
#endif
#ifndef MTW_ENABLE_BATTLE_ICON_FIX
#define MTW_ENABLE_BATTLE_ICON_FIX 0
#endif
#ifndef MTW_ENABLE_CREDITS_FLASH_FIX
#define MTW_ENABLE_CREDITS_FLASH_FIX 0
#endif
#if (MTW_ENABLE_ZOOM != 0 && MTW_ENABLE_ZOOM != 1) || \
    (MTW_ENABLE_FAST_MOVES != 0 && MTW_ENABLE_FAST_MOVES != 1) || \
    (MTW_ENABLE_BATTLE_ICON_FIX != 0 && MTW_ENABLE_BATTLE_ICON_FIX != 1) || \
    (MTW_ENABLE_CREDITS_FLASH_FIX != 0 && MTW_ENABLE_CREDITS_FLASH_FIX != 1)
#error Feature-only proxy choices must be zero or one.
#endif
#if !MTW_ENABLE_FAST_MOVES && !MTW_ENABLE_ZOOM && !MTW_ENABLE_BATTLE_ICON_FIX && \
    !MTW_ENABLE_CREDITS_FLASH_FIX
#error Feature-only proxy must provide at least one selected fix.
#endif

#if MTW_ENABLE_FAST_MOVES || MTW_ENABLE_ZOOM
int mtw_campaign_fast_moves_install(void);
#endif
#if MTW_ENABLE_BATTLE_ICON_FIX
#include "battle_icon_atlas_fix.h"
#endif

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version) {
    typedef IDirect3D9 *(WINAPI *factory_fn)(UINT);
    static HMODULE system_d3d9;
    static factory_fn factory;
    wchar_t path[MAX_PATH];
    UINT length;
    IDirect3D9 *result;
    if (!factory) {
        length = GetSystemDirectoryW(path, MAX_PATH);
        if (!length || length >= MAX_PATH ||
            length + sizeof(L"\\d3d9.dll") / sizeof(wchar_t) >= MAX_PATH) return NULL;
#if MTW_XP_RUNTIME
        /* The length check above includes the complete constant and its NUL.
         * Avoid a secure-CRT import absent from XP's system msvcrt.dll. */
        memcpy(path + length, L"\\d3d9.dll", sizeof(L"\\d3d9.dll"));
#else
        if (wcscat_s(path, MAX_PATH, L"\\d3d9.dll")) return NULL;
#endif
        system_d3d9 = LoadLibraryW(path);
        if (!system_d3d9) return NULL;
        factory = (factory_fn)GetProcAddress(system_d3d9, "Direct3DCreate9");
        if (!factory) return NULL;
    }
    result = factory(sdk_version);
    if (result) {
#if MTW_ENABLE_CREDITS_FLASH_FIX
        (void)mtw_credits_flash_install_once();
#endif
#if MTW_ENABLE_FAST_MOVES || MTW_ENABLE_ZOOM
        (void)mtw_campaign_fast_moves_install();
#endif
#if MTW_ENABLE_BATTLE_ICON_FIX
        (void)mtw_battle_icon_atlas_install_once();
#endif
    }
    return result;
}
