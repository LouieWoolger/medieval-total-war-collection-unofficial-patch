#ifndef MTW_LOADING_SHADOW_CORE_H
#define MTW_LOADING_SHADOW_CORE_H

#include <stddef.h>

typedef struct loading_shadow_state {
    unsigned char *bytes;
    size_t capacity;
    size_t size;
    int valid;
    unsigned long capture_count;
    unsigned long restore_count;
    unsigned long commit_count;
} loading_shadow_state;

int loading_shadow_capture(loading_shadow_state *state,
                           const void *live,
                           size_t size);
int loading_shadow_restore_after_lock(loading_shadow_state *state,
                                      void *live,
                                      size_t size);
int loading_shadow_commit_before_unlock(loading_shadow_state *state,
                                        const void *live,
                                        size_t size);

#endif
