#ifndef MTW_CAMPAIGN_FAST_MOVES_CORE_H
#define MTW_CAMPAIGN_FAST_MOVES_CORE_H

#include <stdint.h>
#include <stddef.h>

/* The stock game interpolates a visible map action in 26 render updates.
 * Slow mode changes only that presentation interval, never its queued action.
 * A stopped or hidden game does not spend its remaining display time. */
typedef struct {
    uintptr_t sprite, action;
    int32_t start_x, start_y, end_x, end_y;
    uint64_t last_ms;
    uint32_t elapsed_ms, duration_ms;
    unsigned char active;
} mtw_fast_move_slot;

typedef struct {
    int32_t x, y;
    uint16_t counter;
    unsigned char override;
} mtw_fast_move_result;

static inline uint32_t mtw_fast_move_duration(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0;
    uint64_t distance = (uint64_t)(dx < 0 ? -dx : dx);
    uint64_t other = (uint64_t)(dy < 0 ? -dy : dy);
    uint64_t added;
    if (other > distance) distance = other;
    /* About one second for a short visible move; at most 2.2 seconds even
     * for very long paths. Multiple actions in one faction animate together. */
    added = distance / 5u;
    if (added > 1200u) added = 1200u;
    return 1000u + (uint32_t)added;
}

static inline mtw_fast_move_result mtw_fast_move_step(
    mtw_fast_move_slot *slots, size_t count, uintptr_t sprite, uintptr_t action,
    int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint16_t native_counter, uint64_t now_ms, int focused, int show, int fast) {
    mtw_fast_move_result result = {0};
    mtw_fast_move_slot *slot = NULL, *empty = NULL;
    size_t i;
    uint64_t delta;
    if (!slots || !count || !sprite || !action || native_counter > 25u) return result;
    for (i = 0; i < count; ++i) {
        mtw_fast_move_slot *candidate = slots + i;
        if (candidate->active && candidate->sprite == sprite) {
            if (candidate->action == action && candidate->start_x == x0 &&
                candidate->start_y == y0 && candidate->end_x == x1 &&
                candidate->end_y == y1) slot = candidate;
            else candidate->active = 0;
        }
        if (!candidate->active && !empty) empty = candidate;
    }
    if (!show) {
        if (slot) {
            /* Show was disabled during a slow action. Let the game's usual
             * finalization run from its destination on the next update;
             * returning to native frame one would jump the sprite backward. */
            result.x = slot->end_x;
            result.y = slot->end_y;
            result.counter = 26u;
            result.override = 1;
            slot->active = 0;
        }
        return result;
    }
    if (!slot) {
        /* A speed change takes effect on the next visible action. The first
         * update of an already-running native action stays on its native path. */
        if (fast || native_counter != 0u || !empty) return result;
        slot = empty;
        slot->sprite = sprite;
        slot->action = action;
        slot->start_x = x0; slot->start_y = y0;
        slot->end_x = x1; slot->end_y = y1;
        slot->last_ms = now_ms;
        slot->elapsed_ms = 0u;
        slot->duration_ms = mtw_fast_move_duration(x0, y0, x1, y1);
        slot->active = 1;
    }
    delta = now_ms >= slot->last_ms ? now_ms - slot->last_ms : 0u;
    slot->last_ms = now_ms;
    if (focused) {
        /* One long frame after a modal, Alt+Tab or load may advance at most
         * 250 ms. Ordinary 4 FPS and faster work at their natural wall rate. */
        if (delta > 250u) delta = 250u;
        if (delta > slot->duration_ms - slot->elapsed_ms)
            delta = slot->duration_ms - slot->elapsed_ms;
        slot->elapsed_ms += (uint32_t)delta;
    }
    result.x = (int32_t)((int64_t)x0 +
        ((int64_t)x1 - x0) * slot->elapsed_ms / slot->duration_ms);
    result.y = (int32_t)((int64_t)y0 +
        ((int64_t)y1 - y0) * slot->elapsed_ms / slot->duration_ms);
    result.counter = slot->elapsed_ms >= slot->duration_ms ? 26u : 0u;
    result.override = 1;
    if (result.counter == 26u) slot->active = 0;
    return result;
}

#endif
