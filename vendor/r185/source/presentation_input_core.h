#ifndef MTW_PRESENTATION_INPUT_CORE_H
#define MTW_PRESENTATION_INPUT_CORE_H

#include <stdint.h>

typedef struct mtw_presentation_input_transform {
    int32_t client_origin_x;
    int32_t client_origin_y;
    int32_t client_width;
    int32_t client_height;
    int32_t logical_width;
    int32_t logical_height;
} mtw_presentation_input_transform;

typedef struct mtw_presentation_input_rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} mtw_presentation_input_rect;

typedef struct mtw_presentation_input_cursor_state {
    int logical_valid;
    int32_t logical_x;
    int32_t logical_y;
} mtw_presentation_input_cursor_state;

int mtw_presentation_input_should_remap(
    uintptr_t game_window,
    uintptr_t stable_presentation_window,
    int stable_presentation_valid);

int mtw_presentation_input_destination_rect(
    const mtw_presentation_input_transform *transform,
    mtw_presentation_input_rect *destination);

int mtw_presentation_input_client_to_logical(
    const mtw_presentation_input_transform *transform,
    int32_t client_x,
    int32_t client_y,
    int32_t *logical_x,
    int32_t *logical_y);

int mtw_presentation_input_logical_to_client(
    const mtw_presentation_input_transform *transform,
    int32_t logical_x,
    int32_t logical_y,
    int32_t *client_x,
    int32_t *client_y);

int mtw_presentation_input_screen_to_logical_screen(
    const mtw_presentation_input_transform *transform,
    int32_t screen_x,
    int32_t screen_y,
    int32_t *logical_screen_x,
    int32_t *logical_screen_y);

int mtw_presentation_input_logical_screen_to_screen(
    const mtw_presentation_input_transform *transform,
    int32_t logical_screen_x,
    int32_t logical_screen_y,
    int32_t *screen_x,
    int32_t *screen_y);

void mtw_presentation_input_cursor_state_init(
    mtw_presentation_input_cursor_state *state);

int mtw_presentation_input_cursor_state_observe_screen(
    mtw_presentation_input_cursor_state *state,
    const mtw_presentation_input_transform *transform,
    int32_t screen_x,
    int32_t screen_y);

int mtw_presentation_input_alt_enter_restore_target(
    const mtw_presentation_input_cursor_state *state,
    const mtw_presentation_input_transform *transform,
    int32_t current_screen_x,
    int32_t current_screen_y,
    int input_state_active,
    int32_t *target_screen_x,
    int32_t *target_screen_y);

int mtw_presentation_input_is_alt_enter_keydown(
    uint32_t message,
    uintptr_t wparam,
    uintptr_t lparam);

#endif
