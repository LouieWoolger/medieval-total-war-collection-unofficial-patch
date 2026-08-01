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
#define MTW_FRONTEND_DESCRIPTOR_ROW_BYTES 0x134u

typedef struct mtw_mapper_frontend_family_span {
    uint32_t start_rva;
    uint16_t row_count;
    uint16_t family;
} mtw_mapper_frontend_family_span;

/*
 * Supported Medieval_TW.exe common-frontend registry.  Each descriptor row
 * is immutable, image-relative, and 0x134 bytes.  The table comes from the
 * executable's 50 registration calls into the common page-graph loader.
 */
static const mtw_mapper_frontend_family_span mtw_frontend_families[] = {
    {0x003EF220u,   7u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_ASSIGN_KEYS},
    {0x003F1228u,  17u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_AUDIO_OPTIONS},
    {0x003F2FC0u,  13u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_CONTROL_OPTIONS},
    {0x003F51D8u,  16u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_SELECT_ERA},
    {0x003FB2C0u,  29u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_DIALUP_ADAPTERS},
    {0x003FF720u,  22u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_GAME_OPTIONS},
    {0x004025E0u,  17u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_GRAPHIC_OPTIONS},
    {0x00403C10u,   9u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_IN_GAME_MENU},
    {0x00404B30u,  11u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_INITIAL},
    {0x00406310u,  15u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_LOAD_GAME},
    {0x00409680u,  20u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_MAP_EDITOR},
    {0x0040BA50u,  14u,
     MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_MAP_EDITOR_NEWMAP_OPTIONS},
    {0x0040DCB0u,  21u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_MULTIPLAYER},
    {0x0040FFF8u,  13u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_NEW_GAME},
    {0x00411430u,   8u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_OPTIONS},
    {0x004141D0u,  19u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_REGISTRATION},
    {0x004162C0u,  14u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_REPLAY},
    {0x00418218u,  19u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_SAVE_GAME},
    {0x00419CA0u,   6u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_TUTORIAL},
    {0x0041A3F0u,  16u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_VERIFY_PLAYER},
    {0x0041D330u,  17u, MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_VIDEO_OPTIONS},
    {0x00420448u,  27u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_INTERNET_GAMES},
    {0x00422560u,  66u, MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_LOBBY},
    {0x00427DC8u,  16u, MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_MAP_INFO},
    {0x0042A328u,  21u, MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_OPTIONS},
    {0x0042CA30u,   9u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_ROLE_OF_HONOUR},
    {0x0042E640u,  31u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_SELECT_CHATROOM},
    {0x00430BB0u,  27u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_WAITING_TO_GO},
    {0x00433548u, 150u, MTW_MAPPER_FRONTEND_FAMILY_CUSTOM_BATTLE_ARMY_SELECT},
    {0x0043EB68u, 131u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_ARMY_SELECT},
    {0x00448920u, 127u, MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_ARMY_SELECT},
    {0x00452C38u,  32u,
     MTW_MAPPER_FRONTEND_FAMILY_CUSTOM_BATTLE_BATTLE_SELECT},
    {0x00455970u,  60u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_CREATE_GAME},
    {0x0045A1C0u,  55u, MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_HOST_GAME},
    {0x0046B728u,  14u, MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_MULTIPLAYER},
    {0x0046C800u,  11u,
     MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_POST_CUSTOM_BATTLE},
    {0x0046D540u,   5u,
     MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_POST_HISTORICAL_BATTLE},
    {0x0046DB48u,   5u,
     MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_POST_HISTORICAL_CAMPAIGN},
    {0x0046E150u,  20u,
     MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_BATTLE_RESULTS},
    {0x0047A2B0u, 104u,
     MTW_MAPPER_FRONTEND_FAMILY_CUSTOM_BATTLE_FACTION_SELECT},
    {0x00491128u,  17u, MTW_MAPPER_FRONTEND_FAMILY_FACTION_SELECT_CS},
    {0x004925A0u,  14u, MTW_MAPPER_FRONTEND_FAMILY_FACTION_SELECT_FS},
    {0x0049C420u,  56u, MTW_MAPPER_FRONTEND_FAMILY_HISTORICAL_BATTLE_LIST},
    {0x004A3410u,  20u,
     MTW_MAPPER_FRONTEND_FAMILY_HISTORICAL_BATTLE_FRONT_MENU},
    {0x004A4C20u,  43u,
     MTW_MAPPER_FRONTEND_FAMILY_HISTORICAL_CAMPAIGN_INTERMEDIATE_MENU},
    {0x004B3268u,  59u, MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_LOBBY},
    {0x004B7968u,  15u, MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_INITIAL},
    {0x004C40F8u,   8u, MTW_MAPPER_FRONTEND_FAMILY_NEWS_CLIENT_MAIN_MENU},
    {0x004C8C18u,   8u, MTW_MAPPER_FRONTEND_FAMILY_TUTORIALS_POST_TUTORIAL},
    {0x004C9608u,  26u, MTW_MAPPER_FRONTEND_FAMILY_WAITING_TO_GO}
};

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

int mtw_mapper_frontend_identify_metadata(
    uintptr_t game_base,
    uintptr_t page_metadata,
    mtw_mapper_frontend_metadata_identity *identity) {
    uintptr_t metadata_rva;
    size_t index;

    if (identity == NULL) return 0;
    identity->family = MTW_MAPPER_FRONTEND_FAMILY_NONE;
    identity->row_index = 0u;
    if (game_base == 0u || page_metadata < game_base) return 0;

    metadata_rva = page_metadata - game_base;
    if (metadata_rva > UINT32_MAX) return 0;
    for (index = 0u;
         index < sizeof(mtw_frontend_families) /
                     sizeof(mtw_frontend_families[0]);
         ++index) {
        const mtw_mapper_frontend_family_span *span =
            &mtw_frontend_families[index];
        uint32_t relative;
        uint32_t span_bytes =
            (uint32_t)span->row_count * MTW_FRONTEND_DESCRIPTOR_ROW_BYTES;

        if ((uint32_t)metadata_rva < span->start_rva ||
            (uint32_t)metadata_rva >= span->start_rva + span_bytes) {
            continue;
        }
        relative = (uint32_t)metadata_rva - span->start_rva;
        if (relative % MTW_FRONTEND_DESCRIPTOR_ROW_BYTES != 0u) {
            return 0;
        }
        identity->family =
            (mtw_mapper_frontend_family_kind)span->family;
        identity->row_index =
            relative / MTW_FRONTEND_DESCRIPTOR_ROW_BYTES;
        return 1;
    }
    return 0;
}

uint32_t mtw_mapper_frontend_family_count(void) {
    return (uint32_t)(sizeof(mtw_frontend_families) /
                      sizeof(mtw_frontend_families[0]));
}

uint32_t mtw_mapper_frontend_registered_row_count(void) {
    uint32_t total = 0u;
    size_t index;

    for (index = 0u;
         index < sizeof(mtw_frontend_families) /
                     sizeof(mtw_frontend_families[0]);
         ++index) {
        total += mtw_frontend_families[index].row_count;
    }
    return total;
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
        frontend_owner == MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS ||
        frontend_owner == MTW_MAPPER_FRONTEND_OWNER_COMMON_PAGE) {
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
    mtw_mapper_frontend_metadata_identity identity;

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
    if (current == MTW_MAPPER_FRONTEND_OWNER_UNKNOWN &&
        mtw_mapper_frontend_identify_metadata(
            game_base, page_metadata, &identity)) {
        return MTW_MAPPER_FRONTEND_OWNER_COMMON_PAGE;
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
