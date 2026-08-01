#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/resolution_filter_core.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static mtw_resolution_record record(uint32_t width, uint32_t height,
                                    uint8_t strategy, uint8_t battle,
                                    uint32_t refresh) {
    mtw_resolution_record item;

    memset(&item, 0, sizeof(item));
    item.flags[0] = 1u;
    item.flags[1] = strategy;
    item.flags[3] = battle;
    item.width = width;
    item.height = height;
    item.refresh_or_field14 = refresh;
    return item;
}

static int apply(mtw_resolution_filter_state *state,
                 mtw_resolution_record *records, uint32_t count,
                 uintptr_t identity, uintptr_t output_identity,
                 uint32_t width, uint32_t height,
                 mtw_resolution_filter_result *result) {
    return mtw_resolution_filter_apply(
        state, records, count, identity, output_identity,
        width, height, result);
}

static int test_componentwise_caps(void) {
    mtw_resolution_record records[] = {
        {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}
    };
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;

    records[0] = record(800u, 600u, 1u, 1u, 60u);
    records[1] = record(1280u, 1024u, 1u, 1u, 60u);
    records[2] = record(1600u, 1200u, 1u, 1u, 60u);
    records[3] = record(1920u, 1080u, 1u, 1u, 60u);
    records[4] = record(1920u, 1200u, 1u, 1u, 60u);
    records[5] = record(2560u, 1440u, 1u, 1u, 60u);
    records[6] = record(2560u, 1600u, 1u, 1u, 60u);
    records[7] = record(3840u, 2160u, 1u, 1u, 60u);

    mtw_resolution_filter_initialize(&state);
    CHECK(apply(&state, records, 8u, 0x1000u, 0xA0u,
                1920u, 1080u, &result));
    CHECK(records[0].flags[1] == 1u && records[0].flags[3] == 1u);
    CHECK(records[1].flags[1] == 1u && records[1].flags[3] == 1u);
    CHECK(records[2].flags[1] == 0u && records[2].flags[3] == 0u);
    CHECK(records[3].flags[1] == 1u && records[3].flags[3] == 1u);
    CHECK(records[4].flags[1] == 0u && records[4].flags[3] == 0u);
    CHECK(records[5].flags[1] == 0u && records[5].flags[3] == 0u);
    CHECK(records[6].flags[1] == 0u && records[6].flags[3] == 0u);
    CHECK(records[7].flags[1] == 0u && records[7].flags[3] == 0u);
    CHECK(result.strategy_records == 3u);
    CHECK(result.battle_records == 3u);
    CHECK(result.rejected_records == 5u);
    return 0;
}

static int test_1440p_4k_and_portrait_caps(void) {
    mtw_resolution_record records[] = {
        {0}, {0}, {0}, {0}, {0}
    };
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;

    records[0] = record(1024u, 768u, 1u, 1u, 60u);
    records[1] = record(1440u, 1080u, 1u, 1u, 60u);
    records[2] = record(1920u, 1440u, 1u, 1u, 60u);
    records[3] = record(2560u, 1440u, 1u, 1u, 60u);
    records[4] = record(3840u, 2160u, 1u, 1u, 60u);

    mtw_resolution_filter_initialize(&state);
    CHECK(apply(&state, records, 5u, 0x2000u, 0xB0u,
                2560u, 1440u, &result));
    CHECK(records[0].flags[1] == 1u);
    CHECK(records[1].flags[1] == 1u);
    CHECK(records[2].flags[1] == 1u);
    CHECK(records[3].flags[1] == 1u);
    CHECK(records[4].flags[1] == 0u);

    CHECK(apply(&state, records, 5u, 0x2000u, 0xB0u,
                3840u, 2160u, &result));
    CHECK(records[4].flags[1] == 1u);

    CHECK(apply(&state, records, 5u, 0x2000u, 0xC0u,
                1440u, 2560u, &result));
    CHECK(records[0].flags[1] == 1u);
    CHECK(records[1].flags[1] == 1u);
    CHECK(records[2].flags[1] == 0u);
    CHECK(records[3].flags[1] == 0u);
    CHECK(records[4].flags[1] == 0u);
    return 0;
}

static int test_duplicates_refresh_and_order_are_preserved(void) {
    mtw_resolution_record records[] = {
        {0}, {0}, {0}, {0}
    };
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;

    records[0] = record(1920u, 1080u, 1u, 1u, 60u);
    records[1] = record(1920u, 1080u, 1u, 1u, 120u);
    records[2] = record(2560u, 1440u, 1u, 1u, 60u);
    records[3] = record(1280u, 1024u, 1u, 1u, 75u);
    records[0].format_or_field0c = 16u;
    records[1].format_or_field0c = 32u;

    mtw_resolution_filter_initialize(&state);
    CHECK(apply(&state, records, 4u, 0x3000u, 0xD0u,
                1920u, 1080u, &result));
    CHECK(records[0].flags[1] == 1u);
    CHECK(records[1].flags[1] == 1u);
    CHECK(records[2].flags[1] == 0u);
    CHECK(records[3].flags[1] == 1u);
    CHECK(records[0].refresh_or_field14 == 60u);
    CHECK(records[1].refresh_or_field14 == 120u);
    CHECK(records[0].format_or_field0c == 16u);
    CHECK(records[1].format_or_field0c == 32u);
    CHECK(records[3].width == 1280u && records[3].height == 1024u);
    return 0;
}

static int test_monitor_refresh_restores_original_flags(void) {
    mtw_resolution_record records[] = {{0}, {0}, {0}};
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;
    uint32_t first_generation;

    records[0] = record(1280u, 720u, 1u, 1u, 60u);
    records[1] = record(1920u, 1080u, 1u, 1u, 60u);
    records[2] = record(2560u, 1440u, 1u, 1u, 60u);
    mtw_resolution_filter_initialize(&state);

    CHECK(apply(&state, records, 3u, 0x4000u, 0xE0u,
                1920u, 1080u, &result));
    CHECK(records[2].flags[1] == 0u);
    first_generation = state.output_generation;

    CHECK(apply(&state, records, 3u, 0x4000u, 0xF0u,
                2560u, 1440u, &result));
    CHECK(records[2].flags[1] == 1u);
    CHECK(state.output_generation == first_generation + 1u);

    CHECK(apply(&state, records, 3u, 0x4000u, 0x100u,
                2560u, 1440u, &result));
    CHECK(records[2].flags[1] == 1u);
    CHECK(state.output_generation == first_generation + 2u);

    records[2] = record(3840u, 2160u, 1u, 1u, 144u);
    CHECK(apply(&state, records, 3u, 0x5000u, 0x100u,
                2560u, 1440u, &result));
    CHECK(result.recaptured_baseline == 1);
    CHECK(records[2].flags[1] == 0u);
    return 0;
}

static int test_original_selector_independence_is_preserved(void) {
    mtw_resolution_record records[] = {{0}, {0}, {0}};
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;

    records[0] = record(640u, 480u, 0u, 1u, 60u);
    records[1] = record(800u, 600u, 1u, 1u, 60u);
    records[2] = record(3840u, 2160u, 1u, 0u, 60u);
    mtw_resolution_filter_initialize(&state);
    CHECK(apply(&state, records, 3u, 0x6000u, 0x110u,
                1920u, 1080u, &result));
    CHECK(records[0].flags[1] == 0u && records[0].flags[3] == 1u);
    CHECK(records[1].flags[1] == 1u && records[1].flags[3] == 1u);
    CHECK(records[2].flags[1] == 0u && records[2].flags[3] == 0u);
    return 0;
}

static int test_cached_selector_indices_are_filtered_and_restored(void) {
    mtw_resolution_record records[] = {{0}, {0}, {0}, {0}, {0}};
    uint32_t strategy_indices[5] = {1u, 2u, 3u, 4u, 0u};
    uint32_t battle_indices[5] = {0u, 1u, 2u, 3u, 4u};
    uint32_t strategy_count = 4u;
    uint32_t battle_count = 5u;
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;

    records[0] = record(640u, 480u, 0u, 1u, 60u);
    records[1] = record(800u, 600u, 1u, 1u, 60u);
    records[2] = record(1920u, 1080u, 1u, 1u, 60u);
    records[3] = record(2560u, 1440u, 1u, 1u, 60u);
    records[4] = record(3840u, 2160u, 1u, 1u, 60u);
    mtw_resolution_filter_initialize(&state);

    CHECK(mtw_resolution_filter_apply_selectors(
        &state, records, 5u, 0x6100u, 0x111u, 1u,
        2560u, 1440u,
        strategy_indices, &strategy_count, 5u,
        battle_indices, &battle_count, 5u, &result));
    CHECK(strategy_count == 3u);
    CHECK(strategy_indices[0] == 1u);
    CHECK(strategy_indices[1] == 2u);
    CHECK(strategy_indices[2] == 3u);
    CHECK(battle_count == 4u);
    CHECK(battle_indices[0] == 0u);
    CHECK(battle_indices[1] == 1u);
    CHECK(battle_indices[2] == 2u);
    CHECK(battle_indices[3] == 3u);
    CHECK(records[4].flags[1] == 0u && records[4].flags[3] == 0u);

    CHECK(mtw_resolution_filter_apply_selectors(
        &state, records, 5u, 0x6100u, 0x112u, 1u,
        3840u, 2160u,
        strategy_indices, &strategy_count, 5u,
        battle_indices, &battle_count, 5u, &result));
    CHECK(strategy_count == 4u);
    CHECK(strategy_indices[3] == 4u);
    CHECK(battle_count == 5u);
    CHECK(battle_indices[4] == 4u);
    CHECK(records[4].flags[1] == 1u && records[4].flags[3] == 1u);

    strategy_indices[0] = 1u;
    strategy_indices[1] = 2u;
    strategy_indices[2] = 4u;
    strategy_count = 3u;
    battle_indices[0] = 0u;
    battle_indices[1] = 1u;
    battle_indices[2] = 2u;
    battle_indices[3] = 4u;
    battle_count = 4u;
    records[3].flags[1] = 0u;
    records[3].flags[3] = 0u;
    CHECK(mtw_resolution_filter_apply_selectors(
        &state, records, 5u, 0x6100u, 0x112u, 2u,
        2560u, 1440u,
        strategy_indices, &strategy_count, 5u,
        battle_indices, &battle_count, 5u, &result));
    CHECK(result.recaptured_baseline == 1);
    CHECK(strategy_count == 2u);
    CHECK(strategy_indices[0] == 1u && strategy_indices[1] == 2u);
    CHECK(battle_count == 3u);
    CHECK(battle_indices[0] == 0u && battle_indices[1] == 1u &&
          battle_indices[2] == 2u);
    return 0;
}

static int test_invalid_caps_capacity_and_reentrancy_fail_closed(void) {
    mtw_resolution_record records[] = {{0}, {0}};
    mtw_resolution_record before[2];
    mtw_resolution_filter_state state;
    mtw_resolution_filter_result result;

    records[0] = record(800u, 600u, 1u, 1u, 60u);
    records[1] = record(1920u, 1080u, 1u, 1u, 60u);
    memcpy(before, records, sizeof(records));
    mtw_resolution_filter_initialize(&state);

    CHECK(!apply(&state, records, 2u, 0x7000u, 0x120u,
                 0u, 1080u, &result));
    CHECK(memcmp(before, records, sizeof(records)) == 0);
    CHECK(!apply(&state, records, 2u, 0x7000u, 0x120u,
                 UINT32_MAX, 1080u, &result));
    CHECK(memcmp(before, records, sizeof(records)) == 0);
    state.operation_active = 1;
    CHECK(!apply(&state, records, 2u, 0x7000u, 0x120u,
                 1920u, 1080u, &result));
    CHECK(memcmp(before, records, sizeof(records)) == 0);
    state.operation_active = 0;
    CHECK(!apply(&state, records, MTW_RESOLUTION_FILTER_CAPACITY + 1u,
                 0x7000u, 0x120u, 1920u, 1080u, &result));
    CHECK(memcmp(before, records, sizeof(records)) == 0);
    return 0;
}

static int test_fallback_never_invents_a_mode(void) {
    mtw_resolution_record records[] = {{0}, {0}, {0}, {0}};
    uint32_t width = 0u;
    uint32_t height = 0u;

    records[0] = record(640u, 480u, 0u, 1u, 60u);
    records[1] = record(800u, 600u, 1u, 1u, 60u);
    records[2] = record(1280u, 1024u, 1u, 1u, 60u);
    records[3] = record(1920u, 1080u, 1u, 1u, 60u);

    CHECK(mtw_resolution_select_fallback(
        records, 4u, MTW_RESOLUTION_SELECTOR_STRATEGY,
        3840u, 2160u, &width, &height));
    CHECK(width == 1920u && height == 1080u);
    CHECK(mtw_resolution_select_fallback(
        records, 4u, MTW_RESOLUTION_SELECTOR_BATTLE,
        3840u, 2160u, &width, &height));
    CHECK(width == 1920u && height == 1080u);

    records[1].flags[1] = 0u;
    records[2].flags[1] = 0u;
    records[3].flags[1] = 0u;
    CHECK(!mtw_resolution_select_fallback(
        records, 4u, MTW_RESOLUTION_SELECTOR_STRATEGY,
        3840u, 2160u, &width, &height));

    records[0].flags[3] = 0u;
    records[1].flags[3] = 1u;
    records[2].flags[3] = 0u;
    records[3].flags[3] = 0u;
    CHECK(mtw_resolution_select_fallback(
        records, 4u, MTW_RESOLUTION_SELECTOR_BATTLE,
        3840u, 2160u, &width, &height));
    CHECK(width == 800u && height == 600u);
    return 0;
}

static int test_conservative_cap_uses_existing_safe_modes(void) {
    mtw_resolution_record records[] = {{0}, {0}, {0}};
    uint32_t width = 0u;
    uint32_t height = 0u;

    records[0] = record(640u, 480u, 0u, 1u, 60u);
    records[1] = record(800u, 600u, 1u, 1u, 60u);
    records[2] = record(1920u, 1080u, 1u, 1u, 60u);
    CHECK(mtw_resolution_conservative_cap(
        records, 3u, &width, &height));
    CHECK(width == 800u && height == 600u);
    CHECK(mtw_resolution_candidate_fits(640u, 480u, width, height));
    CHECK(mtw_resolution_candidate_fits(800u, 600u, width, height));
    CHECK(!mtw_resolution_candidate_fits(1920u, 1080u, width, height));
    return 0;
}

int main(void) {
    CHECK(sizeof(mtw_resolution_record) == 32u);
    CHECK(test_componentwise_caps() == 0);
    CHECK(test_1440p_4k_and_portrait_caps() == 0);
    CHECK(test_duplicates_refresh_and_order_are_preserved() == 0);
    CHECK(test_monitor_refresh_restores_original_flags() == 0);
    CHECK(test_original_selector_independence_is_preserved() == 0);
    CHECK(test_cached_selector_indices_are_filtered_and_restored() == 0);
    CHECK(test_invalid_caps_capacity_and_reentrancy_fail_closed() == 0);
    CHECK(test_fallback_never_invents_a_mode() == 0);
    CHECK(test_conservative_cap_uses_existing_safe_modes() == 0);
    puts("resolution filter tests passed");
    return 0;
}
