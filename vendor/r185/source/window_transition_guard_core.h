#ifndef MTW_WINDOW_TRANSITION_GUARD_CORE_H
#define MTW_WINDOW_TRANSITION_GUARD_CORE_H

#include <stdint.h>

typedef struct mtw_window_rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} mtw_window_rect;

typedef struct mtw_window_transition_request {
    uintptr_t target_window;
    uintptr_t game_window;
    uintptr_t return_address;
    uintptr_t expected_return_address;
    int32_t requested_x;
    int32_t requested_y;
    int32_t requested_width;
    int32_t requested_height;
    int repaint;
    mtw_window_rect current_window;
    mtw_window_rect monitor;
} mtw_window_transition_request;

int mtw_window_transition_should_suppress(
    const mtw_window_transition_request *request);
int mtw_wrapper_window_transition_should_suppress(
    const mtw_window_transition_request *request,
    uint32_t set_window_pos_flags);

#endif
