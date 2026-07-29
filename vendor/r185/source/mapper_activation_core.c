#include "mapper_activation_core.h"

#include <string.h>

#define MTW_LOADING_BYTES_PER_PIXEL 2u
#define MTW_LOADING_MAX_DIMENSION 32768u
#define MTW_LOADING_MAX_BYTES 0x20000000u

#define MTW_GAME_MODE_FRONTEND_ENTRY 0u
#define MTW_GAME_MODE_FRONTEND_IDLE UINT32_MAX
#define MTW_MAIN_MENU_INITIAL_METADATA_RVA ((uintptr_t)0x00404B30u)
#define MTW_IN_GAME_MENU_METADATA_RVA ((uintptr_t)0x00403C10u)
#define MTW_BATTLE_RESULTS_METADATA_RVA ((uintptr_t)0x0046CA68u)
#define MTW_BATTLE_RESULTS_UPDATE_METADATA_RVA ((uintptr_t)0x0046CB9Cu)
#define MTW_CAMPAIGN_SETUP_HEADER_METADATA_RVA ((uintptr_t)0x0040FFF8u)
#define MTW_CAMPAIGN_SETUP_DIFFICULTY_METADATA_RVA ((uintptr_t)0x0041012Cu)
#define MTW_CAMPAIGN_SETUP_METADATA_COMPLETE 3u

static const unsigned char mtw_prebattle_entry_call_expected[
    MTW_PREBATTLE_ENTRY_CALL_SIZE] = {
        0xE8, 0x42, 0x67, 0x00, 0x00
};
static const unsigned char mtw_prebattle_resolution_return_expected[
    MTW_PREBATTLE_RESOLUTION_RETURN_SIZE] = {
        0x83, 0xC4, 0x10, 0x40, 0x89, 0x84,
        0x24, 0x84, 0x00, 0x00, 0x00
};
static int mtw_game_mode_is_frontend(uint32_t game_mode) {
    return game_mode == MTW_GAME_MODE_FRONTEND_ENTRY ||
           game_mode == MTW_GAME_MODE_FRONTEND_IDLE;
}

mtw_mapper_activation_state mtw_mapper_activation_initial(void) {
    mtw_mapper_activation_state result = {1, 0};
    return result;
}

mtw_mapper_activation_state mtw_mapper_activation_after_surface(
    mtw_mapper_activation_state current,
    int render_input_root_present,
    uint16_t width,
    uint16_t height,
    uint32_t pitch,
    uint8_t bytes_per_unit) {
    /*
     * The renderer/input root and 800x600 descriptor are shared by the menu,
     * campaign overlays, and pre-battle screen.  Surface identity and
     * dimensions therefore cannot prove a frontend transition.
     */
    (void)render_input_root_present;
    (void)width;
    (void)height;
    (void)pitch;
    (void)bytes_per_unit;
    return current;
}

mtw_mapper_activation_state mtw_mapper_activation_after_loading(
    mtw_mapper_activation_state current,
    uint32_t width,
    uint32_t height,
    uint32_t pitch) {
    uint32_t row_bytes;

    if (width == 0u || height == 0u ||
        width > MTW_LOADING_MAX_DIMENSION ||
        height > MTW_LOADING_MAX_DIMENSION ||
        width > UINT32_MAX / MTW_LOADING_BYTES_PER_PIXEL) {
        return current;
    }
    row_bytes = width * MTW_LOADING_BYTES_PER_PIXEL;
    if (pitch >= row_bytes && pitch != 0u &&
        height <= MTW_LOADING_MAX_BYTES / pitch) {
        current.mapper_mode = 0;
        current.frontend_rearm_armed = 0;
    }
    return current;
}

mtw_mapper_activation_state mtw_mapper_activation_after_game_mode(
    mtw_mapper_activation_state current,
    uint32_t game_mode,
    mtw_mapper_frontend_owner_kind frontend_owner) {
    if (frontend_owner == MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP) {
        /*
         * Campaign setup shares mode 1 with campaign gameplay.  Its verified
         * page graph may preserve an active mapper, but it must never rearm
         * one that a verified loading-plane transition already disarmed.
         */
        if (current.mapper_mode) {
            current.frontend_rearm_armed = 0;
        }
        return current;
    }

    /*
     * These owners all use the legacy 800x600 frontend presentation path.
     * Ownership is stronger evidence than the shared top-level mode or surface
     * geometry, so it also permits an immediate rearm after a loading-plane
     * disarm.
     */
    if (frontend_owner == MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU ||
        frontend_owner == MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU ||
        frontend_owner == MTW_MAPPER_FRONTEND_OWNER_PREBATTLE ||
        frontend_owner == MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS) {
        current.mapper_mode = 1;
        current.frontend_rearm_armed = 0;
        return current;
    }

    if (!mtw_game_mode_is_frontend(game_mode)) {
        current.mapper_mode = 0;
        current.frontend_rearm_armed = 1;
        return current;
    }

    return current;
}

uint32_t mtw_mapper_campaign_setup_metadata_anchor(
    uintptr_t game_base,
    uintptr_t page_metadata) {
    if (game_base == 0u || page_metadata == 0u) return 0u;
    if (page_metadata ==
        game_base + MTW_CAMPAIGN_SETUP_HEADER_METADATA_RVA) {
        return 1u;
    }
    if (page_metadata ==
        game_base + MTW_CAMPAIGN_SETUP_DIFFICULTY_METADATA_RVA) {
        return 2u;
    }
    return 0u;
}

int mtw_mapper_campaign_setup_metadata_complete(uint32_t anchor_mask) {
    return (anchor_mask & MTW_CAMPAIGN_SETUP_METADATA_COMPLETE) ==
           MTW_CAMPAIGN_SETUP_METADATA_COMPLETE;
}

mtw_mapper_activation_state mtw_mapper_activation_after_prebattle_resolution(
    mtw_mapper_activation_state current,
    uint32_t game_mode,
    int controller_present) {
    return mtw_mapper_activation_after_game_mode(
        current, game_mode,
        controller_present ?
            MTW_MAPPER_FRONTEND_OWNER_PREBATTLE :
            MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
}

mtw_mapper_frontend_owner_kind mtw_mapper_frontend_owner_observe_metadata(
    mtw_mapper_frontend_owner_kind current,
    uintptr_t game_base,
    uintptr_t page_metadata) {
    if (game_base == 0u || page_metadata == 0u) {
        return current;
    }
    if (page_metadata == game_base + MTW_IN_GAME_MENU_METADATA_RVA) {
        return MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU;
    }
    /*
     * These two immutable page descriptors use the Battle Results row and
     * update callbacks at game RVAs 0x2BB7F0 and 0x2BB850.  Both were present
     * in the live Quick Battle result graph, while the same top-level mode had
     * no frontend graph during the 3D battle.
     */
    if (page_metadata == game_base + MTW_BATTLE_RESULTS_METADATA_RVA ||
        page_metadata ==
            game_base + MTW_BATTLE_RESULTS_UPDATE_METADATA_RVA) {
        return MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS;
    }
    if (current == MTW_MAPPER_FRONTEND_OWNER_UNKNOWN &&
        page_metadata == game_base + MTW_MAIN_MENU_INITIAL_METADATA_RVA) {
        return MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU;
    }
    return current;
}

int mtw_prebattle_entry_callsite_supported(
    const unsigned char *bytes,
    uint32_t size) {
    return bytes != NULL &&
           size == MTW_PREBATTLE_ENTRY_CALL_SIZE &&
           memcmp(bytes, mtw_prebattle_entry_call_expected,
                  MTW_PREBATTLE_ENTRY_CALL_SIZE) == 0;
}

int mtw_prebattle_resolution_return_supported(
    const unsigned char *bytes,
    uint32_t size) {
    return bytes != NULL &&
           size == MTW_PREBATTLE_RESOLUTION_RETURN_SIZE &&
           memcmp(bytes, mtw_prebattle_resolution_return_expected,
                  MTW_PREBATTLE_RESOLUTION_RETURN_SIZE) == 0;
}
