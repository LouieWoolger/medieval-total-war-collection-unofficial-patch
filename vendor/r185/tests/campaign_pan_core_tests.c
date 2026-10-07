#include "../source/campaign_pan_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *name) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL %s\n", name);
    }
}

static double travel_for_rate(unsigned frames_per_second) {
    mtw_campaign_pan_state state;
    double distance;
    double total = 0.0;
    unsigned frame;

    memset(&state, 0, sizeof(state));
    for (frame = 0; frame <= frames_per_second; ++frame) {
        uint64_t tick = (uint64_t)frame * 1000000u /
                        frames_per_second;
        if (mtw_campaign_pan_step(&state, 1u, tick, 1000000u,
                                  &distance)) {
            total += distance;
        }
    }
    return total;
}

static double travel_for_two_paths(unsigned frames_per_second) {
    mtw_campaign_pan_state state[2];
    double distance;
    double total = 0.0;
    unsigned frame;
    unsigned path;

    memset(state, 0, sizeof(state));
    for (frame = 0; frame <= frames_per_second; ++frame) {
        uint64_t tick = (uint64_t)frame * 1000000u /
                        frames_per_second;
        for (path = 0; path != 2u; ++path) {
            if (mtw_campaign_pan_step(&state[path], 1u, tick,
                                      1000000u, &distance)) {
                total += distance;
            }
        }
    }
    return total;
}

int main(void) {
    mtw_campaign_pan_state state;
    double distance = -1.0;
    double sixty = travel_for_rate(60u);
    double one_forty_four = travel_for_rate(144u);
    double three_fifty_nine = travel_for_rate(359u);

    check(fabs(sixty - 6100.0) < 0.01, "reference travel");
    check(fabs(sixty - one_forty_four) < 0.01,
          "144 fps matches elapsed travel");
    check(fabs(sixty - three_fifty_nine) < 0.01,
          "359 fps matches elapsed travel");
    check(fabs(travel_for_two_paths(60u) - 12200.0) < 0.01,
          "two active input paths retain both 60 Hz contributions");
    check(fabs(travel_for_two_paths(60u) -
               travel_for_two_paths(359u)) < 0.01,
          "two input paths remain frame-rate independent");

    memset(&state, 0, sizeof(state));
    check(mtw_campaign_pan_step(&state, 1u, 10u, 1000u, &distance) &&
          distance == 100.0, "first frame remains responsive");
    check(mtw_campaign_pan_step(&state, 1u, 20u, 1000u, &distance) &&
          distance == 60.0, "elapsed frame distance");
    check(mtw_campaign_pan_step(&state, 0u, 21u, 1000u, &distance) &&
          distance == 100.0, "direction reversal resets elapsed time");
    check(mtw_campaign_pan_step(&state, 1u, 22u, 1000u, &distance) &&
          distance == 100.0, "reversal back has no accumulated jump");
    check(mtw_campaign_pan_step(&state, 1u, 500u, 1000u, &distance) &&
          distance == 100.0, "long stall has no catch-up teleport");
    check(!mtw_campaign_pan_step(&state, 2u, 501u, 1000u, &distance),
          "unverified direction rejected");
    check(!mtw_campaign_pan_step(&state, 1u, 501u, 0u, &distance),
          "invalid timebase rejected");
    if (failures != 0) return 1;
    puts("campaign pan core tests passed");
    return 0;
}
