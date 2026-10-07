#ifndef MTW_CAMPAIGN_PAN_CORE_H
#define MTW_CAMPAIGN_PAN_CORE_H

#include <stdint.h>

typedef struct mtw_campaign_pan_state {
    uint64_t last_ticks[8];
    unsigned char seen[8];
} mtw_campaign_pan_state;

/* Returns 0 only when a repeated call has no measurable elapsed time. */
int mtw_campaign_pan_step(mtw_campaign_pan_state *state,
                          unsigned direction,
                          uint64_t now_ticks,
                          uint64_t ticks_per_second,
                          double *distance);

#endif
