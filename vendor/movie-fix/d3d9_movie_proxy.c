#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <string.h>

#include "movie_backing_fix.h"

typedef IDirect3D9 *(WINAPI *Create9Fn)(UINT);
static INIT_ONCE system_once = INIT_ONCE_STATIC_INIT;
static HMODULE system_d3d9;
static Create9Fn real_create9;

static BOOL CALLBACK load_system_d3d9(PINIT_ONCE once, PVOID parameter,
                                      PVOID *context) {
    WCHAR path[MAX_PATH];
    UINT length;
    FARPROC found;
    (void)once; (void)parameter; (void)context;
    length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length > MAX_PATH - 11) return TRUE;
    wcscat_s(path, MAX_PATH, L"\\d3d9.dll");
    system_d3d9 = LoadLibraryW(path);
    if (!system_d3d9) return TRUE;
    found = GetProcAddress(system_d3d9, "Direct3DCreate9");
    memcpy(&real_create9, &found, sizeof found);
    return TRUE;
}

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version) {
    (void)mtw_movie_backing_install_once();
    InitOnceExecuteOnce(&system_once, load_system_d3d9, NULL, NULL);
    return real_create9 ? real_create9(sdk_version) : NULL;
}

DWORD WINAPI MtwMovieFixStatus(DWORD field) {
    return mtw_movie_backing_status(field);
}

static DWORD WINAPI install_worker(void *parameter) {
    (void)parameter;
    (void)mtw_movie_backing_install_once();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved) {
    HANDLE worker;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        worker = CreateThread(NULL, 0, install_worker, NULL, 0, NULL);
        if (worker) CloseHandle(worker);
    }
    return TRUE;
}
