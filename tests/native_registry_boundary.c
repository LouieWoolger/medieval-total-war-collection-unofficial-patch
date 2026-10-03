/* Inject through a real final-flush boundary, after owned values are removed.
   Every registry route is bound to the caller's sentinel-checked UUID sandbox. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static LSTATUS WINAPI boundary_flush(HKEY key);
#define RegFlushKey boundary_flush
#include "native_registry_override.c"
#undef RegFlushKey

static int injection, injected, flushes, deletes, values_at_injection = -1;
static const wchar_t marker[] = L"late unrelated state";
static LSTATUS WINAPI boundary_flush(HKEY key) {
    HKEY child = NULL;
    DWORD values;
    LSTATUS status;
    ++flushes;
    status = RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, &values, NULL, NULL, NULL, NULL);
    if (status != ERROR_SUCCESS) return status;
    values_at_injection = (int)values;
    if (injection == 1) {
        status = RegSetValueExW(key, L"PersonalValue", 0, REG_SZ, (const BYTE *)marker, sizeof marker);
    } else if (injection == 2) {
        status = RegCreateKeyExW(key, L"PersonalChild", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &child, NULL);
        if (status == ERROR_SUCCESS) {
            status = RegSetValueExW(child, L"Keep", 0, REG_SZ, (const BYTE *)marker, sizeof marker);
            if (status == ERROR_SUCCESS) status = RegFlushKey(child);
            RegCloseKey(child);
        }
    }
    if (status != ERROR_SUCCESS) return status;
    status = RegFlushKey(key);
    if (status == ERROR_SUCCESS && injection) injected = 1;
    return status;
}
static LSTATUS WINAPI boundary_delete_ex(HKEY hive, LPCWSTR path, REGSAM view, DWORD reserved) {
    ++deletes;
    return isolated_delete_ex(hive, path, view, reserved);
}
static LSTATUS WINAPI boundary_delete(HKEY hive, LPCWSTR path) {
    ++deletes;
    return isolated_delete_default(hive, path);
}
int wmain(int argc, wchar_t **argv) {
    const char absent[] = "{\"exists\":false,\"subkeys\":0,\"values\":[]}";
    PatchRegistry registry = {0};
    PatchError error = {0};
    JsonDocument doc = {0};
    int ok;
    if (argc != 4 || !isolated_registry_begin(argv[1])) return 93;
    if (!wcscmp(argv[2], L"value")) injection = 1;
    else if (!wcscmp(argv[2], L"subkey")) injection = 2;
    else if (wcscmp(argv[2], L"control")) return 92;
    ok = patch_registry_init(&registry, &error) && json_parse(absent, sizeof absent - 1, &doc, &error);
    registry.delete_ex = !wcscmp(argv[3], L"legacy") ? NULL : boundary_delete_ex;
    registry.delete_default = boundary_delete;
    if (ok) ok = patch_registry_set(&registry, "LocalMachine", "Registry32", "Task3", doc.root, &error);
    printf("{\"setter_success\":%d,\"injected\":%d,\"flushes\":%d,\"delete_calls\":%d,"
           "\"values_at_injection\":%d,\"error\":\"%s\"}\n",
           ok, injected, flushes, deletes, values_at_injection, error.code ? error.code : "");
    json_document_close(&doc);
    isolated_registry_close();
    return 0;
}
