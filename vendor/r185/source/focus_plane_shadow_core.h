#ifndef MTW_FOCUS_PLANE_SHADOW_CORE_H
#define MTW_FOCUS_PLANE_SHADOW_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "focus_span_core.h"

#define MTW_FOCUS_PLANE_WIDTH 800u
#define MTW_FOCUS_PLANE_HEIGHT 600u
#define MTW_FOCUS_PLANE_PITCH 1600u
#define MTW_FOCUS_PLANE_BYTES (MTW_FOCUS_PLANE_PITCH * MTW_FOCUS_PLANE_HEIGHT)

/* This is a bounded LRU of physical composition histories, not a menu/page
   table. Eight independent frontend planes cover the observed concurrency
   while keeping committed memory below 8 MiB. Pointer reuse is separated by
   the monotonically assigned surface generation. */
#define MTW_FOCUS_PLANE_HISTORY_CAPACITY 8u
#define MTW_FOCUS_PLANE_HISTORY_BYTES \
    (MTW_FOCUS_PLANE_BYTES * MTW_FOCUS_PLANE_HISTORY_CAPACITY)

typedef struct focus_plane_shadow_entry {
    uint32_t content_generation;
    uint32_t surface_generation;
    uintptr_t backing;
    uint32_t identity_seen;
    uint32_t valid;
    uint32_t carried_from_prior_content;
    uint32_t seed_count;
    uint32_t update_count;
    uint64_t last_use_serial;
} focus_plane_shadow_entry;

typedef struct focus_plane_shadow_state {
    unsigned char *bytes;
    size_t capacity;
    focus_plane_shadow_entry entries[MTW_FOCUS_PLANE_HISTORY_CAPACITY];
    uint64_t use_serial;
    uint32_t live_count;
} focus_plane_shadow_state;

typedef struct focus_plane_shadow_plan {
    size_t offset;
    size_t length;
    uint32_t entry_index;
    uint32_t seed_full;
    uint32_t had_valid_base;
    uint32_t carried_from_prior_content;
} focus_plane_shadow_plan;

void focus_plane_shadow_initialize(focus_plane_shadow_state *state,
                                   unsigned char *bytes,
                                   size_t capacity);
int focus_plane_shadow_select(focus_plane_shadow_state *state,
                              uint32_t content_generation,
                              uint32_t surface_generation,
                              uintptr_t backing);
int focus_plane_shadow_make_plan(const focus_plane_shadow_state *state,
                                 int entry_index,
                                 uint32_t source_pitch,
                                 uint8_t bytes_per_unit,
                                 const focus_span_rect *rect,
                                 focus_plane_shadow_plan *plan);
unsigned char *focus_plane_shadow_bytes(focus_plane_shadow_state *state,
                                        const focus_plane_shadow_plan *plan);
void focus_plane_shadow_commit(focus_plane_shadow_state *state,
                               const focus_plane_shadow_plan *plan);
int focus_plane_shadow_needs_content_seed(
    const focus_plane_shadow_state *state,
    int entry_index,
    uint32_t content_generation);
int focus_plane_shadow_can_seed_generation(
    const focus_plane_shadow_state *state,
    int entry_index,
    uint32_t content_generation,
    uint32_t source_pitch,
    uint8_t bytes_per_unit,
    size_t readable_length);
int focus_plane_shadow_seed_content(focus_plane_shadow_state *state,
                                    int entry_index,
                                    uint32_t content_generation,
                                    const unsigned char *bytes,
                                    size_t length);
uint32_t focus_plane_shadow_live_count(const focus_plane_shadow_state *state);

#endif
