#include "focus_span_core.h"

#define MTW_RESET_WIDTH 640u
#define MTW_RESET_HEIGHT 480u
#define MTW_RESET_PITCH 2560u
#define MTW_RESET_BYTES_PER_UNIT 4u

#define MTW_MENU_WIDTH 800u
#define MTW_MENU_HEIGHT 600u
#define MTW_MENU_PITCH 1600u
#define MTW_MENU_BYTES_PER_UNIT 2u
#define MTW_FALLBACK_CALLER_RVA 0x0007C335u
#define MTW_FIRST_BOOTSTRAP_COHORT 5u
#define MTW_BOOTSTRAP_COHORT_WIDTH 4u

static void focus_span_clear_generation(focus_span_generation *entry) {
    if (entry == 0) return;
    entry->object = 0u;
    entry->backing = 0u;
    entry->generation = 0u;
    entry->full_publication_version = 0u;
    entry->authority_content_generation = 0u;
}

static void focus_span_clear_all(focus_span_state *state) {
    uint32_t index;
    if (state == 0) return;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        focus_span_clear_generation(&state->generations[index]);
    }
    state->live_count = 0u;
    state->active_generation = 0u;
    state->bootstrap_authority_count = 0u;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        state->bootstrap_authority_objects[index] = 0u;
        state->bootstrap_authority_generations[index] = 0u;
        state->bootstrap_authority_content_generations[index] = 0u;
    }
}

static void focus_span_clear_bootstrap(focus_span_state *state) {
    uint32_t index;
    if (state == 0) return;
    state->bootstrap_authority_count = 0u;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        state->bootstrap_authority_objects[index] = 0u;
        state->bootstrap_authority_generations[index] = 0u;
        state->bootstrap_authority_content_generations[index] = 0u;
    }
}

static void focus_span_forget_active_if_generation(
    focus_span_state *state,
    uint32_t generation) {
    if (state != 0 && generation != 0u &&
        state->active_generation == generation) {
        state->active_generation = 0u;
    }
}

static void focus_span_bind_bootstrap_generation(focus_span_state *state,
                                                 uintptr_t object,
                                                 uint32_t generation) {
    uint32_t index;
    if (state == 0 || object == 0u || generation == 0u) return;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->bootstrap_authority_objects[index] == object) {
            state->bootstrap_authority_generations[index] = generation;
        }
    }
}

static int focus_span_is_bootstrap_cohort(uint32_t index) {
    return index >= MTW_FIRST_BOOTSTRAP_COHORT &&
           ((index - MTW_FIRST_BOOTSTRAP_COHORT) %
            MTW_BOOTSTRAP_COHORT_WIDTH) == 0u;
}

static void focus_span_update_bootstrap(focus_span_state *state,
                                        uintptr_t object,
                                        uint32_t generation,
                                        int should_arm) {
    uint32_t index;
    uint32_t active;
    if (state == 0 || object == 0u) return;
    active = state->bootstrap_authority_count;
    if (active > MTW_FOCUS_AUTHORITY_CAPACITY) {
        active = MTW_FOCUS_AUTHORITY_CAPACITY;
    }
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->bootstrap_authority_objects[index] == object) {
            state->bootstrap_authority_objects[index] = 0u;
            state->bootstrap_authority_generations[index] = 0u;
            state->bootstrap_authority_content_generations[index] = 0u;
            if (active != 0u) --active;
        }
    }
    if (should_arm) {
        for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
            if (state->bootstrap_authority_objects[index] == 0u) {
                state->bootstrap_authority_objects[index] = object;
                state->bootstrap_authority_generations[index] = generation;
                state->bootstrap_authority_content_generations[index] =
                    state->content_generation;
                ++active;
                break;
            }
        }
    }
    state->bootstrap_authority_count = active;
}

static uint32_t focus_span_next_generation(focus_span_state *state) {
    ++state->next_generation;
    if (state->next_generation == 0u) ++state->next_generation;
    return state->next_generation;
}

static int focus_span_find_object(const focus_span_state *state,
                                  uintptr_t object) {
    uint32_t index;
    if (state == 0 || object == 0u) return -1;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->generations[index].object == object) return (int)index;
    }
    return -1;
}

static int focus_span_find_backing(const focus_span_state *state,
                                   uintptr_t backing) {
    uint32_t index;
    if (state == 0 || backing == 0u) return -1;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->generations[index].backing == backing) return (int)index;
    }
    return -1;
}

static int focus_span_find_slot(const focus_span_state *state) {
    uint32_t index;
    uint32_t oldest_index = 0u;
    uint32_t oldest_generation = UINT32_MAX;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->generations[index].object == 0u) return (int)index;
        if (state->generations[index].generation < oldest_generation) {
            oldest_generation = state->generations[index].generation;
            oldest_index = index;
        }
    }
    return (int)oldest_index;
}

static int focus_span_is_full(const focus_span_rect *rect) {
    return rect != 0 && rect->left == 0 && rect->top == 0 &&
           rect->right == 800 && rect->bottom == 599;
}

int focus_span_observe_surface(focus_span_state *state,
                               uint16_t width,
                               uint16_t height,
                               uint32_t pitch,
                               uint8_t bytes_per_unit,
                               uintptr_t object,
                               uintptr_t backing) {
    if (state == 0) return 0;
    if (width == MTW_RESET_WIDTH && height == MTW_RESET_HEIGHT &&
        pitch == MTW_RESET_PITCH &&
        bytes_per_unit == MTW_RESET_BYTES_PER_UNIT) {
        state->saw_640_bgra = 1u;
        state->post_reset_800_index = 0u;
        focus_span_clear_all(state);
        ++state->content_generation;
        if (state->content_generation == 0u) ++state->content_generation;
        state->authority_admission_open = 1u;
        return 0;
    }
    if (!state->saw_640_bgra || width != MTW_MENU_WIDTH ||
        height != MTW_MENU_HEIGHT || pitch != MTW_MENU_PITCH ||
        bytes_per_unit != MTW_MENU_BYTES_PER_UNIT) {
        return 0;
    }

    ++state->post_reset_800_index;
    /* This entry-time observation intentionally preserves R6F160's proven
       startup ordering. Object fields can still contain their previous
       generation here, so it may arm only the bootstrap pointer; completed
       lifecycle identity is registered separately on constructor return. */
    focus_span_update_bootstrap(
        state, object, 0u,
        state->authority_admission_open != 0u &&
        focus_span_is_bootstrap_cohort(state->post_reset_800_index));
    (void)backing;
    return state->post_reset_800_index == 1u;
}

int focus_span_complete_surface(focus_span_state *state,
                                uint16_t width,
                                uint16_t height,
                                uint32_t pitch,
                                uint8_t bytes_per_unit,
                                uintptr_t object,
                                uintptr_t backing) {
    int slot;
    int duplicate;
    if (state == 0 || width != MTW_MENU_WIDTH ||
        height != MTW_MENU_HEIGHT || pitch != MTW_MENU_PITCH ||
        bytes_per_unit != MTW_MENU_BYTES_PER_UNIT ||
        object == 0u || backing == 0u) {
        return 0;
    }

    duplicate = focus_span_find_object(state, object);
    if (duplicate >= 0) {
        focus_span_forget_active_if_generation(
            state, state->generations[duplicate].generation);
        focus_span_clear_generation(&state->generations[duplicate]);
        if (state->live_count != 0u) --state->live_count;
    }
    duplicate = focus_span_find_backing(state, backing);
    if (duplicate >= 0) {
        focus_span_update_bootstrap(
            state, state->generations[duplicate].object, 0u, 0);
        focus_span_forget_active_if_generation(
            state, state->generations[duplicate].generation);
        focus_span_clear_generation(&state->generations[duplicate]);
        if (state->live_count != 0u) --state->live_count;
    }

    slot = focus_span_find_slot(state);
    focus_span_forget_active_if_generation(
        state, state->generations[slot].generation);
    if (state->generations[slot].object == 0u) {
        ++state->live_count;
    }
    state->generations[slot].object = object;
    state->generations[slot].backing = backing;
    state->generations[slot].generation = focus_span_next_generation(state);
    state->generations[slot].full_publication_version = 0u;
    state->generations[slot].authority_content_generation = 0u;
    focus_span_bind_bootstrap_generation(
        state, object, state->generations[slot].generation);
    return 1;
}

int focus_span_observe_publication(focus_span_state *state,
                                   uintptr_t current_backing,
                                   uint32_t source_pitch,
                                   uintptr_t caller_rva,
                                   const focus_span_rect *rect) {
    int slot;
    focus_span_generation *entry;
    if (state == 0 || rect == 0 || current_backing == 0u ||
        source_pitch != MTW_MENU_PITCH ||
        caller_rva != MTW_FALLBACK_CALLER_RVA) {
        return 0;
    }
    slot = focus_span_find_backing(state, current_backing);
    if (slot < 0) return 0;
    entry = &state->generations[slot];
    if (focus_span_is_full(rect)) {
        if (state->authority_admission_open == 0u) return 0;
        /* After startup, a full fallback from a different still-allocated
           physical generation is not proof that its CPU plane is current.
           R182 transition 51 demonstrated exactly this ordering: dormant
           generation 9 supplied a complete stale Single Player plane while
           generation 8 held the current Multiplayer page. Let one original
           reverse synchronize that generation before it can become active. */
        if (state->content_generation > 1u &&
            state->active_generation != 0u &&
            state->active_generation != entry->generation) {
            entry->full_publication_version = 0u;
            entry->authority_content_generation = 0u;
            return 0;
        }
        ++entry->full_publication_version;
        if (entry->full_publication_version == 0u) {
            ++entry->full_publication_version;
        }
        entry->authority_content_generation = state->content_generation;
        state->active_generation = entry->generation;
        return 0;
    }
    return entry->full_publication_version != 0u &&
           entry->authority_content_generation == state->content_generation;
}

int focus_span_should_ack_reverse(const focus_span_state *state,
                                  uintptr_t current_object) {
    uint32_t index;
    int slot = focus_span_find_object(state, current_object);
    if (slot < 0) return 0;
    if (state->generations[slot].full_publication_version != 0u &&
        state->generations[slot].generation == state->active_generation &&
        state->generations[slot].authority_content_generation ==
            state->content_generation) {
        return 1;
    }
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->bootstrap_authority_objects[index] == current_object &&
            state->bootstrap_authority_generations[index] ==
                state->generations[slot].generation &&
            state->bootstrap_authority_content_generations[index] ==
                state->content_generation) {
            return 1;
        }
    }
    return 0;
}

int focus_span_note_reverse_applied(focus_span_state *state,
                                    uintptr_t current_object) {
    uint32_t index;
    int slot = focus_span_find_object(state, current_object);
    if (slot < 0) return 0;

    /* The original reverse dispatcher has just copied the GPU's complete
       current plane into this exact CPU backing. That is the lifecycle handoff
       point: revoke competing content authority, make this generation active,
       and acknowledge later reverse calls until a real lifetime transition. */
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if ((int)index == slot) continue;
        state->generations[index].full_publication_version = 0u;
        state->generations[index].authority_content_generation = 0u;
    }
    state->active_generation = state->generations[slot].generation;
    state->generations[slot].full_publication_version = 1u;
    state->generations[slot].authority_content_generation =
        state->content_generation;
    focus_span_clear_bootstrap(state);
    return 1;
}

int focus_span_refresh_backing(focus_span_state *state,
                               uintptr_t object,
                               uintptr_t current_backing) {
    int slot;
    int duplicate;
    if (state == 0 || object == 0u || current_backing == 0u) return 0;
    slot = focus_span_find_object(state, object);
    if (slot < 0 || state->generations[slot].backing == current_backing) {
        return 0;
    }
    duplicate = focus_span_find_backing(state, current_backing);
    if (duplicate >= 0 && duplicate != slot) {
        focus_span_update_bootstrap(
            state, state->generations[duplicate].object, 0u, 0);
        focus_span_forget_active_if_generation(
            state, state->generations[duplicate].generation);
        focus_span_clear_generation(&state->generations[duplicate]);
        if (state->live_count != 0u) --state->live_count;
    }
    focus_span_update_bootstrap(state, object, 0u, 0);
    focus_span_forget_active_if_generation(
        state, state->generations[slot].generation);
    state->generations[slot].backing = current_backing;
    state->generations[slot].generation = focus_span_next_generation(state);
    state->generations[slot].full_publication_version = 0u;
    state->generations[slot].authority_content_generation = 0u;
    return 1;
}

int focus_span_revoke_object(focus_span_state *state, uintptr_t object) {
    int slot = focus_span_find_object(state, object);
    if (slot < 0) return 0;
    focus_span_update_bootstrap(state, object, 0u, 0);
    focus_span_forget_active_if_generation(
        state, state->generations[slot].generation);
    focus_span_clear_generation(&state->generations[slot]);
    if (state->live_count != 0u) --state->live_count;
    return 1;
}

int focus_span_begin_content_generation(focus_span_state *state) {
    uint32_t index;
    uint32_t prior_content_generation;
    int transferred = 0;
    if (state == 0) return 0;
    prior_content_generation = state->content_generation;
    ++state->content_generation;
    if (state->content_generation == 0u) ++state->content_generation;
    /* Input predicts a possible page transition, but it is not itself a GPU
       write or a physical resource-lifetime boundary. Carry only authority
       that belonged to the immediately preceding content version of the same
       tracked object/backing generation. Backing replacement, destruction,
       and reset still revoke through their lifecycle paths. A later complete
       publication by the handler supersedes this carried version normally. */
    state->authority_admission_open = 1u;
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (state->generations[index].generation ==
                state->active_generation &&
            state->generations[index].full_publication_version != 0u &&
            state->generations[index].authority_content_generation ==
                prior_content_generation) {
            state->generations[index].full_publication_version = 1u;
            state->generations[index].authority_content_generation =
                state->content_generation;
            transferred = 1;
        } else if (state->generations[index].full_publication_version != 0u ||
                   state->generations[index].authority_content_generation !=
                       0u) {
            state->generations[index].full_publication_version = 0u;
            state->generations[index].authority_content_generation = 0u;
        }
    }
    /* Bootstrap is an initialization exception, not durable page authority.
       Once real frontend input begins, only the currently active generation's
       completed publication may cross the content boundary. */
    focus_span_clear_bootstrap(state);
    return transferred;
}

uint32_t focus_span_content_generation(const focus_span_state *state) {
    return state == 0 ? 0u : state->content_generation;
}

uint32_t focus_span_generation_for_object(const focus_span_state *state,
                                          uintptr_t object) {
    int slot = focus_span_find_object(state, object);
    return slot < 0 ? 0u : state->generations[slot].generation;
}

uint32_t focus_span_live_count(const focus_span_state *state) {
    if (state == 0) return 0u;
    return state->live_count > MTW_FOCUS_AUTHORITY_CAPACITY ?
           MTW_FOCUS_AUTHORITY_CAPACITY : state->live_count;
}
