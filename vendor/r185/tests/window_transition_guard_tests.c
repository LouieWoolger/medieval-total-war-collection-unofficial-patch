#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/window_transition_guard_core.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static mtw_window_transition_request fullscreen_request(void) {
    mtw_window_transition_request request;

    memset(&request, 0, sizeof(request));
    request.target_window = (uintptr_t)0x11110000u;
    request.game_window = (uintptr_t)0x11110000u;
    request.return_address = (uintptr_t)0x006D631Du;
    request.expected_return_address = (uintptr_t)0x006D631Du;
    request.requested_x = 0;
    request.requested_y = 0;
    request.requested_width = 640;
    request.requested_height = 480;
    request.repaint = 0;
    request.current_window.left = 0;
    request.current_window.top = 0;
    request.current_window.right = 2560;
    request.current_window.bottom = 1440;
    request.monitor.left = 0;
    request.monitor.top = 0;
    request.monitor.right = 2560;
    request.monitor.bottom = 1440;
    return request;
}

static int test_exact_quick_battle_shrink_is_suppressed(void) {
    mtw_window_transition_request request = fullscreen_request();

    CHECK(mtw_window_transition_should_suppress(&request));
    request.requested_width = 800;
    request.requested_height = 600;
    CHECK(mtw_window_transition_should_suppress(&request));
    return 0;
}

static int test_wrapper_set_window_pos_shrinks_are_suppressed(void) {
    mtw_window_transition_request request = fullscreen_request();

    request.return_address = 0u;
    request.expected_return_address = 0u;
    CHECK(mtw_wrapper_window_transition_should_suppress(&request, 0x214u));

    request.requested_width = 800;
    request.requested_height = 600;
    CHECK(mtw_wrapper_window_transition_should_suppress(&request, 0x216u));

    request.requested_width = 2560;
    request.requested_height = 1440;
    CHECK(!mtw_wrapper_window_transition_should_suppress(&request, 0x006u));

    request.requested_width = 640;
    request.requested_height = 480;
    CHECK(!mtw_wrapper_window_transition_should_suppress(&request, 0x215u));

    request.target_window = (uintptr_t)0x22220000u;
    CHECK(!mtw_wrapper_window_transition_should_suppress(&request, 0x214u));
    return 0;
}

static int test_windowed_and_unrelated_calls_forward(void) {
    mtw_window_transition_request request = fullscreen_request();

    request.current_window.right = 1280;
    request.current_window.bottom = 720;
    CHECK(!mtw_window_transition_should_suppress(&request));

    request = fullscreen_request();
    request.target_window = (uintptr_t)0x22220000u;
    CHECK(!mtw_window_transition_should_suppress(&request));

    request = fullscreen_request();
    request.return_address++;
    CHECK(!mtw_window_transition_should_suppress(&request));

    request = fullscreen_request();
    request.repaint = 1;
    CHECK(!mtw_window_transition_should_suppress(&request));
    return 0;
}

static int test_same_or_larger_geometry_forwards(void) {
    mtw_window_transition_request request = fullscreen_request();

    request.requested_width = 2560;
    request.requested_height = 1440;
    CHECK(!mtw_window_transition_should_suppress(&request));

    request.requested_width = 3840;
    request.requested_height = 2160;
    CHECK(!mtw_window_transition_should_suppress(&request));

    request.requested_width = 0;
    request.requested_height = 480;
    CHECK(!mtw_window_transition_should_suppress(&request));
    request.requested_width = 640;
    request.requested_height = -1;
    CHECK(!mtw_window_transition_should_suppress(&request));
    return 0;
}

static int test_secondary_and_negative_monitor_coordinates(void) {
    mtw_window_transition_request request = fullscreen_request();

    request.monitor.left = -1920;
    request.monitor.top = 0;
    request.monitor.right = 0;
    request.monitor.bottom = 1080;
    request.current_window = request.monitor;
    CHECK(mtw_window_transition_should_suppress(&request));

    request.monitor.left = 2560;
    request.monitor.top = -200;
    request.monitor.right = 4480;
    request.monitor.bottom = 880;
    request.current_window = request.monitor;
    CHECK(mtw_window_transition_should_suppress(&request));

    request.current_window.left++;
    CHECK(!mtw_window_transition_should_suppress(&request));
    return 0;
}

static int test_confirmed_event_sequences_never_expose_small_window(void) {
    static const uint32_t route_dimensions[][2] = {
        {640u, 480u}, /* Quick Battle. */
        {800u, 600u}, /* New Campaign. */
        {800u, 600u}, /* Load Campaign. */
        {1024u, 768u} /* Representative different logical mode. */
    };
    size_t route;
    size_t iteration;

    for (route = 0u;
         route < sizeof(route_dimensions) / sizeof(route_dimensions[0]);
         ++route) {
        for (iteration = 0u; iteration < 32u; ++iteration) {
            mtw_window_transition_request request = fullscreen_request();

            request.requested_width = (int32_t)route_dimensions[route][0];
            request.requested_height = (int32_t)route_dimensions[route][1];
            CHECK(mtw_window_transition_should_suppress(&request));
        }
    }
    return 0;
}

static int test_invalid_or_incomplete_state_fails_open(void) {
    mtw_window_transition_request request = fullscreen_request();

    CHECK(!mtw_window_transition_should_suppress(NULL));
    request.game_window = 0u;
    CHECK(!mtw_window_transition_should_suppress(&request));
    request = fullscreen_request();
    request.expected_return_address = 0u;
    CHECK(!mtw_window_transition_should_suppress(&request));
    request = fullscreen_request();
    request.monitor.right = request.monitor.left;
    CHECK(!mtw_window_transition_should_suppress(&request));
    request = fullscreen_request();
    request.current_window.bottom = request.current_window.top;
    CHECK(!mtw_window_transition_should_suppress(&request));
    return 0;
}

int main(void) {
    CHECK(test_exact_quick_battle_shrink_is_suppressed() == 0);
    CHECK(test_wrapper_set_window_pos_shrinks_are_suppressed() == 0);
    CHECK(test_windowed_and_unrelated_calls_forward() == 0);
    CHECK(test_same_or_larger_geometry_forwards() == 0);
    CHECK(test_secondary_and_negative_monitor_coordinates() == 0);
    CHECK(test_confirmed_event_sequences_never_expose_small_window() == 0);
    CHECK(test_invalid_or_incomplete_state_fails_open() == 0);
    puts("window transition guard tests passed");
    return 0;
}
