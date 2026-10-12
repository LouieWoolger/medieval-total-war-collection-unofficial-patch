#ifndef MTW_INTRO_MORPH_GUARD_CORE_H
#define MTW_INTRO_MORPH_GUARD_CORE_H

/* The credits images are composed by the game before its final 800x600
 * menu mode change. A lost movie surface during either morph discards part
 * of that composition. Keep the original call pending until the real game
 * window is restored; the original morph then performs its normal recovery. */
typedef struct mtw_intro_morph_state {
    int surface_lost;
    int surface_present;
    int owner_present;
    int window_valid;
    int window_foreground;
    int window_iconic;
    int pump_available;
    int shutdown;
} mtw_intro_morph_state;

static inline int mtw_intro_morph_should_defer(
    const mtw_intro_morph_state *state) {
    return state && state->surface_lost && !state->surface_present &&
           state->owner_present && state->window_valid &&
           !state->window_foreground && state->pump_available &&
           !state->shutdown;
}

static inline int mtw_intro_morph_should_wait(
    const mtw_intro_morph_state *state) {
    return state && state->window_valid && !state->shutdown &&
           (!state->window_foreground || state->window_iconic);
}

#endif
