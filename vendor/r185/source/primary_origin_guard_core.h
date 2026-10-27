#ifndef MTW_PRIMARY_ORIGIN_GUARD_CORE_H
#define MTW_PRIMARY_ORIGIN_GUARD_CORE_H

#include <stddef.h>
#include <stdint.h>

#define MTW_PRIMARY_ORIGIN_GUARD_HOOK_RVA 0x00B3F185u
#define MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE 7u
#define MTW_PRIMARY_ORIGIN_GUARD_FAILURE_RVA 0x00B3F148u
#define MTW_PRIMARY_ORIGIN_GUARD_SUCCESS_RVA 0x00B3F18Cu
#define MTW_PRIMARY_STAGING_SURFACE_RVA 0x00B44098u
#define MTW_DEPLOYMENT_TERRAIN_DRAW_CALL_RVA 0x0023120Du
#define MTW_DEPLOYMENT_TERRAIN_DRAW_TARGET_RVA 0x0014D260u
#define MTW_DEPLOYMENT_TERRAIN_DRAW_CALL_SIZE 5u

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

int mtw_deployment_terrain_call_supported(
    const unsigned char *actual,
    size_t size);

const unsigned char *mtw_deployment_terrain_call_expected(void);

int mtw_deployment_stage_should_sync(
    uint32_t armed,
    int32_t lock_result,
    uintptr_t locked_surface,
    uintptr_t current_primary_staging_surface,
    const void *bits,
    uint32_t width,
    uint32_t height,
    int32_t pitch);

int mtw_deployment_copy_rows(
    void *destination,
    int32_t destination_pitch,
    const void *source,
    int32_t source_pitch,
    uint32_t width,
    uint32_t height);

int mtw_deployment_source_looks_rendered(
    const void *source,
    int32_t pitch,
    uint32_t width,
    uint32_t height);

#endif
