#ifndef MTW_REENTRANT_LOCK_PATCH_CORE_H
#define MTW_REENTRANT_LOCK_PATCH_CORE_H

#include <stddef.h>
#include <stdint.h>

#define MTW_REENTRANT_CALL_SIZE 5u

typedef struct mtw_reentrant_callsite_bundle {
    const uint8_t *outer_acquire;
    size_t outer_acquire_size;
    const uint8_t *outer_release;
    size_t outer_release_size;
    const uint8_t *inner_acquire;
    size_t inner_acquire_size;
    const uint8_t *inner_release_ok;
    size_t inner_release_ok_size;
    const uint8_t *inner_release_fail;
    size_t inner_release_fail_size;
} mtw_reentrant_callsite_bundle;

int mtw_validate_reentrant_callsites(
    const mtw_reentrant_callsite_bundle *bundle);

int mtw_should_divert_outer_owned_guard_fault(uint32_t owner_thread_id,
                                              uint32_t current_thread_id,
                                              uint32_t fast_owner_flag);

#endif
