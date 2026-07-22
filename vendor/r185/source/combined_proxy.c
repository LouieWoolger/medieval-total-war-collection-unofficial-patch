#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include "frontend_fix.h"

IDirect3D9 *WINAPI mtw_accepted_dust_direct3dcreate9(UINT sdk_version);

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version) {
    frontend_fix_prepare_backend_tracking();
    IDirect3D9 *result = mtw_accepted_dust_direct3dcreate9(sdk_version);
    if (result != NULL) {
        frontend_fix_install_once();
    }
    return result;
}
