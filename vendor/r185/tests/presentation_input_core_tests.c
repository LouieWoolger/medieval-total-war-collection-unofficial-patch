#include <stdint.h>
#include <stdio.h>

#include "../source/presentation_input_core.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static mtw_presentation_input_transform w1080_transform(void) {
    mtw_presentation_input_transform transform;

    transform.client_origin_x = 10;
    transform.client_origin_y = 40;
    transform.client_width = 1900;
    transform.client_height = 997;
    transform.logical_width = 800;
    transform.logical_height = 600;
    return transform;
}

static int test_aspect_fit_destination_uses_measured_dimensions(void) {
    mtw_presentation_input_transform transform = w1080_transform();
    mtw_presentation_input_rect destination;

    CHECK(mtw_presentation_input_destination_rect(
        &transform, &destination));
    CHECK(destination.left == 285);
    CHECK(destination.top == 0);
    CHECK(destination.right == 1614);
    CHECK(destination.bottom == 997);
    return 0;
}

static int test_large_window_client_points_map_to_frontend_coordinates(void) {
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = -1;
    int32_t y = -1;

    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 949, 498, &x, &y));
    CHECK(x == 400);
    CHECK(y == 300);

    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 399, 180, &x, &y));
    CHECK(x == 69);
    CHECK(y == 108);
    return 0;
}

static int test_letterbox_coordinates_clamp_to_logical_edges(void) {
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = -1;
    int32_t y = -1;

    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 0, -500, &x, &y));
    CHECK(x == 0);
    CHECK(y == 0);

    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 1899, 1499, &x, &y));
    CHECK(x == 799);
    CHECK(y == 599);
    return 0;
}

static int test_logical_points_inverse_map_into_visible_content(void) {
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = -1;
    int32_t y = -1;

    CHECK(mtw_presentation_input_logical_to_client(
        &transform, 0, 0, &x, &y));
    CHECK(x == 285);
    CHECK(y == 0);

    CHECK(mtw_presentation_input_logical_to_client(
        &transform, 799, 599, &x, &y));
    CHECK(x == 1613);
    CHECK(y == 996);

    CHECK(mtw_presentation_input_logical_to_client(
        &transform, 400, 300, &x, &y));
    CHECK(x == 950);
    CHECK(y == 499);
    return 0;
}

static int test_small_and_portrait_windows_use_the_same_invariant(void) {
    mtw_presentation_input_transform transform;
    mtw_presentation_input_rect destination;
    int32_t x = -1;
    int32_t y = -1;

    transform.client_origin_x = 0;
    transform.client_origin_y = 0;
    transform.client_width = 1280;
    transform.client_height = 720;
    transform.logical_width = 800;
    transform.logical_height = 600;
    CHECK(mtw_presentation_input_destination_rect(
        &transform, &destination));
    CHECK(destination.left == 160);
    CHECK(destination.top == 0);
    CHECK(destination.right == 1120);
    CHECK(destination.bottom == 720);
    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 640, 360, &x, &y));
    CHECK(x == 400);
    CHECK(y == 300);

    transform.client_width = 800;
    transform.client_height = 1000;
    CHECK(mtw_presentation_input_destination_rect(
        &transform, &destination));
    CHECK(destination.left == 0);
    CHECK(destination.top == 200);
    CHECK(destination.right == 800);
    CHECK(destination.bottom == 800);
    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 400, 500, &x, &y));
    CHECK(x == 400);
    CHECK(y == 300);
    return 0;
}

static int test_window_origin_is_preserved_for_screen_coordinate_apis(void) {
    mtw_presentation_input_transform transform;
    int32_t x = -1;
    int32_t y = -1;

    transform.client_origin_x = -1920;
    transform.client_origin_y = 40;
    transform.client_width = 1280;
    transform.client_height = 720;
    transform.logical_width = 800;
    transform.logical_height = 600;

    CHECK(mtw_presentation_input_screen_to_logical_screen(
        &transform, -1280, 400, &x, &y));
    CHECK(x == -1520);
    CHECK(y == 340);

    CHECK(mtw_presentation_input_logical_screen_to_screen(
        &transform, -1520, 340, &x, &y));
    CHECK(x == -1280);
    CHECK(y == 400);
    return 0;
}

static int test_identity_mapping_preserves_unscaled_input(void) {
    mtw_presentation_input_transform transform;
    int32_t x = -1;
    int32_t y = -1;

    transform.client_origin_x = 200;
    transform.client_origin_y = -100;
    transform.client_width = 800;
    transform.client_height = 600;
    transform.logical_width = 800;
    transform.logical_height = 600;
    CHECK(mtw_presentation_input_client_to_logical(
        &transform, 321, 456, &x, &y));
    CHECK(x == 321);
    CHECK(y == 456);
    CHECK(mtw_presentation_input_logical_to_client(
        &transform, 321, 456, &x, &y));
    CHECK(x == 321);
    CHECK(y == 456);
    return 0;
}

static int test_invalid_dimensions_fail_without_mutating_outputs(void) {
    mtw_presentation_input_transform transform = w1080_transform();
    mtw_presentation_input_rect destination = {1, 2, 3, 4};
    int32_t x = 123;
    int32_t y = 456;

    transform.client_width = 0;
    CHECK(!mtw_presentation_input_destination_rect(
        &transform, &destination));
    CHECK(destination.left == 1 && destination.top == 2 &&
          destination.right == 3 && destination.bottom == 4);
    CHECK(!mtw_presentation_input_client_to_logical(
        &transform, 10, 20, &x, &y));
    CHECK(x == 123);
    CHECK(y == 456);

    transform = w1080_transform();
    transform.logical_height = -1;
    CHECK(!mtw_presentation_input_logical_to_client(
        &transform, 10, 20, &x, &y));
    CHECK(x == 123);
    CHECK(y == 456);
    return 0;
}

static int test_alt_enter_detection_is_exact(void) {
    CHECK(mtw_presentation_input_is_alt_enter_keydown(
        0x0104u, 0x0du, 0x20000001u));
    CHECK(!mtw_presentation_input_is_alt_enter_keydown(
        0x0104u, 0x12u, 0x20000001u));
    CHECK(!mtw_presentation_input_is_alt_enter_keydown(
        0x0100u, 0x0du, 0x20000001u));
    CHECK(!mtw_presentation_input_is_alt_enter_keydown(
        0x0104u, 0x0du, 0x00000001u));
    CHECK(!mtw_presentation_input_is_alt_enter_keydown(
        0x0105u, 0x0du, 0x20000001u));
    return 0;
}

static int test_outside_cursor_restores_last_valid_logical_position(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = -1;
    int32_t y = -1;

    mtw_presentation_input_cursor_state_init(&state);
    CHECK(mtw_presentation_input_cursor_state_observe_screen(
        &state, &transform, 410, 220));
    CHECK(mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 0, 0, 0, &x, &y));
    CHECK(x == 410);
    CHECK(y == 220);
    return 0;
}

static int test_inside_cursor_and_active_input_are_not_moved(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = 123;
    int32_t y = 456;

    mtw_presentation_input_cursor_state_init(&state);
    CHECK(!mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 410, 220, 0, &x, &y));
    CHECK(x == 123 && y == 456);
    CHECK(!mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 0, 0, 1, &x, &y));
    CHECK(x == 123 && y == 456);
    return 0;
}

static int test_missing_history_uses_resolution_independent_content_center(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = -1;
    int32_t y = -1;

    mtw_presentation_input_cursor_state_init(&state);
    CHECK(mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 0, 0, 0, &x, &y));
    CHECK(x == 960);
    CHECK(y == 539);
    return 0;
}

static int test_outside_observation_does_not_replace_valid_history(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = -1;
    int32_t y = -1;

    mtw_presentation_input_cursor_state_init(&state);
    CHECK(mtw_presentation_input_cursor_state_observe_screen(
        &state, &transform, 410, 220));
    CHECK(!mtw_presentation_input_cursor_state_observe_screen(
        &state, &transform, 0, 0));
    CHECK(mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 0, 0, 0, &x, &y));
    CHECK(x == 410 && y == 220);
    return 0;
}

static int test_logical_history_maps_across_client_geometry_changes(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform windowed = w1080_transform();
    mtw_presentation_input_transform fullscreen = windowed;
    int32_t x = -1;
    int32_t y = -1;
    int32_t logical_screen_x = -1;
    int32_t logical_screen_y = -1;

    fullscreen.client_origin_x = 0;
    fullscreen.client_origin_y = 0;
    fullscreen.client_width = 1920;
    fullscreen.client_height = 1080;
    mtw_presentation_input_cursor_state_init(&state);
    CHECK(mtw_presentation_input_cursor_state_observe_screen(
        &state, &windowed, 410, 220));
    CHECK(mtw_presentation_input_alt_enter_restore_target(
        &state, &fullscreen, -1, -1, 0, &x, &y));
    CHECK(mtw_presentation_input_screen_to_logical_screen(
        &fullscreen, x, y, &logical_screen_x, &logical_screen_y));
    CHECK(logical_screen_x == 69);
    CHECK(logical_screen_y == 108);
    return 0;
}

static int test_negative_monitor_origin_is_preserved(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform transform;
    int32_t x = -1;
    int32_t y = -1;

    transform.client_origin_x = -1920;
    transform.client_origin_y = 40;
    transform.client_width = 1280;
    transform.client_height = 720;
    transform.logical_width = 800;
    transform.logical_height = 600;
    mtw_presentation_input_cursor_state_init(&state);
    CHECK(mtw_presentation_input_cursor_state_observe_screen(
        &state, &transform, -1280, 400));
    CHECK(mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 100, 100, 0, &x, &y));
    CHECK(x == -1280);
    CHECK(y == 400);
    return 0;
}

static int test_invalid_restore_inputs_leave_outputs_unchanged(void) {
    mtw_presentation_input_cursor_state state;
    mtw_presentation_input_transform transform = w1080_transform();
    int32_t x = 123;
    int32_t y = 456;

    mtw_presentation_input_cursor_state_init(&state);
    transform.client_width = 0;
    CHECK(!mtw_presentation_input_cursor_state_observe_screen(
        &state, &transform, 410, 220));
    CHECK(!mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 0, 0, 0, &x, &y));
    CHECK(x == 123 && y == 456);
    CHECK(!mtw_presentation_input_alt_enter_restore_target(
        &state, &transform, 0, 0, 0, NULL, &y));
    CHECK(x == 123 && y == 456);
    return 0;
}

static int test_fake_fullscreen_passes_wrapper_logical_input_through(void) {
    const uintptr_t game_window = (uintptr_t)0x1234u;

    CHECK(!mtw_presentation_input_should_remap(
        game_window, game_window, 0));
    return 0;
}

static int test_verified_decorated_presentation_remaps_input_once(void) {
    const uintptr_t game_window = (uintptr_t)0x1234u;

    CHECK(mtw_presentation_input_should_remap(
        game_window, game_window, 1));
    return 0;
}

static int test_stale_or_unknown_presentation_never_remaps_input(void) {
    const uintptr_t game_window = (uintptr_t)0x1234u;

    CHECK(!mtw_presentation_input_should_remap(
        game_window, (uintptr_t)0x5678u, 1));
    CHECK(!mtw_presentation_input_should_remap(
        game_window, (uintptr_t)0u, 1));
    CHECK(!mtw_presentation_input_should_remap(
        (uintptr_t)0u, game_window, 1));
    CHECK(!mtw_presentation_input_should_remap(
        game_window, game_window, 0));
    return 0;
}

int main(void) {
    CHECK(test_aspect_fit_destination_uses_measured_dimensions() == 0);
    CHECK(test_large_window_client_points_map_to_frontend_coordinates() == 0);
    CHECK(test_letterbox_coordinates_clamp_to_logical_edges() == 0);
    CHECK(test_logical_points_inverse_map_into_visible_content() == 0);
    CHECK(test_small_and_portrait_windows_use_the_same_invariant() == 0);
    CHECK(test_window_origin_is_preserved_for_screen_coordinate_apis() == 0);
    CHECK(test_identity_mapping_preserves_unscaled_input() == 0);
    CHECK(test_invalid_dimensions_fail_without_mutating_outputs() == 0);
    CHECK(test_alt_enter_detection_is_exact() == 0);
    CHECK(test_outside_cursor_restores_last_valid_logical_position() == 0);
    CHECK(test_inside_cursor_and_active_input_are_not_moved() == 0);
    CHECK(test_missing_history_uses_resolution_independent_content_center() == 0);
    CHECK(test_outside_observation_does_not_replace_valid_history() == 0);
    CHECK(test_logical_history_maps_across_client_geometry_changes() == 0);
    CHECK(test_negative_monitor_origin_is_preserved() == 0);
    CHECK(test_invalid_restore_inputs_leave_outputs_unchanged() == 0);
    CHECK(test_fake_fullscreen_passes_wrapper_logical_input_through() == 0);
    CHECK(test_verified_decorated_presentation_remaps_input_once() == 0);
    CHECK(test_stale_or_unknown_presentation_never_remaps_input() == 0);
    puts("presentation input core tests passed");
    return 0;
}
