#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/primary_origin_guard_core.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct transition_case {
    uint32_t frontend_width;
    uint32_t frontend_height;
    uint32_t gameplay_width;
    uint32_t gameplay_height;
    uintptr_t locked_surface;
    uintptr_t current_surface;
    int expected;
} transition_case;

static void *guarded_test_page;
static volatile LONG guard_handler_count;

static LONG CALLBACK handle_test_guard_fault(
    EXCEPTION_POINTERS *exception) {
    EXCEPTION_RECORD *record;
    DWORD ignored;

    if (exception == NULL || exception->ExceptionRecord == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    record = exception->ExceptionRecord;
    if ((DWORD)record->ExceptionCode != 0x80000001u ||
        record->NumberParameters < 2u ||
        (void *)(uintptr_t)record->ExceptionInformation[1] !=
            guarded_test_page) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    InterlockedIncrement(&guard_handler_count);
    if (!VirtualProtect(
            guarded_test_page, 1u, PAGE_READWRITE, &ignored)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD protection_of(const void *address) {
    MEMORY_BASIC_INFORMATION info;

    memset(&info, 0, sizeof(info));
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) {
        return 0u;
    }
    return info.Protect;
}

static int test_exact_hook_signature(void) {
    static const unsigned char expected[
        MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE] = {
            0x85u, 0xC0u, 0x75u, 0xBFu, 0x89u, 0x46u, 0x1Cu
        };
    unsigned char corrupt[MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE];

    CHECK(MTW_PRIMARY_ORIGIN_GUARD_HOOK_RVA == 0x00B3F185u);
    CHECK(MTW_PRIMARY_ORIGIN_GUARD_FAILURE_RVA == 0x00B3F148u);
    CHECK(MTW_PRIMARY_ORIGIN_GUARD_SUCCESS_RVA == 0x00B3F18Cu);
    CHECK(MTW_PRIMARY_STAGING_SURFACE_RVA == 0x00B44098u);
    CHECK(mtw_primary_origin_guard_hook_size() ==
          MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE);
    CHECK(memcmp(mtw_primary_origin_guard_hook_expected(),
                 expected, sizeof(expected)) == 0);
    CHECK(mtw_primary_origin_guard_hook_supported(
        expected, sizeof(expected)));

    memcpy(corrupt, expected, sizeof(corrupt));
    corrupt[3] ^= 0x01u;
    CHECK(!mtw_primary_origin_guard_hook_supported(
        corrupt, sizeof(corrupt)));
    CHECK(!mtw_primary_origin_guard_hook_supported(
        expected, sizeof(expected) - 1u));
    CHECK(!mtw_primary_origin_guard_hook_supported(
        NULL, sizeof(expected)));
    return 0;
}

static int test_resolution_independent_identity_gate(void) {
    static const transition_case cases[] = {
        {800u, 600u, 800u, 600u, 0x11110000u, 0x11110000u, 1},
        {800u, 600u, 1024u, 768u, 0x22220000u, 0x22220000u, 1},
        {1024u, 768u, 800u, 600u, 0x33330000u, 0x33330000u, 1},
        {800u, 600u, 1920u, 1080u, 0x44440000u, 0x44440000u, 1},
        {800u, 600u, 800u, 600u, 0x55550000u, 0x66660000u, 0},
        {800u, 600u, 800u, 600u, 0u, 0u, 0}
    };
    unsigned char sentinel = 0u;
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const transition_case *item = &cases[index];

        /*
         * Dimensions deliberately do not enter the predicate. The same
         * identity rule covers equal-resolution and mixed-resolution
         * frontend/gameplay transitions.
         */
        CHECK(item->frontend_width != 0u);
        CHECK(item->frontend_height != 0u);
        CHECK(item->gameplay_width != 0u);
        CHECK(item->gameplay_height != 0u);
        CHECK(mtw_primary_origin_guard_should_touch(
                  0,
                  item->locked_surface,
                  item->current_surface,
                  &sentinel) == item->expected);
    }

    CHECK(!mtw_primary_origin_guard_should_touch(
        (int32_t)0x88760868u,
        0x11110000u, 0x11110000u, &sentinel));
    CHECK(!mtw_primary_origin_guard_should_touch(
        0, 0x11110000u, 0x11110000u, NULL));
    return 0;
}

static int test_guard_lifecycle(void) {
    SYSTEM_INFO system_info;
    unsigned char *page;
    PVOID guard_handler;
    DWORD ignored;
    size_t iteration;

    GetSystemInfo(&system_info);
    page = (unsigned char *)VirtualAlloc(
        NULL,
        (SIZE_T)system_info.dwPageSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    CHECK(page != NULL);
    page[0] = 0xA5u;

    CHECK(VirtualProtect(
        page, 1u, PAGE_READWRITE | PAGE_GUARD, &ignored));
    CHECK((protection_of(page) & PAGE_GUARD) != 0u);

    CHECK(!mtw_primary_origin_guard_touch(
        (int32_t)0x88760868u,
        0x11110000u, 0x11110000u, page));
    CHECK((protection_of(page) & PAGE_GUARD) != 0u);

    CHECK(!mtw_primary_origin_guard_touch(
        0, 0x22220000u, 0x11110000u, page));
    CHECK((protection_of(page) & PAGE_GUARD) != 0u);

    guarded_test_page = page;
    guard_handler_count = 0;
    guard_handler = AddVectoredExceptionHandler(
        1u, handle_test_guard_fault);
    CHECK(guard_handler != NULL);

    CHECK(mtw_primary_origin_guard_touch(
        0, 0x11110000u, 0x11110000u, page));
    CHECK((protection_of(page) & PAGE_GUARD) == 0u);
    CHECK(page[0] == 0xA5u);
    CHECK(guard_handler_count == 1);

    CHECK(!mtw_primary_origin_guard_touch(
        0, 0x11110000u, 0x11110000u, page));
    CHECK(page[0] == 0xA5u);

    /*
     * Model repeated locks and backend recreation: each lock rearms the
     * origin page, and a replaced surface is ignored until it becomes the
     * authoritative primary staging surface.
     */
    for (iteration = 0u; iteration < 32u; ++iteration) {
        uintptr_t surface = 0x30000000u + (uintptr_t)iteration * 0x1000u;

        CHECK(VirtualProtect(
            page, 1u, PAGE_READWRITE | PAGE_GUARD, &ignored));
        CHECK(!mtw_primary_origin_guard_touch(
            0, surface, surface + 0x1000u, page));
        CHECK((protection_of(page) & PAGE_GUARD) != 0u);
        CHECK(mtw_primary_origin_guard_touch(
            0, surface, surface, page));
        CHECK((protection_of(page) & PAGE_GUARD) == 0u);
        CHECK(page[0] == 0xA5u);
    }
    CHECK(guard_handler_count == 33);
    CHECK(RemoveVectoredExceptionHandler(guard_handler) != 0u);

    /*
     * If no owner handles the guard fault, the core catches it and fails
     * closed instead of crashing the game. The OS still clears PAGE_GUARD.
     */
    CHECK(VirtualProtect(
        page, 1u, PAGE_READWRITE | PAGE_GUARD, &ignored));
    CHECK(!mtw_primary_origin_guard_touch(
        0, 0x11110000u, 0x11110000u, page));
    CHECK((protection_of(page) & PAGE_GUARD) == 0u);
    CHECK(page[0] == 0xA5u);

    VirtualFree(page, 0u, MEM_RELEASE);
    return 0;
}

int main(void) {
    CHECK(test_exact_hook_signature() == 0);
    CHECK(test_resolution_independent_identity_gate() == 0);
    CHECK(test_guard_lifecycle() == 0);
    puts("primary origin guard tests passed");
    return 0;
}
