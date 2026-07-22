#ifndef MTW_GUARDED_MEMORY_COPY_H
#define MTW_GUARDED_MEMORY_COPY_H

#include <stddef.h>

typedef struct guarded_memory_copy_stats {
    size_t queried_regions;
    size_t guard_regions;
    size_t restored_regions;
    size_t copied_bytes;
} guarded_memory_copy_stats;

int guarded_memory_copy(void *destination,
                        const void *source,
                        size_t length,
                        guarded_memory_copy_stats *stats);

#endif
