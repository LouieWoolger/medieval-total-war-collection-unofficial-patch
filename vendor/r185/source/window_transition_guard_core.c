#include "window_transition_guard_core.h"

static int rect_valid(const mtw_window_rect *rect) {
    return rect != 0 && rect->right > rect->left &&
           rect->bottom > rect->top;
}

static int rect_covers(const mtw_window_rect *outer,
                       const mtw_window_rect *inner) {
    return rect_valid(outer) && rect_valid(inner) &&
           outer->left <= inner->left && outer->top <= inner->top &&
           outer->right >= inner->right &&
           outer->bottom >= inner->bottom;
}

static int request_is_monitor_shrink(
    const mtw_window_transition_request *request) {
    int32_t monitor_width;
    int32_t monitor_height;

    if (request == 0 || request->target_window == 0u ||
        request->game_window == 0u ||
        request->target_window != request->game_window ||
        request->requested_width <= 0 || request->requested_height <= 0 ||
        !rect_covers(&request->current_window, &request->monitor)) {
        return 0;
    }
    monitor_width = request->monitor.right - request->monitor.left;
    monitor_height = request->monitor.bottom - request->monitor.top;
    if (request->requested_width > monitor_width ||
        request->requested_height > monitor_height) {
        return 0;
    }
    return request->requested_width < monitor_width ||
           request->requested_height < monitor_height;
}

int mtw_window_transition_should_suppress(
    const mtw_window_transition_request *request) {
    if (request == 0 ||
        request->expected_return_address == 0u ||
        request->return_address != request->expected_return_address ||
        request->repaint != 0 || request->requested_x != 0 ||
        request->requested_y != 0) {
        return 0;
    }

    return request_is_monitor_shrink(request);
}

int mtw_wrapper_window_transition_should_suppress(
    const mtw_window_transition_request *request,
    uint32_t set_window_pos_flags) {
    const uint32_t swp_nosize = 0x0001u;
    const uint32_t swp_nomove = 0x0002u;

    if (request == 0 || (set_window_pos_flags & swp_nosize) != 0u) {
        return 0;
    }
    if ((set_window_pos_flags & swp_nomove) == 0u &&
        (request->requested_x != request->current_window.left ||
         request->requested_y != request->current_window.top)) {
        return 0;
    }
    return request_is_monitor_shrink(request);
}
