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

static int request_is_known_borderless_popup(
    const mtw_window_transition_request *request) {
    const uintptr_t ws_popup = (uintptr_t)0x80000000u;
    const uintptr_t ws_caption = (uintptr_t)0x00C00000u;

    return request != 0 && request->window_style_valid != 0 &&
           (request->window_style & ws_popup) != 0u &&
           (request->window_style & ws_caption) == 0u;
}

static int request_is_known_decorated(
    const mtw_window_transition_request *request) {
    const uintptr_t ws_caption = (uintptr_t)0x00C00000u;

    return request != 0 && request->window_style_valid != 0 &&
           (request->window_style & ws_caption) != 0u;
}

static int request_has_live_stable_decorated_presentation(
    const mtw_window_transition_request *request) {
    int32_t current_width;
    int32_t current_height;
    int32_t stable_width;
    int32_t stable_height;

    if (request == 0 || request->target_window == 0u ||
        request->game_window == 0u ||
        request->target_window != request->game_window ||
        request->requested_width <= 0 || request->requested_height <= 0 ||
        request->stable_presentation_valid == 0 ||
        !request_is_known_decorated(request) ||
        !rect_valid(&request->current_window) ||
        !rect_valid(&request->stable_presentation)) {
        return 0;
    }
    current_width =
        request->current_window.right - request->current_window.left;
    current_height =
        request->current_window.bottom - request->current_window.top;
    stable_width = request->stable_presentation.right -
        request->stable_presentation.left;
    stable_height = request->stable_presentation.bottom -
        request->stable_presentation.top;
    return current_width == stable_width && current_height == stable_height;
}

static int request_is_decorated_monitor_expansion(
    const mtw_window_transition_request *request) {
    int32_t monitor_width;
    int32_t monitor_height;

    if (request == 0 || request->target_window == 0u ||
        request->game_window == 0u ||
        request->target_window != request->game_window ||
        request->requested_width <= 0 || request->requested_height <= 0 ||
        !request_is_known_decorated(request) ||
        !rect_valid(&request->current_window) ||
        !rect_valid(&request->monitor) ||
        rect_covers(&request->current_window, &request->monitor)) {
        return 0;
    }
    monitor_width = request->monitor.right - request->monitor.left;
    monitor_height = request->monitor.bottom - request->monitor.top;
    return request->requested_width >= monitor_width &&
           request->requested_height >= monitor_height;
}

static int request_is_monitor_shrink(
    const mtw_window_transition_request *request) {
    int32_t monitor_width;
    int32_t monitor_height;

    if (request == 0 || request->target_window == 0u ||
        request->game_window == 0u ||
        request->target_window != request->game_window ||
        request->requested_width <= 0 || request->requested_height <= 0 ||
        !request_is_known_borderless_popup(request) ||
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

    return request_is_monitor_shrink(request) ||
           request_is_decorated_monitor_expansion(request) ||
           request_has_live_stable_decorated_presentation(request);
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
    if (request_is_monitor_shrink(request)) {
        return 1;
    }
    if (request_has_live_stable_decorated_presentation(request)) {
        int32_t stable_width = request->stable_presentation.right -
            request->stable_presentation.left;
        int32_t stable_height = request->stable_presentation.bottom -
            request->stable_presentation.top;

        return request->requested_width < stable_width ||
               request->requested_height < stable_height;
    }
    return 0;
}
