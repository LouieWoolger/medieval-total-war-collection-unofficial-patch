#include <string.h>

#include "loading_shadow_core.h"

static int shadow_request_is_valid(const loading_shadow_state *state,
                                   const void *live,
                                   size_t size) {
    return state != NULL && state->bytes != NULL && live != NULL && size != 0u &&
           size <= state->capacity;
}

int loading_shadow_capture(loading_shadow_state *state,
                           const void *live,
                           size_t size) {
    if (!shadow_request_is_valid(state, live, size)) return 0;
    memcpy(state->bytes, live, size);
    state->size = size;
    state->valid = 1;
    ++state->capture_count;
    return 1;
}

int loading_shadow_restore_after_lock(loading_shadow_state *state,
                                      void *live,
                                      size_t size) {
    if (!shadow_request_is_valid(state, live, size) || !state->valid ||
        state->size != size) {
        return 0;
    }
    memcpy(live, state->bytes, size);
    ++state->restore_count;
    return 1;
}

int loading_shadow_commit_before_unlock(loading_shadow_state *state,
                                        const void *live,
                                        size_t size) {
    if (!shadow_request_is_valid(state, live, size) || !state->valid ||
        state->size != size) {
        return 0;
    }
    memcpy(state->bytes, live, size);
    ++state->commit_count;
    return 1;
}
