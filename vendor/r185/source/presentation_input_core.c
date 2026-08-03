#include "presentation_input_core.h"

#include <limits.h>

int mtw_presentation_input_should_remap(
    uintptr_t game_window,
    uintptr_t stable_presentation_window,
    int stable_presentation_valid) {
    return stable_presentation_valid != 0 && game_window != 0u &&
           stable_presentation_window == game_window;
}

static int transform_valid(
    const mtw_presentation_input_transform *transform) {
    return transform != 0 &&
           transform->client_width > 0 && transform->client_height > 0 &&
           transform->logical_width > 0 && transform->logical_height > 0;
}

static int32_t clamp_coordinate(int32_t coordinate, int32_t extent) {
    if (coordinate < 0) return 0;
    if (coordinate >= extent) return extent - 1;
    return coordinate;
}

static int32_t map_axis(int32_t coordinate,
                        int32_t source_start,
                        int32_t source_extent,
                        int32_t destination_extent) {
    int32_t offset;
    int64_t numerator;

    if (coordinate <= source_start || source_extent == 1 ||
        destination_extent == 1) {
        return 0;
    }
    if (coordinate >= source_start + source_extent - 1) {
        return destination_extent - 1;
    }
    offset = coordinate - source_start;
    numerator = (int64_t)offset * (destination_extent - 1) +
                (source_extent - 1) / 2;
    return (int32_t)(numerator / (source_extent - 1));
}

static int checked_add(int32_t left, int32_t right, int32_t *result) {
    int64_t sum;

    if (result == 0) return 0;
    sum = (int64_t)left + right;
    if (sum < INT32_MIN || sum > INT32_MAX) return 0;
    *result = (int32_t)sum;
    return 1;
}

static int checked_subtract(int32_t left, int32_t right, int32_t *result) {
    int64_t difference;

    if (result == 0) return 0;
    difference = (int64_t)left - right;
    if (difference < INT32_MIN || difference > INT32_MAX) return 0;
    *result = (int32_t)difference;
    return 1;
}

static int screen_point_inside_transform(
    const mtw_presentation_input_transform *transform,
    int32_t screen_x,
    int32_t screen_y) {
    int64_t relative_x;
    int64_t relative_y;

    if (!transform_valid(transform)) return 0;
    relative_x = (int64_t)screen_x - transform->client_origin_x;
    relative_y = (int64_t)screen_y - transform->client_origin_y;
    return relative_x >= 0 && relative_y >= 0 &&
           relative_x < transform->client_width &&
           relative_y < transform->client_height;
}

int mtw_presentation_input_destination_rect(
    const mtw_presentation_input_transform *transform,
    mtw_presentation_input_rect *destination) {
    mtw_presentation_input_rect candidate;
    int32_t destination_width;
    int32_t destination_height;
    int64_t client_aspect;
    int64_t logical_aspect;

    if (!transform_valid(transform) || destination == 0) return 0;
    client_aspect =
        (int64_t)transform->client_width * transform->logical_height;
    logical_aspect =
        (int64_t)transform->client_height * transform->logical_width;
    if (client_aspect > logical_aspect) {
        destination_height = transform->client_height;
        destination_width = (int32_t)(
            (int64_t)destination_height * transform->logical_width /
            transform->logical_height);
    } else {
        destination_width = transform->client_width;
        destination_height = (int32_t)(
            (int64_t)destination_width * transform->logical_height /
            transform->logical_width);
    }
    if (destination_width <= 0 || destination_height <= 0) return 0;
    candidate.left = (transform->client_width - destination_width) / 2;
    candidate.top = (transform->client_height - destination_height) / 2;
    candidate.right = candidate.left + destination_width;
    candidate.bottom = candidate.top + destination_height;
    *destination = candidate;
    return 1;
}

int mtw_presentation_input_client_to_logical(
    const mtw_presentation_input_transform *transform,
    int32_t client_x,
    int32_t client_y,
    int32_t *logical_x,
    int32_t *logical_y) {
    mtw_presentation_input_rect destination;
    int32_t mapped_x;
    int32_t mapped_y;

    if (logical_x == 0 || logical_y == 0 ||
        !mtw_presentation_input_destination_rect(
            transform, &destination)) {
        return 0;
    }
    mapped_x = map_axis(
        client_x, destination.left,
        destination.right - destination.left,
        transform->logical_width);
    mapped_y = map_axis(
        client_y, destination.top,
        destination.bottom - destination.top,
        transform->logical_height);
    *logical_x = mapped_x;
    *logical_y = mapped_y;
    return 1;
}

int mtw_presentation_input_logical_to_client(
    const mtw_presentation_input_transform *transform,
    int32_t logical_x,
    int32_t logical_y,
    int32_t *client_x,
    int32_t *client_y) {
    mtw_presentation_input_rect destination;
    int32_t mapped_x;
    int32_t mapped_y;
    int32_t clamped_x;
    int32_t clamped_y;

    if (client_x == 0 || client_y == 0 ||
        !mtw_presentation_input_destination_rect(
            transform, &destination)) {
        return 0;
    }
    clamped_x = clamp_coordinate(logical_x, transform->logical_width);
    clamped_y = clamp_coordinate(logical_y, transform->logical_height);
    mapped_x = destination.left + map_axis(
        clamped_x, 0, transform->logical_width,
        destination.right - destination.left);
    mapped_y = destination.top + map_axis(
        clamped_y, 0, transform->logical_height,
        destination.bottom - destination.top);
    *client_x = mapped_x;
    *client_y = mapped_y;
    return 1;
}

int mtw_presentation_input_screen_to_logical_screen(
    const mtw_presentation_input_transform *transform,
    int32_t screen_x,
    int32_t screen_y,
    int32_t *logical_screen_x,
    int32_t *logical_screen_y) {
    int32_t client_x;
    int32_t client_y;
    int32_t logical_x;
    int32_t logical_y;
    int32_t mapped_screen_x;
    int32_t mapped_screen_y;

    if (transform == 0 || logical_screen_x == 0 || logical_screen_y == 0 ||
        !checked_subtract(
            screen_x, transform->client_origin_x, &client_x) ||
        !checked_subtract(
            screen_y, transform->client_origin_y, &client_y) ||
        !mtw_presentation_input_client_to_logical(
            transform, client_x, client_y, &logical_x, &logical_y) ||
        !checked_add(
            transform->client_origin_x, logical_x, &mapped_screen_x) ||
        !checked_add(
            transform->client_origin_y, logical_y, &mapped_screen_y)) {
        return 0;
    }
    *logical_screen_x = mapped_screen_x;
    *logical_screen_y = mapped_screen_y;
    return 1;
}

int mtw_presentation_input_logical_screen_to_screen(
    const mtw_presentation_input_transform *transform,
    int32_t logical_screen_x,
    int32_t logical_screen_y,
    int32_t *screen_x,
    int32_t *screen_y) {
    int32_t logical_x;
    int32_t logical_y;
    int32_t client_x;
    int32_t client_y;
    int32_t mapped_screen_x;
    int32_t mapped_screen_y;

    if (transform == 0 || screen_x == 0 || screen_y == 0 ||
        !checked_subtract(
            logical_screen_x, transform->client_origin_x, &logical_x) ||
        !checked_subtract(
            logical_screen_y, transform->client_origin_y, &logical_y) ||
        !mtw_presentation_input_logical_to_client(
            transform, logical_x, logical_y, &client_x, &client_y) ||
        !checked_add(
            transform->client_origin_x, client_x, &mapped_screen_x) ||
        !checked_add(
            transform->client_origin_y, client_y, &mapped_screen_y)) {
        return 0;
    }
    *screen_x = mapped_screen_x;
    *screen_y = mapped_screen_y;
    return 1;
}

void mtw_presentation_input_cursor_state_init(
    mtw_presentation_input_cursor_state *state) {
    mtw_presentation_input_cursor_state empty = {0};

    if (state != 0) *state = empty;
}

int mtw_presentation_input_cursor_state_observe_screen(
    mtw_presentation_input_cursor_state *state,
    const mtw_presentation_input_transform *transform,
    int32_t screen_x,
    int32_t screen_y) {
    int32_t logical_screen_x;
    int32_t logical_screen_y;

    if (state == 0 || !screen_point_inside_transform(
            transform, screen_x, screen_y) ||
        !mtw_presentation_input_screen_to_logical_screen(
            transform, screen_x, screen_y,
            &logical_screen_x, &logical_screen_y)) {
        return 0;
    }
    state->logical_x = logical_screen_x - transform->client_origin_x;
    state->logical_y = logical_screen_y - transform->client_origin_y;
    state->logical_valid = 1;
    return 1;
}

int mtw_presentation_input_alt_enter_restore_target(
    const mtw_presentation_input_cursor_state *state,
    const mtw_presentation_input_transform *transform,
    int32_t current_screen_x,
    int32_t current_screen_y,
    int input_state_active,
    int32_t *target_screen_x,
    int32_t *target_screen_y) {
    int32_t logical_x;
    int32_t logical_y;
    int32_t logical_screen_x;
    int32_t logical_screen_y;

    if (!transform_valid(transform) || target_screen_x == 0 ||
        target_screen_y == 0 || input_state_active ||
        screen_point_inside_transform(
            transform, current_screen_x, current_screen_y)) {
        return 0;
    }
    if (state != 0 && state->logical_valid &&
        state->logical_x >= 0 &&
        state->logical_x < transform->logical_width &&
        state->logical_y >= 0 &&
        state->logical_y < transform->logical_height) {
        logical_x = state->logical_x;
        logical_y = state->logical_y;
    } else {
        logical_x = transform->logical_width / 2;
        logical_y = transform->logical_height / 2;
    }
    if (!checked_add(
            transform->client_origin_x, logical_x, &logical_screen_x) ||
        !checked_add(
            transform->client_origin_y, logical_y, &logical_screen_y) ||
        !mtw_presentation_input_logical_screen_to_screen(
            transform, logical_screen_x, logical_screen_y,
            target_screen_x, target_screen_y)) {
        return 0;
    }
    return 1;
}

int mtw_presentation_input_is_alt_enter_keydown(
    uint32_t message,
    uintptr_t wparam,
    uintptr_t lparam) {
    const uint32_t wm_syskeydown = 0x0104u;
    const uintptr_t vk_return = 0x0du;
    const uintptr_t alt_context_mask = (uintptr_t)1u << 29u;

    return message == wm_syskeydown && wparam == vk_return &&
           (lparam & alt_context_mask) != 0u;
}
