#include "reentrant_lock_patch_core.h"

#include <string.h>

static int exact_call(const uint8_t *observed,
                      size_t observed_size,
                      const uint8_t expected[MTW_REENTRANT_CALL_SIZE]) {
    return observed != NULL &&
           observed_size == MTW_REENTRANT_CALL_SIZE &&
           memcmp(observed, expected, MTW_REENTRANT_CALL_SIZE) == 0;
}

int mtw_validate_reentrant_callsites(
    const mtw_reentrant_callsite_bundle *bundle) {
    static const uint8_t outer_acquire[MTW_REENTRANT_CALL_SIZE] = {
        0xE8, 0xA8, 0xFC, 0xF6, 0xFF
    };
    static const uint8_t outer_release[MTW_REENTRANT_CALL_SIZE] = {
        0xE8, 0x69, 0xF8, 0xF6, 0xFF
    };
    static const uint8_t inner_acquire[MTW_REENTRANT_CALL_SIZE] = {
        0xE8, 0x07, 0xED, 0xFF, 0xFF
    };
    static const uint8_t inner_release_ok[MTW_REENTRANT_CALL_SIZE] = {
        0xE8, 0x35, 0xED, 0xFF, 0xFF
    };
    static const uint8_t inner_release_fail[MTW_REENTRANT_CALL_SIZE] = {
        0xE8, 0x25, 0xED, 0xFF, 0xFF
    };

    if (bundle == NULL) return 0;
    return exact_call(bundle->outer_acquire, bundle->outer_acquire_size,
                      outer_acquire) &&
           exact_call(bundle->outer_release, bundle->outer_release_size,
                      outer_release) &&
           exact_call(bundle->inner_acquire, bundle->inner_acquire_size,
                      inner_acquire) &&
           exact_call(bundle->inner_release_ok,
                      bundle->inner_release_ok_size, inner_release_ok) &&
           exact_call(bundle->inner_release_fail,
                      bundle->inner_release_fail_size, inner_release_fail);
}

int mtw_should_divert_outer_owned_guard_fault(uint32_t owner_thread_id,
                                              uint32_t current_thread_id,
                                              uint32_t fast_owner_flag) {
    return owner_thread_id != 0u &&
           owner_thread_id == current_thread_id &&
           fast_owner_flag == 1u;
}
