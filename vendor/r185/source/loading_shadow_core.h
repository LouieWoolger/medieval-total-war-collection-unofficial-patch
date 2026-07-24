#ifndef MTW_LOADING_SHADOW_CORE_H
#define MTW_LOADING_SHADOW_CORE_H

#include <stddef.h>
#include <stdint.h>

#define MTW_LOADING_SHADOW_MAX_DIMENSION ((size_t)32768u)
#define MTW_LOADING_SHADOW_MAX_BYTES \
    ((size_t)512u * (size_t)1024u * (size_t)1024u)
#define MTW_LOADING_SHADOW_MAX_BYTES_PER_PIXEL ((size_t)16u)

typedef void *(*loading_shadow_allocate_fn)(size_t size, void *context);
typedef void (*loading_shadow_release_fn)(void *memory, void *context);

typedef struct loading_shadow_surface {
    void *bits;
    size_t width;
    size_t height;
    size_t pitch;
    size_t bytes_per_pixel;
    uintptr_t epoch;
} loading_shadow_surface;

typedef struct loading_shadow_state {
    unsigned char *bytes;
    size_t capacity;
    size_t size;
    loading_shadow_surface surface;
    loading_shadow_allocate_fn allocate;
    loading_shadow_release_fn release;
    void *allocator_context;
    int valid;
    int operation_active;
    unsigned long capture_count;
    unsigned long restore_count;
    unsigned long commit_count;
    unsigned long allocation_count;
    unsigned long invalidation_count;
} loading_shadow_state;

void loading_shadow_initialize(loading_shadow_state *state,
                               loading_shadow_allocate_fn allocate,
                               loading_shadow_release_fn release,
                               void *allocator_context);
void loading_shadow_release(loading_shadow_state *state);
void loading_shadow_invalidate(loading_shadow_state *state);
int loading_shadow_surface_size(const loading_shadow_surface *surface,
                                size_t *size);
int loading_shadow_capture(loading_shadow_state *state,
                           const loading_shadow_surface *surface);
int loading_shadow_restore_after_lock(
    loading_shadow_state *state,
    const loading_shadow_surface *surface);
int loading_shadow_commit_before_unlock(
    loading_shadow_state *state,
    const loading_shadow_surface *surface);

#endif
