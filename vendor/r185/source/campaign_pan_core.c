#include "campaign_pan_core.h"

#include <string.h>

#define MTW_PAN_REFERENCE_HZ 60.0
#define MTW_PAN_REFERENCE_STEP 100.0

static unsigned opposite_direction(unsigned direction) {
    switch (direction) {
    case 0u: return 1u;
    case 1u: return 0u;
    case 6u: return 7u;
    case 7u: return 6u;
    default: return 8u;
    }
}

int mtw_campaign_pan_step(mtw_campaign_pan_state *state,
                          unsigned direction,
                          uint64_t now_ticks,
                          uint64_t ticks_per_second,
                          double *distance) {
    uint64_t elapsed;
    unsigned opposite;

    if (state == NULL || distance == NULL || ticks_per_second == 0u ||
        opposite_direction(direction) == 8u) {
        return 0;
    }
    opposite = opposite_direction(direction);
    if (!state->seen[direction] ||
        (state->seen[opposite] &&
         state->last_ticks[opposite] > state->last_ticks[direction]) ||
        now_ticks < state->last_ticks[direction]) {
        /* First active frame, a reversal, or a QPC discontinuity. */
        state->last_ticks[direction] = now_ticks;
        state->seen[direction] = 1u;
        *distance = MTW_PAN_REFERENCE_STEP;
        return 1;
    }

    if (now_ticks == state->last_ticks[direction]) return 0;

    elapsed = now_ticks - state->last_ticks[direction];
    state->last_ticks[direction] = now_ticks;
    if ((double)elapsed / (double)ticks_per_second > 0.100) {
        /* A paused or stalled campaign must not teleport on resume. */
        *distance = MTW_PAN_REFERENCE_STEP;
    } else {
        *distance = MTW_PAN_REFERENCE_STEP * MTW_PAN_REFERENCE_HZ *
                    ((double)elapsed / (double)ticks_per_second);
    }
    return 1;
}
