/* Test-only API boundary adapter: terminate after the selected EXE is moved
   aside, before its replacement is published. All other engine work is real. */
#include <windows.h>
static FARPROC WINAPI interrupted_get_proc(HMODULE module, LPCSTR name);
#define GetProcAddress interrupted_get_proc
#define wmain interrupted_real_main
#include "../src/medieval_fix_patcher.c"
#undef wmain
#undef GetProcAddress

static PatchSetInfoFn original_set_info;
static wchar_t interrupted_exe[32768];
static BOOL WINAPI interrupted_set_info(HANDLE file, int kind, LPVOID data, DWORD size) {
    wchar_t path[32768];
    FARPROC proc = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFinalPathNameByHandleW");
    PatchFinalPathFn final_path = NULL;
    int selected = 0;
    BOOL result;
    memcpy(&final_path, &proc, sizeof final_path);
    if (kind == 3 && interrupted_exe[0] && final_path) {
        DWORD n = final_path(file, path, 32768, 0);
        selected = n > 4 && n < 32768 && !_wcsicmp(path + 4, interrupted_exe);
    }
    result = original_set_info(file, kind, data, size);
    if (result && selected) TerminateProcess(GetCurrentProcess(), 97);
    return result;
}
static FARPROC WINAPI interrupted_get_proc(HMODULE module, LPCSTR name) {
    FARPROC result = GetProcAddress(module, name);
    if (result && !strcmp(name, "SetFileInformationByHandle")) {
        PatchSetInfoFn replacement = interrupted_set_info;
        memcpy(&original_set_info, &result, sizeof original_set_info);
        memcpy(&result, &replacement, sizeof result);
    }
    return result;
}
int wmain(int argc, wchar_t **argv) {
    DWORD n = GetEnvironmentVariableW(L"MTW_TEST_CRASH_RENAME_EXE", interrupted_exe, 32768);
    if (!n || n >= 32768) return 91;
    return interrupted_real_main(argc, argv);
}
