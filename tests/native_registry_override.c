/* Test-only C registry routing. Include before medieval_state.h. Four explicit
   roots preserve view intent; production registry names stay inside a UUID key.
   Task 4 may compile this with -DMTW_REGISTRY_ENGINE_SOURCE=\"../src/medieval_fix_patcher.c\".
   Without that macro this file is a routing adapter, not a placeholder engine. */
#include <string.h>
#include <wchar.h>
#include <windows.h>
struct PatchRegistry;
static void isolated_registry_customize(struct PatchRegistry *);
#define PATCH_REGISTRY_CUSTOMIZE(r) isolated_registry_customize(r)
#include "../src/patch_registry.h"
#undef PATCH_REGISTRY_CUSTOMIZE

static HKEY isolated_roots[2][2];
static unsigned isolated_routed[2][2];
static HKEY isolated_route(HKEY hive, REGSAM access) {
    int h = hive == HKEY_CURRENT_USER ? 0 : hive == HKEY_LOCAL_MACHINE ? 1 : -1;
    REGSAM mask = access & (KEY_WOW64_32KEY | KEY_WOW64_64KEY);
    int v = mask == KEY_WOW64_32KEY ? 0 : mask == KEY_WOW64_64KEY ? 1 : -1;
    if (h < 0 || v < 0 || !isolated_roots[h][v])
        return NULL;
    ++isolated_routed[h][v];
    return isolated_roots[h][v];
}
static LSTATUS WINAPI isolated_open(HKEY hive, LPCWSTR path, DWORD options, REGSAM access, PHKEY out) {
    HKEY root = isolated_route(hive, access);
    if (!root)
        return ERROR_ACCESS_DENIED;
    return RegOpenKeyExW(root, path, options, access & ~(KEY_WOW64_32KEY | KEY_WOW64_64KEY), out);
}
static LSTATUS WINAPI isolated_create(HKEY hive, LPCWSTR path, DWORD reserved, LPWSTR cls, DWORD options,
                                      REGSAM access, const LPSECURITY_ATTRIBUTES security, PHKEY out,
                                      LPDWORD disposition) {
    HKEY root = isolated_route(hive, access);
    if (!root)
        return ERROR_ACCESS_DENIED;
    return RegCreateKeyExW(root, path, reserved, cls, options, access & ~(KEY_WOW64_32KEY | KEY_WOW64_64KEY),
                           security, out, disposition);
}
static LSTATUS WINAPI isolated_delete_ex(HKEY hive, LPCWSTR path, REGSAM access, DWORD reserved) {
    HKEY root = isolated_route(hive, access);
    (void)reserved;
    if (!root)
        return ERROR_ACCESS_DENIED;
    return RegDeleteKeyW(root, path);
}
static LSTATUS WINAPI isolated_delete_default(HKEY hive, LPCWSTR path) {
    HKEY root = isolated_route(hive, KEY_WOW64_32KEY);
    if (!root)
        return ERROR_ACCESS_DENIED;
    return RegDeleteKeyW(root, path);
}
static void isolated_registry_customize(struct PatchRegistry *r) {
    r->open = isolated_open;
    r->create = isolated_create;
    r->delete_ex = isolated_delete_ex;
    r->delete_default = isolated_delete_default;
}
static void isolated_registry_close(void) {
    int h, v;
    for (h = 0; h < 2; ++h)
        for (v = 0; v < 2; ++v) {
            if (isolated_roots[h][v])
                RegCloseKey(isolated_roots[h][v]);
            isolated_roots[h][v] = NULL;
        }
}
static int isolated_registry_begin(const wchar_t *name) {
    const wchar_t *prefix = L"Software\\MedievalPatchLifecycleTests\\";
    size_t n = wcslen(prefix), i;
    int h, v;
    wchar_t path[200], sentinel[33];
    DWORD type, size;
    if (wcslen(name) != n + 32 || wcsncmp(name, prefix, n))
        return 0;
    for (i = n; i < n + 32; ++i)
        if (!((name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'f')))
            return 0;
    for (h = 0; h < 2; ++h)
        for (v = 0; v < 2; ++v) {
            _snwprintf(path, 200, L"%ls\\%ls%ls", name, h ? L"HKLM" : L"HKCU", v ? L"64" : L"32");
            if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ | KEY_WRITE | KEY_WOW64_64KEY,
                              &isolated_roots[h][v]) != ERROR_SUCCESS)
                goto failed;
            size = sizeof(sentinel);
            memset(sentinel, 0, sizeof(sentinel));
            if (RegQueryValueExW(isolated_roots[h][v], L"IsolationSentinel", NULL, &type, (BYTE *)sentinel,
                                 &size) != ERROR_SUCCESS ||
                type != REG_SZ || size != sizeof(sentinel) || wcscmp(name + n, sentinel))
                goto failed;
        }
    return 1;
failed:
    isolated_registry_close();
    return 0;
}
#ifdef MTW_REGISTRY_ENGINE_SOURCE
#define wmain native_engine_wmain
#include MTW_REGISTRY_ENGINE_SOURCE
#undef wmain
int wmain(int argc, wchar_t **argv) {
    int result;
    if (argc < 4 || !isolated_registry_begin(argv[1]))
        return 93;
    result = native_engine_wmain(argc - 1, argv + 1);
    if (isolated_routed[0][1])
        result = 94;
    isolated_registry_close();
    return result;
}
#endif
