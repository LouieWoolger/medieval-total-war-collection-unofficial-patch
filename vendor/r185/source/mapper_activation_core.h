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
    MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP = 5
} mtw_mapper_frontend_owner_kind;

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
