#include <assert.h>
#include <stdio.h>

#include "../source/mapper_activation_core.h"

static void test_verified_loading_plane_disarms_mapper(void) {
    const uint32_t modes[][3] = {
        {2560u, 1440u, 5120u},
        {1920u, 1080u, 3840u},
        {1280u, 720u, 2560u},
        {1024u, 768u, 2048u},
        {3440u, 1440u, 6880u},
        {1600u, 900u, 3264u}
    };
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();
    unsigned int index;

    state.frontend_rearm_armed = 1;
    for (index = 0u; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        state = mtw_mapper_activation_after_loading(
            state, modes[index][0], modes[index][1], modes[index][2]);
        assert(state.mapper_mode == 0);
        assert(state.frontend_rearm_armed == 0);
        state.mapper_mode = 1;
        state.frontend_rearm_armed = 1;
    }
}

static void test_invalid_loading_plane_does_not_disarm_mapper(void) {
    mtw_mapper_activation_state initial = mtw_mapper_activation_initial();
    mtw_mapper_activation_state state;

    initial.frontend_rearm_armed = 1;
    state = mtw_mapper_activation_after_loading(
        initial, 0u, 1080u, 3840u);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 1);
    state = mtw_mapper_activation_after_loading(
        initial, 1920u, 0u, 3840u);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 1);
    state = mtw_mapper_activation_after_loading(
        initial, 1920u, 1080u, 3839u);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 1);
    state = mtw_mapper_activation_after_loading(
        initial, UINT32_MAX, 1u, UINT32_MAX);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 1);
}

static void test_render_input_root_and_800_surface_cannot_rearm(void) {
    mtw_mapper_activation_state state = {0, 1};

    state = mtw_mapper_activation_after_surface(
        state, 0, 800u, 600u, 1600u, 2u);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);
    state = mtw_mapper_activation_after_surface(
        state, 1, 800u, 600u, 1600u, 2u);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);
    state = mtw_mapper_activation_after_surface(
        state, 1, 640u, 480u, 2560u, 4u);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);
}

static void test_loading_does_not_rearm_while_frontend_mode_lingers(void) {
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();

    state = mtw_mapper_activation_after_loading(
        state, 800u, 600u, 1600u);
    state = mtw_mapper_activation_after_game_mode(
        state, 0u, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 0);
    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 0);
}

static void test_campaign_arms_but_does_not_rearm(void) {
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();

    state = mtw_mapper_activation_after_loading(
        state, 800u, 600u, 1600u);
    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);

    state = mtw_mapper_activation_after_surface(
        state, 1, 800u, 600u, 1600u, 2u);
    state = mtw_mapper_activation_after_surface(
        state, 1, 800u, 600u, 1600u, 2u);
    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);
}

static void test_campaign_setup_owner_preserves_but_never_rearms(void) {
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();

    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = (mtw_mapper_activation_state){0, 1};
    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);

    state = (mtw_mapper_activation_state){0, 0};
    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 0);
}

static void test_campaign_setup_requires_both_immutable_metadata_anchors(void) {
    const uintptr_t game_base = (uintptr_t)0x00400000u;
    uint32_t mask = 0u;

    mask |= mtw_mapper_campaign_setup_metadata_anchor(
        game_base, (uintptr_t)0x0080FFF8u);
    assert(mask == 1u);
    assert(mtw_mapper_campaign_setup_metadata_complete(mask) == 0);

    mask |= mtw_mapper_campaign_setup_metadata_anchor(
        game_base, (uintptr_t)0x0081012Cu);
    assert(mask == 3u);
    assert(mtw_mapper_campaign_setup_metadata_complete(mask) == 1);

    assert(mtw_mapper_campaign_setup_metadata_anchor(
               game_base, (uintptr_t)0x12000000u) == 0u);
    assert(mtw_mapper_campaign_setup_metadata_complete(1u) == 0);
    assert(mtw_mapper_campaign_setup_metadata_complete(2u) == 0);
}

static void test_prebattle_owner_rearms_immediately(void) {
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();

    state = mtw_mapper_activation_after_loading(
        state, 800u, 600u, 1600u);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_PREBATTLE);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_PREBATTLE);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);
}

static void test_prebattle_entry_callsite_must_match_exactly(void) {
    static const unsigned char supported[MTW_PREBATTLE_ENTRY_CALL_SIZE] = {
        0xE8, 0x42, 0x67, 0x00, 0x00
    };
    static const unsigned char supported_return[
        MTW_PREBATTLE_RESOLUTION_RETURN_SIZE] = {
        0x83, 0xC4, 0x10, 0x40, 0x89, 0x84,
        0x24, 0x84, 0x00, 0x00, 0x00
    };
    unsigned char corrupt[MTW_PREBATTLE_ENTRY_CALL_SIZE] = {
        0xE8, 0x42, 0x67, 0x00, 0x01
    };
    unsigned char corrupt_return[MTW_PREBATTLE_RESOLUTION_RETURN_SIZE] = {
        0x83, 0xC4, 0x10, 0x40, 0x89, 0x84,
        0x24, 0x84, 0x00, 0x00, 0x01
    };

    assert(mtw_prebattle_entry_callsite_supported(
        supported, sizeof(supported)) == 1);
    assert(mtw_prebattle_entry_callsite_supported(
        corrupt, sizeof(corrupt)) == 0);
    assert(mtw_prebattle_entry_callsite_supported(
        supported, sizeof(supported) - 1u) == 0);
    assert(mtw_prebattle_entry_callsite_supported(
        NULL, sizeof(supported)) == 0);
    assert(mtw_prebattle_resolution_return_supported(
        supported_return, sizeof(supported_return)) == 1);
    assert(mtw_prebattle_resolution_return_supported(
        corrupt_return, sizeof(corrupt_return)) == 0);
    assert(mtw_prebattle_resolution_return_supported(
        supported_return, sizeof(supported_return) - 1u) == 0);
    assert(mtw_prebattle_resolution_return_supported(
        NULL, sizeof(supported_return)) == 0);
}

static void test_prebattle_resolution_requires_a_live_controller(void) {
    mtw_mapper_activation_state state = {0, 1};

    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_PREBATTLE);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);
    state = mtw_mapper_activation_after_prebattle_resolution(
        state, 1u, 0);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);

    state = mtw_mapper_activation_after_game_mode(
        state, 1u, MTW_MAPPER_FRONTEND_OWNER_PREBATTLE);
    state = mtw_mapper_activation_after_prebattle_resolution(
        state, 1u, 1);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);
}

static void test_genuine_menu_return_rearms_without_surface_replacement(void) {
    mtw_mapper_activation_state state = {0, 1};

    state = mtw_mapper_activation_after_game_mode(
        state, 0u, MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = (mtw_mapper_activation_state){0, 1};
    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);
}

static void test_in_game_menu_rearms_mapper(void) {
    mtw_mapper_activation_state state = {0, 1};

    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_surface(
        state, 1, 800u, 600u, 1600u, 2u);
    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);
}

static void test_quick_battle_results_owner_rearms_immediately(void) {
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();

    state = mtw_mapper_activation_after_loading(
        state, 640u, 480u, 1280u);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_game_mode(
        state, 12u, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);

    state = mtw_mapper_activation_after_game_mode(
        state, 12u, MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_game_mode(
        state, 12u, MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);

    state = mtw_mapper_activation_after_game_mode(
        state, 11u, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);
}

static void test_unknown_frontend_owner_fails_closed_until_main_menu(void) {
    mtw_mapper_activation_state state = {0, 1};

    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    assert(state.mapper_mode == 0);
    assert(state.frontend_rearm_armed == 1);

    state = mtw_mapper_activation_after_game_mode(
        state, UINT32_MAX, MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU);
    assert(state.mapper_mode == 1);
    assert(state.frontend_rearm_armed == 0);
}

static void test_page_metadata_identifies_frontend_owner(void) {
    const uintptr_t game_base = (uintptr_t)0x00400000u;
    mtw_mapper_frontend_owner_kind owner =
        MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;

    owner = mtw_mapper_frontend_owner_observe_metadata(
        owner, game_base, (uintptr_t)0x00804B30u);
    assert(owner == MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU);

    owner = mtw_mapper_frontend_owner_observe_metadata(
        MTW_MAPPER_FRONTEND_OWNER_UNKNOWN,
        game_base, (uintptr_t)0x00803C10u);
    assert(owner == MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU);

    owner = mtw_mapper_frontend_owner_observe_metadata(
        MTW_MAPPER_FRONTEND_OWNER_UNKNOWN,
        game_base, (uintptr_t)0x0086CA68u);
    assert(owner == MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS);

    owner = mtw_mapper_frontend_owner_observe_metadata(
        MTW_MAPPER_FRONTEND_OWNER_UNKNOWN,
        game_base, (uintptr_t)0x0086CB9Cu);
    assert(owner == MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS);

    owner = mtw_mapper_frontend_owner_observe_metadata(
        MTW_MAPPER_FRONTEND_OWNER_UNKNOWN,
        game_base, (uintptr_t)0x12697BD0u);
    assert(owner == MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);

    owner = mtw_mapper_frontend_owner_observe_metadata(
        MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU,
        game_base, (uintptr_t)0x00803C10u);
    assert(owner == MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU);
}

static void test_repeated_frontend_gameplay_transitions(void) {
    mtw_mapper_activation_state state = mtw_mapper_activation_initial();
    unsigned int iteration;

    for (iteration = 0u; iteration < 4u; ++iteration) {
        state = mtw_mapper_activation_after_loading(
            state, iteration == 0u ? 800u : 1920u,
            iteration == 0u ? 600u : 1080u,
            iteration == 0u ? 1600u : 3840u);
        assert(state.mapper_mode == 0);
        assert(state.frontend_rearm_armed == 0);

        state = mtw_mapper_activation_after_game_mode(
            state, iteration + 1u,
            MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
        assert(state.mapper_mode == 0);
        assert(state.frontend_rearm_armed == 1);

        state = mtw_mapper_activation_after_surface(
            state, 1, 800u, 600u, 1600u, 2u);
        assert(state.mapper_mode == 0);

        state = mtw_mapper_activation_after_game_mode(
            state, (iteration & 1u) == 0u ? 0u : UINT32_MAX,
            MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU);
        assert(state.mapper_mode == 1);
        assert(state.frontend_rearm_armed == 0);
    }
}

int main(void) {
    test_verified_loading_plane_disarms_mapper();
    test_invalid_loading_plane_does_not_disarm_mapper();
    test_render_input_root_and_800_surface_cannot_rearm();
    test_loading_does_not_rearm_while_frontend_mode_lingers();
    test_campaign_arms_but_does_not_rearm();
    test_campaign_setup_owner_preserves_but_never_rearms();
    test_campaign_setup_requires_both_immutable_metadata_anchors();
    test_prebattle_owner_rearms_immediately();
    test_prebattle_entry_callsite_must_match_exactly();
    test_prebattle_resolution_requires_a_live_controller();
    test_genuine_menu_return_rearms_without_surface_replacement();
    test_in_game_menu_rearms_mapper();
    test_quick_battle_results_owner_rearms_immediately();
    test_unknown_frontend_owner_fails_closed_until_main_menu();
    test_page_metadata_identifies_frontend_owner();
    test_repeated_frontend_gameplay_transitions();
    puts("mapper activation tests passed");
    return 0;
}
