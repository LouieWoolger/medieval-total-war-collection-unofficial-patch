#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

#include "primary_origin_guard_core.h"

static const unsigned char primary_origin_guard_hook_expected[
    MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE] = {
        0x85u, 0xC0u, 0x75u, 0xBFu, 0x89u, 0x46u, 0x1Cu
    };

const unsigned char *mtw_primary_origin_guard_hook_expected(void) {
    return primary_origin_guard_hook_expected;
}

size_t mtw_primary_origin_guard_hook_size(void) {
    return sizeof(primary_origin_guard_hook_expected);
}

int mtw_primary_origin_guard_hook_supported(
    const unsigned char *actual,
    size_t size) {
    return actual != NULL &&
           size == sizeof(primary_origin_guard_hook_expected) &&
           memcmp(actual,
                  primary_origin_guard_hook_expected,
                  sizeof(primary_origin_guard_hook_expected)) == 0;
}

int mtw_primary_origin_guard_should_touch(
    int32_t lock_result,
    uintptr_t locked_surface,
    uintptr_t current_primary_staging_surface,
    const void *bits) {
    return lock_result == 0 &&
           locked_surface != 0u &&
           locked_surface == current_primary_staging_surface &&
           bits != NULL;
}

int mtw_primary_origin_guard_touch(
    int32_t lock_result,
    uintptr_t locked_surface,
    uintptr_t current_primary_staging_surface,
    void *bits) {
    MEMORY_BASIC_INFORMATION info;
    DWORD access;
    volatile unsigned char *origin;
    unsigned char value;

    if (!mtw_primary_origin_guard_should_touch(
            lock_result,
            locked_surface,
            current_primary_staging_surface,
            bits)) {
        return 0;
    }

    memset(&info, 0, sizeof(info));
    if (VirtualQuery(bits, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT ||
        (info.Protect & PAGE_GUARD) == 0u) {
        return 0;
    }

    access = info.Protect & 0xFFu;
    if (access == 0u || access == PAGE_NOACCESS) return 0;

    origin = (volatile unsigned char *)bits;
    __try {
        /*
         * Let the surface owner's vectored guard handler observe the origin
         * fault and update its dirty-state bookkeeping. The write preserves
         * the byte exactly; it is a state notification, not a pixel change.
         */
        value = *origin;
        *origin = value;
    } __except (
        (DWORD)GetExceptionCode() == 0x80000001u
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
    return 1;
}
