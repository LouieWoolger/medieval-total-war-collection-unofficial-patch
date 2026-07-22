#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Fn)(UINT);

int main(void) {
    HMODULE module = LoadLibraryW(L"D3D9.dll");
    Direct3DCreate9Fn create9;
    IDirect3D9 *d3d9;
    if (module == NULL) return 10;
    create9 = (Direct3DCreate9Fn)(void *)GetProcAddress(module, "Direct3DCreate9");
    if (create9 == NULL) return 11;
    d3d9 = create9(D3D_SDK_VERSION);
    if (d3d9 == NULL) return 12;
    IDirect3D9_Release(d3d9);
    FreeLibrary(module);
    return 0;
}
