#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "frontend_fix.h"

IDirect3D9 *WINAPI mtw_accepted_dust_direct3dcreate9(UINT sdk_version);
int mtw_dust_simulation_hook_installed(void);
int mtw_dust_graphics_wrap_d3d9(IDirect3D9 *d3d9);
#if MTW_ENABLE_FAST_MOVES
int mtw_campaign_fast_moves_install(void);
#endif

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version) {
    frontend_fix_prepare_backend_tracking();
    IDirect3D9 *result = mtw_accepted_dust_direct3dcreate9(sdk_version);
    if (result != NULL) {
        if (mtw_dust_simulation_hook_installed())
            (void)mtw_dust_graphics_wrap_d3d9(result);
        frontend_fix_install_once();
#if MTW_ENABLE_FAST_MOVES
        (void)mtw_campaign_fast_moves_install();
#endif
    }
    return result;
}
