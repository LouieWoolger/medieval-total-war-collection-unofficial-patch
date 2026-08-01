#ifndef MTW_PRIMARY_ORIGIN_GUARD_CORE_H
#define MTW_PRIMARY_ORIGIN_GUARD_CORE_H

#include <stddef.h>
#include <stdint.h>

#define MTW_PRIMARY_ORIGIN_GUARD_HOOK_RVA 0x00B3F185u
#define MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE 7u
#define MTW_PRIMARY_ORIGIN_GUARD_FAILURE_RVA 0x00B3F148u
#define MTW_PRIMARY_ORIGIN_GUARD_SUCCESS_RVA 0x00B3F18Cu
#define MTW_PRIMARY_STAGING_SURFACE_RVA 0x00B44098u

const unsigned char *mtw_primary_origin_guard_hook_expected(void);
size_t mtw_primary_origin_guard_hook_size(void);
int mtw_primary_origin_guard_hook_supported(
    const unsigned char *actual,
    size_t size);

int mtw_primary_origin_guard_should_touch(
    int32_t lock_result,
    uintptr_t locked_surface,
    uintptr_t current_primary_staging_surface,
    const void *bits);

int mtw_primary_origin_guard_touch(
    int32_t lock_result,
    uintptr_t locked_surface,
    uintptr_t current_primary_staging_surface,
    void *bits);

#endif
