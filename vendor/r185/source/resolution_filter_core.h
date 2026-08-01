#ifndef MTW_RESOLUTION_FILTER_CORE_H
#define MTW_RESOLUTION_FILTER_CORE_H

#include <stdint.h>

#define MTW_RESOLUTION_FILTER_CAPACITY 512u
#define MTW_RESOLUTION_MAX_DIMENSION 65535u

typedef enum mtw_resolution_selector {
    MTW_RESOLUTION_SELECTOR_STRATEGY = 1,
    MTW_RESOLUTION_SELECTOR_BATTLE = 3
} mtw_resolution_selector;

#pragma pack(push, 1)
typedef struct mtw_resolution_record {
    uint8_t flags[4];
    uint32_t width;
    uint32_t height;
    uint32_t format_or_field0c;
    uint32_t field10;
    uint32_t refresh_or_field14;
    uint32_t field18;
    uint32_t field1c;
} mtw_resolution_record;
#pragma pack(pop)

typedef struct mtw_resolution_filter_result {
    uint32_t strategy_records;
    uint32_t battle_records;
    uint32_t rejected_records;
    int recaptured_baseline;
} mtw_resolution_filter_result;

typedef struct mtw_resolution_filter_state {
    uintptr_t entries_identity;
    uintptr_t output_identity;
    uint32_t baseline_count;
    uint32_t cap_width;
    uint32_t cap_height;
    uint32_t output_generation;
    int initialized;
    int operation_active;
    int selectors_initialized;
    uint32_t selector_enumeration_generation;
    uint32_t baseline_strategy_index_count;
    uint32_t baseline_battle_index_count;
    uint32_t baseline_width[MTW_RESOLUTION_FILTER_CAPACITY];
    uint32_t baseline_height[MTW_RESOLUTION_FILTER_CAPACITY];
    uint8_t baseline_strategy[MTW_RESOLUTION_FILTER_CAPACITY];
    uint8_t baseline_battle[MTW_RESOLUTION_FILTER_CAPACITY];
    uint32_t baseline_strategy_indices[MTW_RESOLUTION_FILTER_CAPACITY];
    uint32_t baseline_battle_indices[MTW_RESOLUTION_FILTER_CAPACITY];
} mtw_resolution_filter_state;

void mtw_resolution_filter_initialize(
    mtw_resolution_filter_state *state);
int mtw_resolution_cap_valid(uint32_t width, uint32_t height);
int mtw_resolution_candidate_fits(uint32_t candidate_width,
                                  uint32_t candidate_height,
                                  uint32_t cap_width,
                                  uint32_t cap_height);
int mtw_resolution_filter_apply(
    mtw_resolution_filter_state *state,
    mtw_resolution_record *records,
    uint32_t count,
    uintptr_t entries_identity,
    uintptr_t output_identity,
    uint32_t cap_width,
    uint32_t cap_height,
    mtw_resolution_filter_result *result);
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
    mtw_resolution_filter_result *result);
int mtw_resolution_select_fallback(
    const mtw_resolution_record *records,
    uint32_t count,
    mtw_resolution_selector selector,
    uint32_t selected_width,
    uint32_t selected_height,
    uint32_t *fallback_width,
    uint32_t *fallback_height);
int mtw_resolution_conservative_cap(
    const mtw_resolution_record *records,
    uint32_t count,
    uint32_t *cap_width,
    uint32_t *cap_height);

#endif
