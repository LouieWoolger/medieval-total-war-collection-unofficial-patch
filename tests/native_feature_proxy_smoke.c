#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef IDirect3D9 *(WINAPI *factory_fn)(UINT);
typedef struct {
    factory_fn factory;
    HANDLE start;
} factory_thread_parameters;

/* The fixture is an unsupported host. Its code must remain unchanged when a
 * feature factory forwards successfully and every game hook declines it. */
static uint32_t host_code_digest(void) {
    const unsigned char *base = (const unsigned char *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    uint32_t value = 2166136261u;
    unsigned int i;
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        size_t j;
        if (!(section[i].Characteristics & IMAGE_SCN_CNT_CODE)) continue;
        for (j = 0; j < section[i].Misc.VirtualSize; ++j)
            value = (value ^ base[section[i].VirtualAddress + j]) * 16777619u;
    }
    return value;
}

static DWORD WINAPI factory_thread(void *parameter) {
    const factory_thread_parameters *parameters = (const factory_thread_parameters *)parameter;
    factory_fn factory = parameters->factory;
    unsigned int i;
    if (WaitForSingleObject(parameters->start, 10000u) != WAIT_OBJECT_0) return 2u;
    for (i = 0; i < 4u; ++i) {
        IDirect3D9 *object = factory(D3D_SDK_VERSION);
        if (!object) return 1u;
        IDirect3D9_Release(object);
    }
    return 0u;
}

int wmain(int argc, wchar_t **argv) {
    HMODULE module, system;
    factory_fn factory, native;
    wchar_t path[MAX_PATH];
    UINT length;
    uint32_t before = host_code_digest();
    const UINT versions[] = {D3D_SDK_VERSION, 0u, 0xffffu, D3D_SDK_VERSION};
    HANDLE threads[4];
    factory_thread_parameters parameters;
    unsigned int i;
    if (argc != 2) return 9;
    module = LoadLibraryW(argv[1]);
    if (!module) return 10;
    factory = (factory_fn)(void *)GetProcAddress(module, "Direct3DCreate9");
    if (!factory) return 11;
    length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length + sizeof(L"\\d3d9.dll") / sizeof(wchar_t) > MAX_PATH) return 15;
    memcpy(path + length, L"\\d3d9.dll", sizeof(L"\\d3d9.dll"));
    system = LoadLibraryW(path);
    if (!system) return 16;
    native = (factory_fn)(void *)GetProcAddress(system, "Direct3DCreate9");
    if (!native) return 17;
    parameters.factory = factory;
    parameters.start = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!parameters.start) return 18;
    /* No proxy factory has run yet. Release all workers at the same gate so
     * loader caching and the XP credits once path face concurrent first calls. */
    for (i = 0; i < 4u; ++i) {
        threads[i] = CreateThread(NULL, 0u, factory_thread, &parameters, 0u, NULL);
        if (!threads[i]) return 18;
    }
    if (!SetEvent(parameters.start)) return 18;
    if (WaitForMultipleObjects(4u, threads, TRUE, 10000u) != WAIT_OBJECT_0) return 19;
    for (i = 0; i < 4u; ++i) {
        DWORD code;
        if (!GetExitCodeThread(threads[i], &code) || code) return 20;
        CloseHandle(threads[i]);
    }
    CloseHandle(parameters.start);
    for (i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        IDirect3D9 *actual = factory(versions[i]);
        IDirect3D9 *expected = native(versions[i]);
        if ((actual == NULL) != (expected == NULL)) return 12;
        if (versions[i] == D3D_SDK_VERSION && !actual) return 13;
        if (actual && IDirect3D9_GetAdapterCount(actual) != IDirect3D9_GetAdapterCount(expected))
            return 14;
        if (actual) IDirect3D9_Release(actual);
        if (expected) IDirect3D9_Release(expected);
    }
    if (host_code_digest() != before) return 21;
    FreeLibrary(system);
    if (!FreeLibrary(module)) return 22;
    puts("system-factory forwarding and unsupported-host guard passed");
    return 0;
}
