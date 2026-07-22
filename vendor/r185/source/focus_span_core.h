#ifndef MTW_FOCUS_SPAN_CORE_H
#define MTW_FOCUS_SPAN_CORE_H

#include <stdint.h>

#define MTW_FOCUS_AUTHORITY_CAPACITY 64u

typedef struct focus_span_rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} focus_span_rect;

typedef struct focus_span_generation {
    uintptr_t object;
    uintptr_t backing;
    uint32_t generation;
    uint32_t full_publication_version;
    uint32_t authority_content_generation;
} focus_span_generation;

typedef struct focus_span_state {
    uint32_t saw_640_bgra;
    uint32_t post_reset_800_index;
    uint32_t next_generation;
    uint32_t content_generation;
    uint32_t authority_admission_open;
    uint32_t active_generation;
    uint32_t live_count;
    focus_span_generation generations[MTW_FOCUS_AUTHORITY_CAPACITY];
    uint32_t bootstrap_authority_count;
    uintptr_t bootstrap_authority_objects[MTW_FOCUS_AUTHORITY_CAPACITY];
    uint32_t bootstrap_authority_generations[MTW_FOCUS_AUTHORITY_CAPACITY];
    uint32_t bootstrap_authority_content_generations[
        MTW_FOCUS_AUTHORITY_CAPACITY];
} focus_span_state;

int focus_span_observe_surface(focus_span_state *state,
                               uint16_t width,
                               uint16_t height,
                               uint32_t pitch,
                               uint8_t bytes_per_unit,
                               uintptr_t object,
                               uintptr_t backing);

int focus_span_complete_surface(focus_span_state *state,
                                uint16_t width,
                                uint16_t height,
                                uint32_t pitch,
                                uint8_t bytes_per_unit,
                                uintptr_t object,
                                uintptr_t backing);

int focus_span_observe_publication(focus_span_state *state,
                                   uintptr_t current_backing,
                                   uint32_t source_pitch,
                                   uintptr_t caller_rva,
                                   const focus_span_rect *rect);

int focus_span_should_ack_reverse(const focus_span_state *state,
                                  uintptr_t current_object);

int focus_span_note_reverse_applied(focus_span_state *state,
                                    uintptr_t current_object);

int focus_span_refresh_backing(focus_span_state *state,
                               uintptr_t object,
                               uintptr_t current_backing);

int focus_span_revoke_object(focus_span_state *state, uintptr_t object);

int focus_span_begin_content_generation(focus_span_state *state);

uint32_t focus_span_content_generation(const focus_span_state *state);

uint32_t focus_span_generation_for_object(const focus_span_state *state,
                                          uintptr_t object);

uint32_t focus_span_live_count(const focus_span_state *state);

#endif
