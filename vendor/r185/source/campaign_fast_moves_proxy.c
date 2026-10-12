/* Feature-only D3D9 loader: forward to Windows' own D3D9, then install the
 * campaign hooks. This keeps dgVoodoo2 out of a feature-only installation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

int mtw_campaign_fast_moves_install(void);

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
        if (wcscat_s(path, MAX_PATH, L"\\d3d9.dll")) return NULL;
        system_d3d9 = LoadLibraryW(path);
        if (!system_d3d9) return NULL;
        factory = (factory_fn)GetProcAddress(system_d3d9, "Direct3DCreate9");
        if (!factory) return NULL;
    }
    result = factory(sdk_version);
    if (result) (void)mtw_campaign_fast_moves_install();
    return result;
}
