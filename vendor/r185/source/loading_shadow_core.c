#include <string.h>

#include "loading_shadow_core.h"

static void invalidate_without_guard(loading_shadow_state *state) {
    if (state == NULL) return;
    if (state->valid) ++state->invalidation_count;
    memset(&state->surface, 0, sizeof(state->surface));
    state->size = 0u;
    state->valid = 0;
}

static int begin_operation(loading_shadow_state *state) {
    if (state == NULL || state->operation_active) return 0;
    state->operation_active = 1;
    return 1;
}

static void end_operation(loading_shadow_state *state) {
    state->operation_active = 0;
}

static int surfaces_match(const loading_shadow_surface *left,
                          const loading_shadow_surface *right) {
    return left->bits == right->bits &&
           left->width == right->width &&
           left->height == right->height &&
           left->pitch == right->pitch &&
           left->bytes_per_pixel == right->bytes_per_pixel &&
           left->epoch == right->epoch;
}

static int reserve_without_guard(loading_shadow_state *state, size_t size) {
    unsigned char *replacement;

    if (state->bytes != NULL && state->capacity >= size) return 1;
    if (state->allocate == NULL || state->release == NULL) {
        invalidate_without_guard(state);
        return 0;
    }
    replacement = (unsigned char *)state->allocate(
        size, state->allocator_context);
    if (replacement == NULL) {
        invalidate_without_guard(state);
        return 0;
    }
    if (state->bytes != NULL) {
        state->release(state->bytes, state->allocator_context);
    }
    state->bytes = replacement;
    state->capacity = size;
    ++state->allocation_count;
    invalidate_without_guard(state);
    return 1;
}

void loading_shadow_initialize(loading_shadow_state *state,
                               loading_shadow_allocate_fn allocate,
                               loading_shadow_release_fn release,
                               void *allocator_context) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->allocate = allocate;
    state->release = release;
    state->allocator_context = allocator_context;
}

void loading_shadow_release(loading_shadow_state *state) {
    if (state == NULL) return;
    if (state->bytes != NULL && state->release != NULL) {
        state->release(state->bytes, state->allocator_context);
    }
    memset(state, 0, sizeof(*state));
}

void loading_shadow_invalidate(loading_shadow_state *state) {
    if (!begin_operation(state)) return;
    invalidate_without_guard(state);
    end_operation(state);
}

int loading_shadow_surface_size(const loading_shadow_surface *surface,
                                size_t *size) {
    const size_t size_max = (size_t)-1;
    size_t row_bytes;
    size_t total;

    if (surface == NULL || size == NULL || surface->bits == NULL ||
        surface->width == 0u || surface->height == 0u ||
        surface->pitch == 0u || surface->bytes_per_pixel == 0u ||
        surface->epoch == (uintptr_t)0u ||
        surface->width > MTW_LOADING_SHADOW_MAX_DIMENSION ||
        surface->height > MTW_LOADING_SHADOW_MAX_DIMENSION ||
        surface->bytes_per_pixel >
            MTW_LOADING_SHADOW_MAX_BYTES_PER_PIXEL) {
        return 0;
    }
    if (surface->width > size_max / surface->bytes_per_pixel) return 0;
    row_bytes = surface->width * surface->bytes_per_pixel;
    if (surface->pitch < row_bytes) return 0;
    if (surface->height > size_max / surface->pitch) return 0;
    total = surface->pitch * surface->height;
    if (total == 0u || total > MTW_LOADING_SHADOW_MAX_BYTES) return 0;
    *size = total;
    return 1;
}

int loading_shadow_capture(loading_shadow_state *state,
                           const loading_shadow_surface *surface) {
    size_t size = 0u;
    int result = 0;

    if (!begin_operation(state)) return 0;
    if (!loading_shadow_surface_size(surface, &size) ||
        !reserve_without_guard(state, size)) {
        invalidate_without_guard(state);
    } else {
        memcpy(state->bytes, surface->bits, size);
        state->surface = *surface;
        state->size = size;
        state->valid = 1;
        ++state->capture_count;
        result = 1;
    }
    end_operation(state);
    return result;
}

int loading_shadow_restore_after_lock(
    loading_shadow_state *state,
    const loading_shadow_surface *surface) {
    size_t size = 0u;
    int result = 0;

    if (!begin_operation(state)) return 0;
    if (!loading_shadow_surface_size(surface, &size) ||
        state->bytes == NULL || !state->valid || state->size != size ||
        !surfaces_match(&state->surface, surface)) {
        invalidate_without_guard(state);
    } else {
        memcpy(surface->bits, state->bytes, size);
        ++state->restore_count;
        result = 1;
    }
    end_operation(state);
    return result;
}

int loading_shadow_commit_before_unlock(
    loading_shadow_state *state,
    const loading_shadow_surface *surface) {
    size_t size = 0u;
    int result = 0;

    if (!begin_operation(state)) return 0;
    if (!loading_shadow_surface_size(surface, &size) ||
        state->bytes == NULL || !state->valid || state->size != size ||
        !surfaces_match(&state->surface, surface)) {
        invalidate_without_guard(state);
    } else {
        memcpy(state->bytes, surface->bits, size);
        ++state->commit_count;
        result = 1;
    }
    end_operation(state);
    return result;
}
