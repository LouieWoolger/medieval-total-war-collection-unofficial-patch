#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "frontend_fix.h"
#ifndef MTW_ENABLE_CREDITS_FLASH_FIX
#define MTW_ENABLE_CREDITS_FLASH_FIX 0
#endif
#if MTW_ENABLE_CREDITS_FLASH_FIX != 0 && MTW_ENABLE_CREDITS_FLASH_FIX != 1
#error Credits proxy choice must be zero or one.
#endif
#if MTW_ENABLE_CREDITS_FLASH_FIX
#include "credits_flash_fix.h"
#endif

IDirect3D9 *WINAPI mtw_accepted_dust_direct3dcreate9(UINT sdk_version);
int mtw_dust_simulation_hook_installed(void);
int mtw_dust_graphics_wrap_d3d9(IDirect3D9 *d3d9);
#ifndef MTW_ENABLE_ZOOM
#define MTW_ENABLE_ZOOM 0
#endif
#if MTW_ENABLE_FAST_MOVES || MTW_ENABLE_ZOOM
int mtw_campaign_fast_moves_install(void);
#endif

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version) {
    frontend_fix_prepare_backend_tracking();
    IDirect3D9 *result = mtw_accepted_dust_direct3dcreate9(sdk_version);
    if (result != NULL) {
#if MTW_ENABLE_CREDITS_FLASH_FIX
        (void)mtw_credits_flash_install_once();
#endif
        if (mtw_dust_simulation_hook_installed())
            (void)mtw_dust_graphics_wrap_d3d9(result);
        frontend_fix_install_once();
#if MTW_ENABLE_FAST_MOVES || MTW_ENABLE_ZOOM
        (void)mtw_campaign_fast_moves_install();
#endif
    }
    return result;
}
