#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

typedef DWORD (WINAPI *mtw_status_fn)(DWORD);
typedef IDirect3D9 *(WINAPI *mtw_create9_fn)(UINT);

int wmain(int argc, wchar_t **argv) {
    HMODULE proxy;
    mtw_status_fn status;
    mtw_create9_fn create9;
    IDirect3D9 *api;
    if (argc != 2) return 2;
    proxy = LoadLibraryW(argv[1]);
    if (!proxy) return 3;
    status = (mtw_status_fn)GetProcAddress(proxy, "MtwMovieFixStatus");
    create9 = (mtw_create9_fn)GetProcAddress(proxy, "Direct3DCreate9");
    if (!status || !create9) return 4;
    /* This loader is deliberately an unsupported EXE. It must not patch any
       addresses, even though it can forward D3D9 normally. */
    if (status(0) || status(4)) return 5;
    api = create9(D3D_SDK_VERSION);
    if (!api) return 6;
    api->lpVtbl->Release(api);
    if (status(0) || status(4)) return 7;
    FreeLibrary(proxy);
    puts("movie proxy unsupported-exe guard and system forwarding passed");
    return 0;
}
