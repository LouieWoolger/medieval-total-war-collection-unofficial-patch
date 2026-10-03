/* Test-only allocator failure adapter over the complete real C engine.
   It changes allocation, never file or registry behavior. */
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
static void *lifecycle_allocate(size_t size);
static void lifecycle_free(void *pointer);
static BOOL WINAPI lifecycle_flush(HANDLE file);
#define malloc lifecycle_allocate
#define free lifecycle_free
#define FlushFileBuffers lifecycle_flush
#define wmain lifecycle_real_wmain
#include "../src/medieval_fix_patcher.c"
#undef wmain
#undef free
#undef malloc
#undef FlushFileBuffers

static size_t allocation_calls, live_allocations, fail_at;
static int persistent_failure, failure_triggered, trigger_exists;
static wchar_t trigger[32768];
static wchar_t flush_target[32768];
static BOOL WINAPI lifecycle_flush(HANDLE file) {
    wchar_t path[32768];
    DWORD length;
    if (flush_target[0]) {
        PatchPlatform platform;
        patch_platform_init(&platform);
        length = platform.final_path ? platform.final_path(file, path, 32768, 0) : 0;
        if (length > 4 && length < 32768 && !_wcsicmp(path + 4, flush_target)) {
            flush_target[0] = 0; failure_triggered = 1;
            SetLastError(ERROR_WRITE_FAULT); return FALSE;
        }
    }
    return FlushFileBuffers(file);
}
static void *lifecycle_allocate(size_t size) {
    void *p;
    int fire;
    ++allocation_calls;
    fire = fail_at && allocation_calls >= fail_at;
    if (trigger[0] && !failure_triggered) {
        int exists = GetFileAttributesW(trigger) != INVALID_FILE_ATTRIBUTES;
        if (exists == trigger_exists) fire = 1;
    }
    if (fire || (failure_triggered && persistent_failure)) {
        failure_triggered = 1;
        fail_at = 0; trigger[0] = 0;
        return NULL;
    }
    p = malloc(size);
    if (p) ++live_allocations;
    return p;
}
static void lifecycle_free(void *pointer) {
    if (pointer) { --live_allocations; free(pointer); }
}
int wmain(int argc, wchar_t **argv) {
    MedievalOptions options = {0};
    MedievalOutcome outcome = {0};
    PatchError error = {0};
    DWORD before, after, before_rpc, after_rpc;
    UUID warmup;
    HKEY registry_warmup;
    int ok, exit_code;
    if (argc != 7) return 91;
    if (!wcscmp(argv[1], L"NoStdout")) {
        wchar_t *forward[] = {argv[0], L"--operation", L"Restore", L"--target", argv[2],
                              L"--payload", argv[3], L"--version", L"1.0.0"};
        SetStdHandle(STD_OUTPUT_HANDLE, NULL);
        return lifecycle_real_wmain(9, forward);
    }
    options.operation = !wcscmp(argv[1], L"Install") ? "Install" : "Restore";
    options.target = argv[2]; options.payload = argv[3]; options.uninstaller = argv[4];
    options.version = "1.0.0"; options.output = "Json";
    persistent_failure = !wcscmp(argv[6], L"persistent");
    if (!wcscmp(argv[5], L"journal")) {
        _snwprintf(trigger, 32768, L"%ls\\.unofficial-medieval-total-war-patch-transaction\\journal.json", argv[2]);
        trigger_exists = 1;
    } else if (!wcscmp(argv[5], L"runtime")) {
        _snwprintf(trigger, 32768, L"%ls\\dgVoodoo_D3D9.dll", argv[2]);
        trigger_exists = !strcmp(options.operation, "Install");
    } else if (!wcscmp(argv[5], L"removed")) {
        _snwprintf(trigger, 32768, L"%ls\\Uninstall Unofficial Medieval Patch.exe", argv[2]);
        trigger_exists = 0;
    } else if (!wcscmp(argv[5], L"flush")) {
        _snwprintf(flush_target, 32768, L"%ls\\dgVoodoo.conf", argv[2]);
    } else fail_at = (size_t)_wtoi(argv[5]);
    /* UuidCreate initializes process-lifetime RPC worker/event state. Record
       that independent warmup explicitly, then measure lifecycle ownership. */
    GetProcessHandleCount(GetCurrentProcess(), &before_rpc);
    UuidCreate(&warmup);
    GetProcessHandleCount(GetCurrentProcess(), &after_rpc);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &registry_warmup) == ERROR_SUCCESS)
        RegCloseKey(registry_warmup);
    GetProcessHandleCount(GetCurrentProcess(), &before);
    ok = medieval_run(&options, &outcome, &error);
    exit_code = outcome.exit_code;
    if (!wcscmp(argv[5], L"flush") && outcome.result) {
        char *json;
        size_t length;
        PatchError output = {0};
        if (json_write_canonical(&outcome.document, &json, &length, &output)) printf("%s\n", json);
    }
    medieval_outcome_close(&outcome);
    GetProcessHandleCount(GetCurrentProcess(), &after);
    printf("{\"ok\":%d,\"exit\":%d,\"code\":\"%s\",\"triggered\":%d,\"allocations\":%lu,\"live\":%lu,\"handles_before\":%lu,\"handles_after\":%lu,\"handles_before_rpc\":%lu,\"handles_after_rpc\":%lu}\n",
           ok, exit_code, error.code ? error.code : "", failure_triggered, (unsigned long)allocation_calls,
           (unsigned long)live_allocations, (unsigned long)before, (unsigned long)after, (unsigned long)before_rpc, (unsigned long)after_rpc);
    return 0;
}
