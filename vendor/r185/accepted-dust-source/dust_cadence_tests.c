#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "dust_cadence.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static int test_30_60_120_and_uncapped_converge(void) {
    const uint32_t deltas[] = {33u, 16u, 8u, 5u};
    const unsigned frames[] = {303u, 625u, 1250u, 2000u};
    unsigned mode;

    for (mode = 0; mode < 4u; ++mode) {
        MtwDustCadence cadence = mtw_dust_cadence_initial(1000.0f / 30.0f);
        unsigned updates = 0u;
        unsigned frame;
        for (frame = 0; frame < frames[mode]; ++frame) {
            updates += (unsigned)mtw_dust_cadence_advance(&cadence, deltas[mode]);
        }
        CHECK(updates >= 298u && updates <= 301u);
    }
    return 1;
}

static int test_one_decision_is_shared_by_every_producer_in_a_frame(void) {
    MtwDustCadence cadence = mtw_dust_cadence_initial(1000.0f / 30.0f);
    CHECK(mtw_dust_cadence_begin_frame(&cadence, 10u, 17u) == 0);
    CHECK(mtw_dust_cadence_begin_frame(&cadence, 10u, 17u) == 0);
    CHECK(mtw_dust_cadence_begin_frame(&cadence, 11u, 17u) == 1);
    CHECK(mtw_dust_cadence_begin_frame(&cadence, 11u, 17u) == 1);
    return 1;
}

static int test_long_stall_runs_once_without_catchup_burst(void) {
    MtwDustCadence cadence = mtw_dust_cadence_initial(1000.0f / 30.0f);
    CHECK(mtw_dust_cadence_advance(&cadence, 200u) == 1);
    CHECK(cadence.debt_ms < cadence.step_ms);
    CHECK(mtw_dust_cadence_advance(&cadence, 1u) == 0);
    return 1;
}

static int test_invalid_step_fails_open(void) {
    MtwDustCadence cadence = mtw_dust_cadence_initial(0.0f);
    CHECK(mtw_dust_cadence_begin_frame(&cadence, 1u, 1u) == 1);
    CHECK(mtw_dust_cadence_begin_frame(&cadence, 2u, 1u) == 1);
    return 1;
}

static int test_release_density_hysteresis(void) {
    int dense = 0;
    dense = mtw_dust_density_update(dense, 12999u, 13000u, 11000u);
    CHECK(dense == 0);
    dense = mtw_dust_density_update(dense, 13000u, 13000u, 11000u);
    CHECK(dense == 1);
    dense = mtw_dust_density_update(dense, 12000u, 13000u, 11000u);
    CHECK(dense == 1);
    dense = mtw_dust_density_update(dense, 11000u, 13000u, 11000u);
    CHECK(dense == 0);
    return 1;
}

static int test_release_dense_only_policy(void) {
    CHECK(fabsf(mtw_dust_step_for_density(0, 28.0f, 1000.0f / 19.0f) -
                28.0f) < 0.0001f);
    CHECK(fabsf(mtw_dust_step_for_density(1, 28.0f, 1000.0f / 19.0f) -
                (1000.0f / 19.0f)) < 0.0001f);
    CHECK(mtw_dust_virtual_fps_for_density(0, 19u) == 0u);
    CHECK(mtw_dust_virtual_fps_for_density(1, 19u) == 19u);
    return 1;
}

int main(void) {
    if (!test_30_60_120_and_uncapped_converge() ||
        !test_one_decision_is_shared_by_every_producer_in_a_frame() ||
        !test_long_stall_runs_once_without_catchup_burst() ||
        !test_invalid_step_fails_open() ||
        !test_release_density_hysteresis() ||
        !test_release_dense_only_policy()) {
        return 1;
    }
    puts("dust cadence tests passed");
    return 0;
}
