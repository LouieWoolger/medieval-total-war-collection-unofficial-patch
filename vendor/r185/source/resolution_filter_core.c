#include "resolution_filter_core.h"

#include <stddef.h>
#include <string.h>

void mtw_resolution_filter_initialize(
    mtw_resolution_filter_state *state) {
    if (state != NULL) memset(state, 0, sizeof(*state));
}

int mtw_resolution_cap_valid(uint32_t width, uint32_t height) {
    return width > 0u && height > 0u &&
           width <= MTW_RESOLUTION_MAX_DIMENSION &&
           height <= MTW_RESOLUTION_MAX_DIMENSION;
}

int mtw_resolution_candidate_fits(uint32_t candidate_width,
                                  uint32_t candidate_height,
                                  uint32_t cap_width,
                                  uint32_t cap_height) {
    return mtw_resolution_cap_valid(candidate_width, candidate_height) &&
           mtw_resolution_cap_valid(cap_width, cap_height) &&
           candidate_width <= cap_width && candidate_height <= cap_height;
}

static int baseline_dimensions_match(
    const mtw_resolution_filter_state *state,
    const mtw_resolution_record *records,
    uint32_t count) {
    uint32_t index;

    if (!state->initialized || state->baseline_count != count) return 0;
    for (index = 0u; index < count; ++index) {
        if (records[index].width != state->baseline_width[index] ||
            records[index].height != state->baseline_height[index]) {
            return 0;
        }
    }
    return 1;
}

static void capture_baseline(mtw_resolution_filter_state *state,
                             const mtw_resolution_record *records,
                             uint32_t count,
                             uintptr_t entries_identity) {
    uint32_t index;

    state->entries_identity = entries_identity;
    state->baseline_count = count;
    for (index = 0u; index < count; ++index) {
        state->baseline_width[index] = records[index].width;
        state->baseline_height[index] = records[index].height;
        state->baseline_strategy[index] = records[index].flags[1];
        state->baseline_battle[index] = records[index].flags[3];
    }
    state->initialized = 1;
}

int mtw_resolution_filter_apply(
    mtw_resolution_filter_state *state,
    mtw_resolution_record *records,
    uint32_t count,
    uintptr_t entries_identity,
    uintptr_t output_identity,
    uint32_t cap_width,
    uint32_t cap_height,
    mtw_resolution_filter_result *result) {
    uint32_t index;
    int recapture;

    if (state == NULL || records == NULL || result == NULL || count == 0u ||
        count > MTW_RESOLUTION_FILTER_CAPACITY || entries_identity == 0u ||
        output_identity == 0u ||
        !mtw_resolution_cap_valid(cap_width, cap_height) ||
        state->operation_active) {
        return 0;
    }

    state->operation_active = 1;
    memset(result, 0, sizeof(*result));
    recapture = !state->initialized ||
                state->entries_identity != entries_identity ||
                !baseline_dimensions_match(state, records, count);
    if (recapture) {
        capture_baseline(state, records, count, entries_identity);
        result->recaptured_baseline = 1;
    }

    if (state->output_identity != output_identity ||
        state->cap_width != cap_width || state->cap_height != cap_height) {
        state->output_generation++;
    }
    state->output_identity = output_identity;
    state->cap_width = cap_width;
    state->cap_height = cap_height;

    for (index = 0u; index < count; ++index) {
        int fits = mtw_resolution_candidate_fits(
            records[index].width, records[index].height,
            cap_width, cap_height);
        int was_available = state->baseline_strategy[index] != 0u ||
                            state->baseline_battle[index] != 0u;

        records[index].flags[1] = fits ?
            state->baseline_strategy[index] : 0u;
        records[index].flags[3] = fits ?
            state->baseline_battle[index] : 0u;
        if (records[index].flags[1] != 0u) result->strategy_records++;
        if (records[index].flags[3] != 0u) result->battle_records++;
        if (!fits && was_available) result->rejected_records++;
    }
    state->operation_active = 0;
    return result->strategy_records > 0u && result->battle_records > 0u;
}

static int selector_indices_valid(const uint32_t *indices,
                                  uint32_t index_count,
                                  uint32_t capacity,
                                  uint32_t record_count) {
    uint32_t index;

    if (indices == NULL || index_count == 0u ||
        index_count > capacity || capacity > MTW_RESOLUTION_FILTER_CAPACITY) {
        return 0;
    }
    for (index = 0u; index < index_count; ++index) {
        if (indices[index] >= record_count) return 0;
    }
    return 1;
}

static void capture_selector_baseline(
    mtw_resolution_filter_state *state,
    const mtw_resolution_record *records,
    uint32_t count,
    uintptr_t entries_identity,
    uint32_t enumeration_generation,
    const uint32_t *strategy_indices,
    uint32_t strategy_count,
    const uint32_t *battle_indices,
    uint32_t battle_count) {
    capture_baseline(state, records, count, entries_identity);
    memcpy(state->baseline_strategy_indices, strategy_indices,
           (size_t)strategy_count * sizeof(strategy_indices[0]));
    memcpy(state->baseline_battle_indices, battle_indices,
           (size_t)battle_count * sizeof(battle_indices[0]));
    state->baseline_strategy_index_count = strategy_count;
    state->baseline_battle_index_count = battle_count;
    state->selector_enumeration_generation = enumeration_generation;
    state->selectors_initialized = 1;
}

static uint32_t filtered_selector_count(
    const mtw_resolution_filter_state *state,
    mtw_resolution_selector selector,
    uint32_t cap_width,
    uint32_t cap_height) {
    const uint32_t *indices;
    uint32_t count;
    uint32_t output_count = 0u;
    uint32_t index;

    if (selector == MTW_RESOLUTION_SELECTOR_STRATEGY) {
        indices = state->baseline_strategy_indices;
        count = state->baseline_strategy_index_count;
    } else {
        indices = state->baseline_battle_indices;
        count = state->baseline_battle_index_count;
    }
    for (index = 0u; index < count; ++index) {
        uint32_t record_index = indices[index];

        if (mtw_resolution_candidate_fits(
                state->baseline_width[record_index],
                state->baseline_height[record_index],
                cap_width, cap_height)) {
            output_count++;
        }
    }
    return output_count;
}

static void write_filtered_selector(
    const mtw_resolution_filter_state *state,
    mtw_resolution_selector selector,
    uint32_t cap_width,
    uint32_t cap_height,
    uint32_t *indices,
    uint32_t *output_count) {
    const uint32_t *baseline_indices;
    uint32_t baseline_count;
    uint32_t source_index;
    uint32_t destination_index = 0u;

    if (selector == MTW_RESOLUTION_SELECTOR_STRATEGY) {
        baseline_indices = state->baseline_strategy_indices;
        baseline_count = state->baseline_strategy_index_count;
    } else {
        baseline_indices = state->baseline_battle_indices;
        baseline_count = state->baseline_battle_index_count;
    }
    for (source_index = 0u; source_index < baseline_count; ++source_index) {
        uint32_t record_index = baseline_indices[source_index];

        if (!mtw_resolution_candidate_fits(
                state->baseline_width[record_index],
                state->baseline_height[record_index],
                cap_width, cap_height)) {
            continue;
        }
        indices[destination_index++] = record_index;
    }
    *output_count = destination_index;
}

int mtw_resolution_filter_apply_selectors(
    mtw_resolution_filter_state *state,
    mtw_resolution_record *records,
    uint32_t count,
    uintptr_t entries_identity,
    uintptr_t output_identity,
    uint32_t enumeration_generation,
    uint32_t cap_width,
    uint32_t cap_height,
    uint32_t *strategy_indices,
    uint32_t *strategy_count,
    uint32_t strategy_capacity,
    uint32_t *battle_indices,
    uint32_t *battle_count,
    uint32_t battle_capacity,
    mtw_resolution_filter_result *result) {
    uint32_t index;
    uint32_t filtered_strategy_count;
    uint32_t filtered_battle_count;
    int recapture;

    if (state == NULL || records == NULL || result == NULL ||
        strategy_count == NULL || battle_count == NULL || count == 0u ||
        count > MTW_RESOLUTION_FILTER_CAPACITY || entries_identity == 0u ||
        output_identity == 0u || enumeration_generation == 0u ||
        !mtw_resolution_cap_valid(cap_width, cap_height) ||
        state->operation_active ||
        !selector_indices_valid(strategy_indices, *strategy_count,
                                strategy_capacity, count) ||
        !selector_indices_valid(battle_indices, *battle_count,
                                battle_capacity, count)) {
        return 0;
    }

    state->operation_active = 1;
    memset(result, 0, sizeof(*result));
    recapture = !state->initialized ||
                state->entries_identity != entries_identity ||
                !baseline_dimensions_match(state, records, count) ||
                !state->selectors_initialized ||
                state->selector_enumeration_generation !=
                    enumeration_generation;
    if (recapture) {
        capture_selector_baseline(
            state, records, count, entries_identity,
            enumeration_generation,
            strategy_indices, *strategy_count,
            battle_indices, *battle_count);
        result->recaptured_baseline = 1;
    }

    filtered_strategy_count = filtered_selector_count(
        state, MTW_RESOLUTION_SELECTOR_STRATEGY,
        cap_width, cap_height);
    filtered_battle_count = filtered_selector_count(
        state, MTW_RESOLUTION_SELECTOR_BATTLE,
        cap_width, cap_height);
    if (filtered_strategy_count == 0u || filtered_battle_count == 0u ||
        filtered_strategy_count > strategy_capacity ||
        filtered_battle_count > battle_capacity) {
        state->operation_active = 0;
        return 0;
    }

    if (state->output_identity != output_identity ||
        state->cap_width != cap_width || state->cap_height != cap_height) {
        state->output_generation++;
    }
    state->output_identity = output_identity;
    state->cap_width = cap_width;
    state->cap_height = cap_height;
    for (index = 0u; index < count; ++index) {
        int fits = mtw_resolution_candidate_fits(
            state->baseline_width[index], state->baseline_height[index],
            cap_width, cap_height);
        int was_available = state->baseline_strategy[index] != 0u ||
                            state->baseline_battle[index] != 0u;

        records[index].flags[1] = fits ?
            state->baseline_strategy[index] : 0u;
        records[index].flags[3] = fits ?
            state->baseline_battle[index] : 0u;
        if (records[index].flags[1] != 0u) result->strategy_records++;
        if (records[index].flags[3] != 0u) result->battle_records++;
        if (!fits && was_available) result->rejected_records++;
    }
    write_filtered_selector(
        state, MTW_RESOLUTION_SELECTOR_STRATEGY,
        cap_width, cap_height, strategy_indices, strategy_count);
    write_filtered_selector(
        state, MTW_RESOLUTION_SELECTOR_BATTLE,
        cap_width, cap_height, battle_indices, battle_count);
    state->operation_active = 0;
    return 1;
}

int mtw_resolution_select_fallback(
    const mtw_resolution_record *records,
    uint32_t count,
    mtw_resolution_selector selector,
    uint32_t selected_width,
    uint32_t selected_height,
    uint32_t *fallback_width,
    uint32_t *fallback_height) {
    uint32_t index;
    uint32_t selected_index = UINT32_MAX;
    uint32_t fallback_index = UINT32_MAX;

    if (records == NULL || fallback_width == NULL ||
        fallback_height == NULL || count == 0u ||
        count > MTW_RESOLUTION_FILTER_CAPACITY ||
        (selector != MTW_RESOLUTION_SELECTOR_STRATEGY &&
         selector != MTW_RESOLUTION_SELECTOR_BATTLE)) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        uint8_t available = records[index].flags[(unsigned int)selector];

        if (available == 0u ||
            !mtw_resolution_cap_valid(
                records[index].width, records[index].height)) {
            continue;
        }
        fallback_index = index;
        if (records[index].width == selected_width &&
            records[index].height == selected_height) {
            selected_index = index;
            break;
        }
    }
    if (selected_index != UINT32_MAX) fallback_index = selected_index;
    if (fallback_index == UINT32_MAX) return 0;
    *fallback_width = records[fallback_index].width;
    *fallback_height = records[fallback_index].height;
    return 1;
}

static int mode_is_smaller(uint32_t width, uint32_t height,
                           uint32_t best_width, uint32_t best_height) {
    uint64_t area = (uint64_t)width * (uint64_t)height;
    uint64_t best_area = (uint64_t)best_width * (uint64_t)best_height;

    return best_width == 0u || area < best_area ||
           (area == best_area && width < best_width) ||
           (area == best_area && width == best_width &&
            height < best_height);
}

int mtw_resolution_conservative_cap(
    const mtw_resolution_record *records,
    uint32_t count,
    uint32_t *cap_width,
    uint32_t *cap_height) {
    uint32_t strategy_width = 0u;
    uint32_t strategy_height = 0u;
    uint32_t battle_width = 0u;
    uint32_t battle_height = 0u;
    uint32_t index;

    if (records == NULL || cap_width == NULL || cap_height == NULL ||
        count == 0u || count > MTW_RESOLUTION_FILTER_CAPACITY) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        uint32_t width = records[index].width;
        uint32_t height = records[index].height;

        if (!mtw_resolution_cap_valid(width, height)) continue;
        if (records[index].flags[1] != 0u &&
            mode_is_smaller(width, height,
                            strategy_width, strategy_height)) {
            strategy_width = width;
            strategy_height = height;
        }
        if (records[index].flags[3] != 0u &&
            mode_is_smaller(width, height,
                            battle_width, battle_height)) {
            battle_width = width;
            battle_height = height;
        }
    }
    if (strategy_width == 0u || battle_width == 0u) return 0;
    *cap_width = strategy_width > battle_width ?
        strategy_width : battle_width;
    *cap_height = strategy_height > battle_height ?
        strategy_height : battle_height;
    return mtw_resolution_cap_valid(*cap_width, *cap_height);
}
