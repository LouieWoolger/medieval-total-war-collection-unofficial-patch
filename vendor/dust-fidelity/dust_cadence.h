#ifndef MTW_DUST_CADENCE_H
#define MTW_DUST_CADENCE_H

#include <stdint.h>

typedef struct MtwDustCadence {
    float step_ms;
    float debt_ms;
    uint32_t last_frame_id;
    int frame_decision;
    int has_frame;
} MtwDustCadence;

static __inline MtwDustCadence mtw_dust_cadence_initial(float step_ms) {
    MtwDustCadence cadence;
    cadence.step_ms = step_ms;
    cadence.debt_ms = 0.0f;
    cadence.last_frame_id = 0u;
    cadence.frame_decision = 1;
    cadence.has_frame = 0;
    return cadence;
}

/*
 * Permit at most one movement-dust update for an elapsed interval.  Excess
 * whole-step debt is discarded instead of causing a catch-up burst: the
 * original game also performs only one producer update after a long frame.
 */
static __inline int mtw_dust_cadence_advance(MtwDustCadence *cadence,
                                             uint32_t elapsed_ms) {
    if (!cadence || cadence->step_ms <= 0.0f) {
        return 1;
    }
    cadence->debt_ms += (float)elapsed_ms;
    if (cadence->debt_ms < cadence->step_ms) {
        return 0;
    }
    do {
        cadence->debt_ms -= cadence->step_ms;
    } while (cadence->debt_ms >= cadence->step_ms);
    return 1;
}

/* Every type-0/type-1 producer traversed in one manager frame shares a gate. */
static __inline int mtw_dust_cadence_begin_frame(MtwDustCadence *cadence,
                                                 uint32_t frame_id,
                                                 uint32_t elapsed_ms) {
    if (!cadence || cadence->step_ms <= 0.0f) {
        return 1;
    }
    if (cadence->has_frame && cadence->last_frame_id == frame_id) {
        return cadence->frame_decision;
    }
    cadence->has_frame = 1;
    cadence->last_frame_id = frame_id;
    cadence->frame_decision = mtw_dust_cadence_advance(cadence, elapsed_ms);
    return cadence->frame_decision;
}

static __inline int mtw_dust_density_update(int dense,
                                            uint32_t manager_cost,
                                            uint32_t enter_cost,
                                            uint32_t exit_cost) {
    if (!dense && manager_cost >= enter_cost) {
        return 1;
    }
    if (dense && manager_cost <= exit_cost) {
        return 0;
    }
    return dense ? 1 : 0;
}

static __inline float mtw_dust_step_for_density(int dense,
                                                float sparse_step_ms,
                                                float dense_step_ms) {
    return dense ? dense_step_ms : sparse_step_ms;
}

static __inline uint32_t mtw_dust_virtual_fps_for_density(
    int dense, uint32_t dense_fps) {
    return dense ? dense_fps : 0u;
}

#endif
