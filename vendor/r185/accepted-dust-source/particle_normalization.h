#ifndef MTW_PARTICLE_NORMALIZATION_H
#define MTW_PARTICLE_NORMALIZATION_H

#include <stdint.h>

/*
 * Convert one frame-dependent game update into the fraction of a native
 * update represented by this loop's elapsed wall time.  The game has already
 * computed `after`; the hook only rescales that one observed delta.
 */
static __inline float mtw_normalize_frame_delta(float before,
                                                float after,
                                                uint32_t elapsed_ms,
                                                float native_step_ms) {
    float scale;

    if (native_step_ms <= 0.0f) {
        return after;
    }
    scale = (float)elapsed_ms / native_step_ms;
    return before + ((after - before) * scale);
}

#endif
