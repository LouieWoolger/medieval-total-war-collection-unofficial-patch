#ifndef MTW_MAPPER_ACTIVATION_CORE_H
#define MTW_MAPPER_ACTIVATION_CORE_H

#include <stdint.h>

#define MTW_PREBATTLE_ENTRY_CALL_SIZE 5u
#define MTW_PREBATTLE_RESOLUTION_RETURN_SIZE 11u

typedef struct mtw_mapper_activation_state {
    int mapper_mode;
    int frontend_rearm_armed;
} mtw_mapper_activation_state;

typedef enum mtw_mapper_frontend_owner_kind {
    MTW_MAPPER_FRONTEND_OWNER_UNKNOWN = 0,
    MTW_MAPPER_FRONTEND_OWNER_MAIN_MENU = 1,
    MTW_MAPPER_FRONTEND_OWNER_IN_GAME_MENU = 2,
    MTW_MAPPER_FRONTEND_OWNER_PREBATTLE = 3,
    MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS = 4,
    MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP = 5,
    MTW_MAPPER_FRONTEND_OWNER_COMMON_PAGE = 6
} mtw_mapper_frontend_owner_kind;

typedef enum mtw_mapper_frontend_family_kind {
    MTW_MAPPER_FRONTEND_FAMILY_NONE = 0,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_ASSIGN_KEYS = 1,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_AUDIO_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_CONTROL_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_SELECT_ERA,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_DIALUP_ADAPTERS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_GAME_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_GRAPHIC_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_IN_GAME_MENU,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_INITIAL,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_LOAD_GAME,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_MAP_EDITOR,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_MAP_EDITOR_NEWMAP_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_MULTIPLAYER,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_NEW_GAME,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_REGISTRATION,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_REPLAY,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_SAVE_GAME,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_TUTORIAL,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_VERIFY_PLAYER,
    MTW_MAPPER_FRONTEND_FAMILY_MAIN_MENU_VIDEO_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_INTERNET_GAMES,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_LOBBY,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_MAP_INFO,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_OPTIONS,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_ROLE_OF_HONOUR,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_SELECT_CHATROOM,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_WAITING_TO_GO,
    MTW_MAPPER_FRONTEND_FAMILY_CUSTOM_BATTLE_ARMY_SELECT,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_ARMY_SELECT,
    MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_ARMY_SELECT,
    MTW_MAPPER_FRONTEND_FAMILY_CUSTOM_BATTLE_BATTLE_SELECT,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_CREATE_GAME,
    MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_HOST_GAME,
    MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_MULTIPLAYER,
    MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_POST_CUSTOM_BATTLE,
    MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_POST_HISTORICAL_BATTLE,
    MTW_MAPPER_FRONTEND_FAMILY_BATTLE_RESULTS_POST_HISTORICAL_CAMPAIGN,
    MTW_MAPPER_FRONTEND_FAMILY_DEFAULT_CHATROOM_BATTLE_RESULTS,
    MTW_MAPPER_FRONTEND_FAMILY_CUSTOM_BATTLE_FACTION_SELECT,
    MTW_MAPPER_FRONTEND_FAMILY_FACTION_SELECT_CS,
    MTW_MAPPER_FRONTEND_FAMILY_FACTION_SELECT_FS,
    MTW_MAPPER_FRONTEND_FAMILY_HISTORICAL_BATTLE_LIST,
    MTW_MAPPER_FRONTEND_FAMILY_HISTORICAL_BATTLE_FRONT_MENU,
    MTW_MAPPER_FRONTEND_FAMILY_HISTORICAL_CAMPAIGN_INTERMEDIATE_MENU,
    MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_LOBBY,
    MTW_MAPPER_FRONTEND_FAMILY_LAN_MULTIPLAYER_INITIAL,
    MTW_MAPPER_FRONTEND_FAMILY_NEWS_CLIENT_MAIN_MENU,
    MTW_MAPPER_FRONTEND_FAMILY_TUTORIALS_POST_TUTORIAL,
    MTW_MAPPER_FRONTEND_FAMILY_WAITING_TO_GO
} mtw_mapper_frontend_family_kind;

typedef struct mtw_mapper_frontend_metadata_identity {
    mtw_mapper_frontend_family_kind family;
    uint32_t row_index;
} mtw_mapper_frontend_metadata_identity;

mtw_mapper_activation_state mtw_mapper_activation_initial(void);

mtw_mapper_activation_state mtw_mapper_activation_after_surface(
    mtw_mapper_activation_state current,
    int render_input_root_present,
    uint16_t width,
    uint16_t height,
    uint32_t pitch,
    uint8_t bytes_per_unit);

mtw_mapper_activation_state mtw_mapper_activation_after_loading(
    mtw_mapper_activation_state current,
    uint32_t width,
    uint32_t height,
    uint32_t pitch);

mtw_mapper_activation_state mtw_mapper_activation_after_game_mode(
    mtw_mapper_activation_state current,
    uint32_t game_mode,
    mtw_mapper_frontend_owner_kind frontend_owner);

mtw_mapper_activation_state mtw_mapper_activation_after_prebattle_resolution(
    mtw_mapper_activation_state current,
    uint32_t game_mode,
    int controller_present);

mtw_mapper_frontend_owner_kind mtw_mapper_frontend_owner_observe_metadata(
    mtw_mapper_frontend_owner_kind current,
    uintptr_t game_base,
    uintptr_t page_metadata);

int mtw_mapper_frontend_identify_metadata(
    uintptr_t game_base,
    uintptr_t page_metadata,
    mtw_mapper_frontend_metadata_identity *identity);

uint32_t mtw_mapper_frontend_family_count(void);

uint32_t mtw_mapper_frontend_registered_row_count(void);

uint32_t mtw_mapper_campaign_setup_metadata_anchor(
    uintptr_t game_base,
    uintptr_t page_metadata);

int mtw_mapper_campaign_setup_metadata_complete(uint32_t anchor_mask);

int mtw_prebattle_entry_callsite_supported(
    const unsigned char *bytes,
    uint32_t size);

int mtw_prebattle_resolution_return_supported(
    const unsigned char *bytes,
    uint32_t size);

#endif
