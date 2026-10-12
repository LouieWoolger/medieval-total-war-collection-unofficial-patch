#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../vendor/r185/source/campaign_fast_moves_core.h"

int main(void) {
    mtw_fast_move_slot slots[4] = {{0}};
    mtw_fast_move_result r;
    uint32_t duration = mtw_fast_move_duration(0, 0, 2500, 0);
    assert(duration == 1500u);
    assert(mtw_fast_move_duration(0, 0, 30000, 0) == 2200u);
    r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 0, 1000, 1, 1, 1);
    assert(!r.override); /* checked by default means native timing */
    r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 0, 1000, 1, 1, 0);
    assert(r.override && r.x == 0 && r.counter == 0);
    r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 0, 1600, 1, 1, 1);
    assert(r.override && r.x == 416 && r.counter == 0); /* latches existing move */
    r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 0, 2100, 0, 1, 1);
    assert(r.override && r.x == 416); /* focus loss does not spend time */
    r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 0, 2250, 1, 1, 1);
    assert(r.override && r.x == 666); /* resumed 150 ms */
    for (uint64_t t = 2500; t <= 3500; t += 250)
        r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 0, t, 1, 1, 1);
    assert(r.override && r.counter == 26 && r.x == 2500);
    r = mtw_fast_move_step(slots, 4, 1, 2, 0, 0, 2500, 0, 26, 3750, 1, 1, 0);
    assert(!r.override); /* game finalizes exactly once on next update */
    r = mtw_fast_move_step(slots, 4, 3, 4, 0, 0, 100, 100, 0, 4000, 1, 0, 0);
    assert(!r.override); /* hidden moves never gain a delay */
    r = mtw_fast_move_step(slots, 4, 3, 4, 0, 0, 100, 100, 0, 4000, 1, 1, 0);
    assert(r.override && r.counter == 0);
    r = mtw_fast_move_step(slots, 4, 3, 4, 0, 0, 100, 100, 0, 4250, 1, 1, 0);
    assert(r.override && r.x == 24 && r.y == 24 && r.counter == 0);
    r = mtw_fast_move_step(slots, 4, 3, 4, 0, 0, 100, 100, 0, 4500, 1, 0, 0);
    assert(r.override && r.x == 100 && r.y == 100 && r.counter == 26);
    /* Disabling Show during slow mode must finish at the destination. The
     * next native update finalizes once without replaying early path frames. */
    r = mtw_fast_move_step(slots, 4, 3, 4, 0, 0, 100, 100, 26, 4516, 1, 0, 0);
    assert(!r.override);
    memset(slots, 0, sizeof(slots));
    r = mtw_fast_move_step(slots, 4, 7, 8, 10, 10, -90, 110, 0, 5000, 1, 1, 0);
    assert(r.override && r.x == 10 && r.y == 10);
    r = mtw_fast_move_step(slots, 4, 7, 8, 10, 10, -90, 110, 0, 6100, 1, 1, 0);
    assert(r.override && r.x == -14 && r.y == 34); /* long stall is bounded */
    return 0;
}
