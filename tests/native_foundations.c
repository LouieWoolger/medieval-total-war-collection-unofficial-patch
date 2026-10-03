/* C foundation regressions. All paths are disposable Python-owned fixtures. */
#include "patch_platform.h"

static BOOL WINAPI fixture_read(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI fixture_seek(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
#define ReadFile fixture_read
#define SetFilePointerEx fixture_seek
#include "patch_identity.h"
#undef ReadFile
#undef SetFilePointerEx

static BOOL WINAPI fixture_write(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI fixture_flush(HANDLE);
#define WriteFile fixture_write
#define FlushFileBuffers fixture_flush
#include "patch_io.h"
#undef WriteFile
#undef FlushFileBuffers

#include <stdio.h>
#include <wchar.h>

static unsigned passed, failed;
static HANDLE fault_handle;
static int read_fault, restore_fault, write_fault, flush_fault;

static void check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) ++passed; else ++failed;
}

static int error_is(const PatchError *error, const char *code, DWORD win32)
{
    return error->code && strcmp(error->code, code) == 0 && error->win32 == win32;
}

static BOOL WINAPI fixture_read(HANDLE file, LPVOID buffer, DWORD size,
                               LPDWORD got, LPOVERLAPPED overlap)
{
    if (file == fault_handle && read_fault) {
        SetLastError(ERROR_CRC);
        return FALSE;
    }
    return ReadFile(file, buffer, size, got, overlap);
}

static BOOL WINAPI fixture_seek(HANDLE file, LARGE_INTEGER offset,
                               PLARGE_INTEGER result, DWORD origin)
{
    if (file == fault_handle && restore_fault && origin == FILE_BEGIN && offset.QuadPart == 7) {
        SetLastError(ERROR_SEEK);
        return FALSE;
    }
    return SetFilePointerEx(file, offset, result, origin);
}

static BOOL WINAPI fixture_write(HANDLE file, LPCVOID buffer, DWORD size,
                                LPDWORD written, LPOVERLAPPED overlap)
{
    if (file == fault_handle && write_fault == 1) {
        *written = 0;
        return TRUE;
    }
    if (file == fault_handle && write_fault == 2 && size > 2) size = 2;
    return WriteFile(file, buffer, size, written, overlap);
}

static BOOL WINAPI fixture_flush(HANDLE file)
{
    if (file == fault_handle && flush_fault) {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return FlushFileBuffers(file);
}

typedef struct { unsigned calls, fail_at, live; } AllocatorState;

static void *fixture_allocate(void *user, size_t size)
{
    AllocatorState *state = (AllocatorState *)user;
    void *value;
    ++state->calls;
    if (state->calls == state->fail_at) return NULL;
    value = malloc(size);
    if (value) ++state->live;
    return value;
}

static void fixture_deallocate(void *user, void *value)
{
    AllocatorState *state = (AllocatorState *)user;
    if (value) { --state->live; free(value); }
    SetLastError(ERROR_INVALID_DATA); /* Cleanup must not replace the original error. */
}

static void fixture_allocator(PatchContext *context, AllocatorState *state)
{
    context->allocator.user = state;
    context->allocator.allocate = fixture_allocate;
    context->allocator.deallocate = fixture_deallocate;
}

static int is_closed(HANDLE handle)
{
    DWORD flags;
    return !GetHandleInformation(handle, &flags) && GetLastError() == ERROR_INVALID_HANDLE;
}

static DWORD close_with_failure(void *value)
{
    if (!CloseHandle((HANDLE)value)) return GetLastError();
    SetLastError(ERROR_INVALID_DATA);
    return ERROR_ACCESS_DENIED;
}

static DWORD WINAPI wait_for_mutex(void *value)
{
    DWORD result = WaitForSingleObject((HANDLE)value, 100);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) ReleaseMutex((HANDLE)value);
    return result;
}

static void test_mutex_cleanup(void)
{
    unsigned fail_at;
    for (fail_at = 0; fail_at <= 1; ++fail_at) {
        PatchContext context = {0};
        AllocatorState state = {0};
        HANDLE mutex = CreateMutexW(NULL, TRUE, NULL), peer = NULL, thread;
        DWORD result = WAIT_FAILED;
        int adopted, closed;
        check(mutex != NULL && DuplicateHandle(GetCurrentProcess(), mutex, GetCurrentProcess(),
              &peer, 0, FALSE, DUPLICATE_SAME_ACCESS), "owned mutex fixture created");
        if (!peer) { if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); } continue; }
        state.fail_at = fail_at;
        fixture_allocator(&context, &state);
        adopted = patch_context_take_mutex(&context, mutex, NULL);
        patch_context_close(&context);
        patch_context_close(&context);
        closed = is_closed(mutex); /* Check before CreateThread can reuse the handle value. */
        thread = CreateThread(NULL, 0, wait_for_mutex, peer, 0, NULL);
        if (thread && WaitForSingleObject(thread, 2000) == WAIT_OBJECT_0)
            GetExitCodeThread(thread, &result);
        check(adopted == (fail_at == 0) && !state.live && closed && result == WAIT_OBJECT_0,
              fail_at ? "failed mutex adoption releases lock and handle" :
                        "context cleanup releases owned mutex before closing handle");
        if (thread) CloseHandle(thread);
        CloseHandle(peer);
    }
}

static void test_sizes_and_cleanup(void)
{
    PatchContext context = {0};
    AllocatorState state = {0};
    void *first = NULL, *second = NULL;
    size_t size = 99;
    HANDLE event;
    PatchResource *resource = NULL;
    check(patch_size_add(3, 7, &size) && size == 10, "checked addition value");
    check(!patch_size_add(SIZE_MAX, 1, &size) && size == 0, "checked addition rejects overflow");
    check(patch_size_multiply(7, 9, &size) && size == 63, "checked multiplication value");
    check(!patch_size_multiply(SIZE_MAX / 2 + 1, 2, &size) && size == 0,
          "checked multiplication rejects overflow");
    check(patch_size_multiply(0, SIZE_MAX, &size) && size == 0, "zero sized arithmetic");
    check(!patch_size_add(1, 2, NULL) && !patch_size_multiply(1, 2, NULL),
          "missing size outputs rejected");
    patch_context_close(&context);
    patch_context_close(&context);
    patch_context_close(NULL);
    check(!context.resources && !context.bytes_owned && !context.error.code,
          "zero context repeated cleanup");
    fixture_allocator(&context, &state);
    check(!patch_context_alloc(&context, SIZE_MAX, 2, &first) && !first && !state.calls &&
          error_is(&context.error, "allocation_overflow", ERROR_ARITHMETIC_OVERFLOW),
          "array overflow rejected before allocator");
    patch_context_close(&context);
    memset(&context, 0, sizeof(context));
    fixture_allocator(&context, &state);
    check(!patch_context_alloc(&context, 1, SIZE_MAX, &first) && !first && !state.calls,
          "allocation metadata addition overflow rejected");
    memset(&context, 0, sizeof(context));
    context.memory_limit = 1024;
    check(!patch_context_alloc(&context, 1, 1025, &first) &&
          error_is(&context.error, "allocation_limit", ERROR_NOT_ENOUGH_MEMORY),
          "context memory budget enforced");
    memset(&context, 0, sizeof(context));
    fixture_allocator(&context, &state);
    check(patch_context_alloc(&context, 0, SIZE_MAX, &first) && first == NULL && state.calls == 0,
          "zero allocation owns nothing");
    check(patch_context_alloc(&context, 17, 1, &first), "allocation succeeds");
    if (first) {
        unsigned char zero[17] = {0};
        check(!memcmp(first, zero, sizeof(zero)), "new allocations zero initialized");
        memcpy(first, "retained", 9);
        state.fail_at = state.calls + 1;
        second = first;
        check(!patch_context_resize(&context, &first, 100, 1) && first == second &&
              !memcmp(first, "retained", 9) && state.live == 1,
              "failed growth keeps original allocation");
        state.fail_at = 0;
        check(patch_context_resize(&context, &first, 33, 1) && !memcmp(first, "retained", 9) &&
              ((unsigned char *)first)[32] == 0 && state.live == 1,
              "growth preserves bytes and zeros new bytes");
        check(patch_context_resize(&context, &first, 4, 1) && !memcmp(first, "reta", 4),
              "shrink preserves prefix");
        check(patch_context_resize(&context, &first, 0, 1) && !first && state.live == 0,
              "zero resize releases allocation");
    }
    patch_context_close(&context);
    memset(&context, 0, sizeof(context));
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    check(event != NULL && patch_context_take(&context, event, close_with_failure, &resource),
          "custom cleanup resource acquired");
    if (resource) {
        check(!patch_context_release(&context, resource) && is_closed(event) &&
              error_is(&context.error, "io_error", ERROR_ACCESS_DENIED),
              "cleanup failure captured after resource release");
    }
    patch_context_close(&context);
    memset(&context, 0, sizeof(context));
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    check(patch_context_take(&context, event, close_with_failure, NULL), "failing cleanup adopted");
    patch_error_set(&context.error, "original_error", "Keep this first error.", ERROR_CRC);
    SetLastError(ERROR_CRC);
    patch_context_close(&context);
    check(error_is(&context.error, "original_error", ERROR_CRC) && GetLastError() == ERROR_CRC &&
          is_closed(event), "cleanup preserves first error and Win32 status");
    patch_context_close(&context);
    check(error_is(&context.error, "original_error", ERROR_CRC), "repeated cleanup preserves error");
    memset(&context, 0, sizeof(context));
    context.memory_limit = 1024;
    check(patch_context_alloc(&context, 700, 1, &first) &&
          !patch_context_alloc(&context, 700, 1, &second) && first && !second &&
          error_is(&context.error, "allocation_limit", ERROR_NOT_ENOUGH_MEMORY),
          "aggregate allocation budget enforced");
    patch_context_close(&context);
    memset(&context, 0, sizeof(context));
    context.allocator.allocate = fixture_allocate;
    check(!patch_context_alloc(&context, 8, 1, &first) && !first &&
          error_is(&context.error, "invalid_argument", ERROR_INVALID_PARAMETER),
          "incomplete allocator rejected before callback");
}

static void test_allocation_failures(const wchar_t *input)
{
    unsigned fail_at;
    for (fail_at = 0; fail_at <= 6; ++fail_at) {
        PatchContext context = {0};
        AllocatorState state = {0};
        HANDLE handles[2] = {NULL, NULL};
        void *first = NULL, *second = NULL;
        PatchDiagnostics diagnostics = {0};
        char name[96];
        int completed = 0;
        state.fail_at = fail_at;
        fixture_allocator(&context, &state);
        if (!patch_context_alloc(&context, 31, 1, &first)) goto cleanup;
        handles[0] = CreateFileW(input, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
        if (!patch_context_take_handle(&context, handles[0], NULL)) goto cleanup;
        if (!patch_context_alloc(&context, 128, 1, &second)) goto cleanup;
        handles[1] = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!patch_context_take_handle(&context, handles[1], NULL)) goto cleanup;
        if (!patch_context_resize(&context, &first, 257, 1)) goto cleanup;
        patch_diagnostics_init(&diagnostics, &context, NULL, NULL);
        if (!patch_diagnostics_printf(&diagnostics, "bounded acquisition %u", fail_at)) goto cleanup;
        completed = 1;
cleanup:
        patch_context_close(&context);
        patch_context_close(&context);
        snprintf(name, sizeof(name), "acquisition sequence failure index %u releases all resources", fail_at);
        check(completed == (fail_at == 0) && state.calls == (fail_at ? fail_at : 6) &&
              state.live == 0 && context.bytes_owned == 0 && !context.resources &&
              (fail_at ? error_is(&context.error, "allocation_failed", ERROR_NOT_ENOUGH_MEMORY) :
                         context.error.code == NULL) &&
              (!handles[0] || is_closed(handles[0])) && (!handles[1] || is_closed(handles[1])), name);
    }
}

static void test_sha_vectors(void)
{
    static const struct { const char *text, *digest; } vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}
    };
    size_t i, j;
    PiSha256 sha;
    char digest[65];
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        pi_sha256_init(&sha);
        for (j = 0; vectors[i].text[j]; ++j)
            pi_sha256_update(&sha, (const unsigned char *)vectors[i].text + j, 1);
        pi_sha256_final(&sha, digest);
        check(strcmp(digest, vectors[i].digest) == 0, "SHA256 standard streamed vector");
    }
    pi_sha256_init(&sha);
    for (i = 0; i < 1000000; ++i) pi_sha256_update(&sha, (const unsigned char *)"a", 1);
    pi_sha256_final(&sha, digest);
    check(strcmp(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0,
          "SHA256 million a vector");
    {
        PatchContext context = {0};
        check(patch_sha256_bytes(&context, "abc", 3, digest) &&
              !strcmp(digest, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"),
              "canonical SHA256 uses uppercase");
        check(patch_sha256_bytes(&context, NULL, 0, digest) &&
              !strcmp(digest, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"),
              "empty SHA256 accepts null buffer");
        check(!patch_sha256_bytes(&context, NULL, 3, digest) && !digest[0],
              "SHA256 invalid buffer publishes no digest");
    }
}

static void test_sha_failures(const wchar_t *input)
{
    PatchContext context = {0};
    HANDLE file = CreateFileW(input, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER offset, actual;
    char digest[65] = "stale";
    offset.QuadPart = 7;
    check(file != INVALID_HANDLE_VALUE && SetFilePointerEx(file, offset, NULL, FILE_BEGIN),
          "hash fixture pinned at original offset");
    fault_handle = file;
    read_fault = 1;
    check(!patch_sha256_handle(&context, file, digest) && !digest[0] &&
          error_is(&context.error, "io_error", ERROR_CRC), "SHA256 read failure captured");
    offset.QuadPart = 0;
    check(SetFilePointerEx(file, offset, &actual, FILE_CURRENT) && actual.QuadPart == 7,
          "SHA256 read failure restores borrowed handle position");
    restore_fault = 1;
    memset(&context, 0, sizeof(context));
    check(!patch_sha256_handle(&context, file, digest) &&
          error_is(&context.error, "io_error", ERROR_CRC), "SHA256 restore failure preserves read error");
    read_fault = 0;
    restore_fault = 0;
    offset.QuadPart = 7;
    SetFilePointerEx(file, offset, NULL, FILE_BEGIN);
    memset(&context, 0, sizeof(context));
    restore_fault = 1;
    check(!patch_sha256_handle(&context, file, digest) && !digest[0] &&
          error_is(&context.error, "io_error", ERROR_SEEK), "SHA256 restore failure publishes no digest");
    restore_fault = 0;
    fault_handle = NULL;
    CloseHandle(file);
}

static int file_is(HANDLE file, const char *expected)
{
    char buffer[128];
    LARGE_INTEGER offset;
    DWORD got;
    offset.QuadPart = 0;
    return SetFilePointerEx(file, offset, NULL, FILE_BEGIN) &&
           ReadFile(file, buffer, sizeof(buffer), &got, NULL) &&
           got == strlen(expected) && !memcmp(buffer, expected, got);
}

static void test_diagnostics(const wchar_t *input, const wchar_t *log_path)
{
    PatchContext context = {0};
    PatchDiagnostics diagnostics = {0};
    HANDLE file = CreateFileW(log_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE read_only = CreateFileW(input, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
    check(file != INVALID_HANDLE_VALUE && read_only != INVALID_HANDLE_VALUE, "diagnostic fixtures opened");
    if (file == INVALID_HANDLE_VALUE || read_only == INVALID_HANDLE_VALUE) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (read_only != INVALID_HANDLE_VALUE) CloseHandle(read_only);
        return;
    }
    patch_diagnostics_init(&diagnostics, &context, NULL, file);
    fault_handle = file;
    write_fault = 2;
    check(patch_diagnostics_printf(&diagnostics, "%s %d\n", "UTF-8 \xc3\xa9 \xf0\x9f\x98\x80", 42) &&
          file_is(file, "UTF-8 \xc3\xa9 \xf0\x9f\x98\x80 42\n") && context.bytes_owned == 0,
          "diagnostic short writes complete and preserve UTF8 bytes");
    {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(file, zero, NULL, FILE_BEGIN);
        SetEndOfFile(file);
        patch_diagnostics_init(&diagnostics, &context, file, file);
        check(patch_diagnostics_emit(&diagnostics, "same sink", 9) && file_is(file, "same sink"),
              "shared output and log handle receives each diagnostic once");
    }
    write_fault = 1;
    check(!patch_diagnostics_emit(&diagnostics, "lost", 4) && diagnostics.failed &&
          error_is(&diagnostics.error, "diagnostic_write_failed", ERROR_WRITE_FAULT),
          "diagnostic zero progress rejected");
    write_fault = 0;
    memset(&context, 0, sizeof(context));
    patch_diagnostics_init(&diagnostics, &context, NULL, file);
    flush_fault = 1;
    check(!patch_diagnostics_emit(&diagnostics, "written-before-flush", 20) && diagnostics.failed &&
          error_is(&context.error, "diagnostic_write_failed", ERROR_WRITE_FAULT),
          "diagnostic durable flush failure captured");
    flush_fault = 0;
    fault_handle = NULL;
    memset(&context, 0, sizeof(context));
    patch_diagnostics_init(&diagnostics, &context, NULL, read_only);
    check(!patch_diagnostics_emit(&diagnostics, "preserve input", 14) && diagnostics.failed &&
          error_is(&diagnostics.error, "diagnostic_write_failed", ERROR_ACCESS_DENIED),
          "diagnostic real log write denial captured");
    memset(&context, 0, sizeof(context));
    patch_diagnostics_init(&diagnostics, &context, read_only, NULL);
    check(!patch_diagnostics_emit(&diagnostics, "output failure", 14) && diagnostics.failed &&
          error_is(&diagnostics.error, "diagnostic_write_failed", ERROR_ACCESS_DENIED),
          "diagnostic real output write denial captured");
    memset(&context, 0, sizeof(context));
    patch_error_set(&context.error, "operation_failed", "Keep recovery evidence.", ERROR_CRC);
    patch_diagnostics_init(&diagnostics, &context, NULL, read_only);
    check(!patch_diagnostics_emit(&diagnostics, "retry", 5) &&
          error_is(&context.error, "operation_failed", ERROR_CRC) &&
          error_is(&diagnostics.error, "diagnostic_write_failed", ERROR_ACCESS_DENIED),
          "diagnostic failure preserves earlier operation error");
    {
        AllocatorState state = {0};
        memset(&context, 0, sizeof(context));
        state.fail_at = 1;
        fixture_allocator(&context, &state);
        patch_error_set(&context.error, "operation_failed", "Keep recovery evidence.", ERROR_CRC);
        patch_diagnostics_init(&diagnostics, &context, NULL, file);
        check(!patch_diagnostics_printf(&diagnostics, "format after %s", "failure") &&
              diagnostics.failed && state.live == 0 &&
              error_is(&context.error, "operation_failed", ERROR_CRC) &&
              error_is(&diagnostics.error, "diagnostic_write_failed", ERROR_NOT_ENOUGH_MEMORY),
              "diagnostic OOM retains its own failure after earlier error");
        patch_context_close(&context);
    }
    CloseHandle(read_only);
    CloseHandle(file);
}

static int hash_file(const wchar_t *path)
{
    PatchContext context = {0};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    LARGE_INTEGER offset, actual;
    char digest[65];
    int ok = 0;
    if (!patch_context_take_handle(&context, file, NULL)) goto cleanup;
    offset.QuadPart = 7;
    if (!SetFilePointerEx(file, offset, NULL, FILE_BEGIN)) goto cleanup;
    if (!patch_sha256_handle(&context, file, digest)) goto cleanup;
    offset.QuadPart = 0;
    if (!SetFilePointerEx(file, offset, &actual, FILE_CURRENT) || actual.QuadPart != 7) goto cleanup;
    printf("%s\n", digest);
    ok = 1;
cleanup:
    patch_context_close(&context);
    return ok && !context.error.code ? 0 : 1;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 3 && !wcscmp(argv[1], L"--hash")) return hash_file(argv[2]);
    if (argc != 4 || wcscmp(argv[1], L"--suite")) return 2;
    test_sizes_and_cleanup();
    test_mutex_cleanup();
    test_allocation_failures(argv[2]);
    test_sha_vectors();
    test_sha_failures(argv[2]);
    test_diagnostics(argv[2], argv[3]);
    printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
