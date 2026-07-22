#include <limits.h>
#include <string.h>

#include "focus_plane_shadow_core.h"

static int focus_plane_shadow_is_full(const focus_span_rect *rect) {
    return rect != NULL && rect->left == 0 && rect->top == 0 &&
           rect->right == (int32_t)MTW_FOCUS_PLANE_WIDTH &&
           rect->bottom == (int32_t)(MTW_FOCUS_PLANE_HEIGHT - 1u);
}

void focus_plane_shadow_initialize(focus_plane_shadow_state *state,
                                   unsigned char *bytes,
                                   size_t capacity) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->bytes = bytes;
    state->capacity = capacity;
}

int focus_plane_shadow_select(focus_plane_shadow_state *state,
                              uint32_t content_generation,
                              uint32_t surface_generation,
                              uintptr_t backing) {
    uint32_t index;
    uint32_t selected = 0u;
    uint64_t oldest = UINT64_MAX;
    int found_empty = 0;
    focus_plane_shadow_entry *entry;

    if (state == NULL || state->bytes == NULL ||
        state->capacity < MTW_FOCUS_PLANE_HISTORY_BYTES ||
        content_generation == 0u || surface_generation == 0u ||
        backing == (uintptr_t)0u) {
        return -1;
    }

    ++state->use_serial;
    if (state->use_serial == 0u) ++state->use_serial;
    for (index = 0u; index < MTW_FOCUS_PLANE_HISTORY_CAPACITY; ++index) {
        entry = &state->entries[index];
        if (entry->identity_seen &&
            entry->surface_generation == surface_generation &&
            entry->backing == backing) {
            if (entry->content_generation != content_generation) {
                entry->content_generation = content_generation;
                entry->carried_from_prior_content = entry->valid;
            }
            entry->last_use_serial = state->use_serial;
            return (int)index;
        }
        if (!entry->identity_seen && !found_empty) {
            selected = index;
            found_empty = 1;
        } else if (!found_empty && entry->last_use_serial < oldest) {
            oldest = entry->last_use_serial;
            selected = index;
        }
    }

    entry = &state->entries[selected];
    if (!entry->identity_seen) ++state->live_count;
    memset(entry, 0, sizeof(*entry));
    entry->content_generation = content_generation;
    entry->surface_generation = surface_generation;
    entry->backing = backing;
    entry->identity_seen = 1u;
    entry->last_use_serial = state->use_serial;
    return (int)selected;
}

int focus_plane_shadow_make_plan(const focus_plane_shadow_state *state,
                                 int entry_index,
                                 uint32_t source_pitch,
                                 uint8_t bytes_per_unit,
                                 const focus_span_rect *rect,
                                 focus_plane_shadow_plan *plan) {
    const focus_plane_shadow_entry *entry;
    size_t start;
    size_t end;

    if (state == NULL || state->bytes == NULL ||
        state->capacity < MTW_FOCUS_PLANE_HISTORY_BYTES || rect == NULL ||
        plan == NULL || entry_index < 0 ||
        entry_index >= (int)MTW_FOCUS_PLANE_HISTORY_CAPACITY ||
        source_pitch != MTW_FOCUS_PLANE_PITCH || bytes_per_unit != 2u) {
        return 0;
    }
    entry = &state->entries[entry_index];
    if (!entry->identity_seen ||
        rect->left < 0 || rect->left > (int32_t)MTW_FOCUS_PLANE_WIDTH ||
        rect->right < 0 || rect->right > (int32_t)MTW_FOCUS_PLANE_WIDTH ||
        rect->top < 0 || rect->top >= (int32_t)MTW_FOCUS_PLANE_HEIGHT ||
        rect->bottom < 0 ||
        rect->bottom >= (int32_t)MTW_FOCUS_PLANE_HEIGHT ||
        rect->bottom < rect->top) {
        return 0;
    }

    memset(plan, 0, sizeof(*plan));
    plan->entry_index = (uint32_t)entry_index;
    plan->had_valid_base = entry->valid;
    plan->carried_from_prior_content =
        entry->carried_from_prior_content;
    if (focus_plane_shadow_is_full(rect)) {
        plan->offset = 0u;
        plan->length = MTW_FOCUS_PLANE_BYTES;
        plan->seed_full = 1u;
        return 1;
    }
    /* A partial first publication cannot prove the unseen bytes. It may only
       update a complete history for this exact physical generation. */
    if (!entry->valid || entry->carried_from_prior_content) return 0;

    /* dgVoodoo reports dirty data as the endpoints of one forward row-major
       byte interval. A horizontal wrap (right < left with bottom > top) is
       therefore valid. R180 joined the observed 416,299,64,302 interval to a
       source store at byte 479232 and proved the complete source backing was
       still valid. This plan is reached only after a complete current-content
       shadow seed, so updating that bounded interval and publishing the full
       shadow does not promote unknown bytes. */
    start = (size_t)rect->top * source_pitch +
            (size_t)rect->left * bytes_per_unit;
    end = (size_t)rect->bottom * source_pitch +
          (size_t)rect->right * bytes_per_unit;
    if (end <= start || end > MTW_FOCUS_PLANE_BYTES) return 0;
    plan->offset = start;
    plan->length = end - start;
    return 1;
}

unsigned char *focus_plane_shadow_bytes(focus_plane_shadow_state *state,
                                        const focus_plane_shadow_plan *plan) {
    size_t base;
    if (state == NULL || plan == NULL || state->bytes == NULL ||
        state->capacity < MTW_FOCUS_PLANE_HISTORY_BYTES ||
        plan->entry_index >= MTW_FOCUS_PLANE_HISTORY_CAPACITY) {
        return NULL;
    }
    base = (size_t)plan->entry_index * MTW_FOCUS_PLANE_BYTES;
    return state->bytes + base;
}

void focus_plane_shadow_commit(focus_plane_shadow_state *state,
                               const focus_plane_shadow_plan *plan) {
    focus_plane_shadow_entry *entry;
    if (state == NULL || plan == NULL ||
        plan->entry_index >= MTW_FOCUS_PLANE_HISTORY_CAPACITY) return;
    entry = &state->entries[plan->entry_index];
    if (!entry->identity_seen) return;
    entry->valid = 1u;
    entry->carried_from_prior_content = 0u;
    if (plan->seed_full) {
        ++entry->seed_count;
    } else {
        ++entry->update_count;
    }
}

int focus_plane_shadow_needs_content_seed(
    const focus_plane_shadow_state *state,
    int entry_index,
    uint32_t content_generation) {
    const focus_plane_shadow_entry *entry;
    if (state == NULL || entry_index < 0 ||
        entry_index >= (int)MTW_FOCUS_PLANE_HISTORY_CAPACITY ||
        content_generation == 0u) {
        return 0;
    }
    entry = &state->entries[entry_index];
    return entry->identity_seen &&
           entry->content_generation == content_generation &&
           (!entry->valid || entry->carried_from_prior_content);
}

int focus_plane_shadow_can_seed_generation(
    const focus_plane_shadow_state *state,
    int entry_index,
    uint32_t content_generation,
    uint32_t source_pitch,
    uint8_t bytes_per_unit,
    size_t readable_length) {
    return source_pitch == MTW_FOCUS_PLANE_PITCH &&
           bytes_per_unit == 2u &&
           readable_length >= MTW_FOCUS_PLANE_BYTES &&
           focus_plane_shadow_needs_content_seed(
               state, entry_index, content_generation);
}

int focus_plane_shadow_seed_content(focus_plane_shadow_state *state,
                                    int entry_index,
                                    uint32_t content_generation,
                                    const unsigned char *bytes,
                                    size_t length) {
    focus_plane_shadow_entry *entry;
    size_t base;
    if (state == NULL || state->bytes == NULL || bytes == NULL ||
        state->capacity < MTW_FOCUS_PLANE_HISTORY_BYTES ||
        entry_index < 0 ||
        entry_index >= (int)MTW_FOCUS_PLANE_HISTORY_CAPACITY ||
        content_generation == 0u || length != MTW_FOCUS_PLANE_BYTES) {
        return 0;
    }
    entry = &state->entries[entry_index];
    if (!entry->identity_seen ||
        entry->content_generation != content_generation) {
        return 0;
    }
    base = (size_t)entry_index * MTW_FOCUS_PLANE_BYTES;
    memcpy(state->bytes + base, bytes, MTW_FOCUS_PLANE_BYTES);
    entry->valid = 1u;
    entry->carried_from_prior_content = 0u;
    ++entry->seed_count;
    return 1;
}

uint32_t focus_plane_shadow_live_count(const focus_plane_shadow_state *state) {
    if (state == NULL) return 0u;
    return state->live_count > MTW_FOCUS_PLANE_HISTORY_CAPACITY ?
           MTW_FOCUS_PLANE_HISTORY_CAPACITY : state->live_count;
}
