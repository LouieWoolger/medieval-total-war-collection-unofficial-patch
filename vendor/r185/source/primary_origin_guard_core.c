#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

#include "primary_origin_guard_core.h"

static const unsigned char primary_origin_guard_hook_expected[
    MTW_PRIMARY_ORIGIN_GUARD_HOOK_SIZE] = {
        0x85u, 0xC0u, 0x75u, 0xBFu, 0x89u, 0x46u, 0x1Cu
    };
static const unsigned char deployment_terrain_call_expected[
    MTW_DEPLOYMENT_TERRAIN_DRAW_CALL_SIZE] = {
        0xE8u, 0x4Eu, 0xC0u, 0xF1u, 0xFFu
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

int mtw_deployment_terrain_call_supported(
    const unsigned char *actual,
    size_t size) {
    return actual != NULL &&
           size == sizeof(deployment_terrain_call_expected) &&
           memcmp(actual, deployment_terrain_call_expected,
                  sizeof(deployment_terrain_call_expected)) == 0;
}

const unsigned char *mtw_deployment_terrain_call_expected(void) {
    return deployment_terrain_call_expected;
}

static int deployment_stage_shape_valid(
    uint32_t width, uint32_t height, int32_t pitch) {
    uint64_t total_bytes;
    if (width == 0u || width > 8192u ||
        height == 0u || height > 4320u ||
        pitch <= 0 || (uint32_t)pitch < width * 2u ||
        (uint32_t)pitch > 65536u) {
        return 0;
    }
    total_bytes = (uint64_t)(uint32_t)pitch * (uint64_t)height;
    return total_bytes <= 134217728u;
}

int mtw_deployment_stage_should_sync(
    uint32_t armed,
    int32_t lock_result,
    uintptr_t locked_surface,
    uintptr_t current_primary_staging_surface,
    const void *bits,
    uint32_t width,
    uint32_t height,
    int32_t pitch) {
    return armed != 0u && lock_result == 0 &&
           locked_surface != 0u &&
           locked_surface == current_primary_staging_surface &&
           bits != NULL &&
           deployment_stage_shape_valid(width, height, pitch);
}

int mtw_deployment_copy_rows(
    void *destination,
    int32_t destination_pitch,
    const void *source,
    int32_t source_pitch,
    uint32_t width,
    uint32_t height) {
    uint32_t y;
    size_t row_bytes;
    if (destination == NULL || source == NULL ||
        !deployment_stage_shape_valid(width, height, destination_pitch) ||
        !deployment_stage_shape_valid(width, height, source_pitch)) {
        return 0;
    }
    row_bytes = (size_t)width * 2u;
    for (y = 0u; y < height; ++y) {
        memcpy((unsigned char *)destination +
                   (size_t)y * (size_t)destination_pitch,
               (const unsigned char *)source +
                   (size_t)y * (size_t)source_pitch,
               row_bytes);
    }
    return 1;
}

int mtw_deployment_source_looks_rendered(
    const void *source,
    int32_t pitch,
    uint32_t width,
    uint32_t height) {
    unsigned x_step, y_step;
    unsigned nonblack = 0u;
    if (source == NULL ||
        !deployment_stage_shape_valid(width, height, pitch)) {
        return 0;
    }
    for (y_step = 1u; y_step <= 4u; ++y_step) {
        uint32_t y = (height * y_step) / 5u;
        const unsigned char *row = (const unsigned char *)source +
            (size_t)y * (size_t)pitch;
        for (x_step = 1u; x_step <= 4u; ++x_step) {
            uint32_t x = (width * x_step) / 5u;
            const unsigned char *pixel = row + (size_t)x * 2u;
            nonblack += (pixel[0] != 0u || pixel[1] != 0u);
        }
    }
    return nonblack >= 4u;
}
