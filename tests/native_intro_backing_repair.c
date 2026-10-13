#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../vendor/r185/source/intro_backing_repair_core.h"

static int checks;
static int failures;

static void check(int ok, const char *name) {
    ++checks;
    if (!ok) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

int main(void) {
    mtw_intro_backing_tracker tracker = {0};
    mtw_intro_backing_snapshot surface = {0};
    mtw_intro_backing_plan plan = {0};
    uintptr_t old_pointer = 0;
    unsigned char changed[5];

    check(MTW_INTRO_ALLOCATOR_RVA == 0x0036D20Du &&
          MTW_INTRO_DEALLOCATOR_RVA == 0x00361AF7u,
          "game allocator ownership RVAs");
    check(MTW_INTRO_ALLOC_FIRST_RVA == 0x00012048u &&
          MTW_INTRO_ALLOC_SECOND_RVA == 0x00012149u &&
          MTW_INTRO_RETURN_FIRST_RVA == 0x00012076u &&
          MTW_INTRO_RETURN_SECOND_RVA == 0x0001219Cu,
          "both allocation and exit paths selected");
    check(mtw_intro_backing_site_matches(mtw_intro_return_expected,
                                         mtw_intro_return_expected, 5u),
          "known five-byte epilog accepted");
    memcpy(changed, mtw_intro_alloc_first_expected, sizeof changed);
    changed[1] ^= 1u;
    check(!mtw_intro_backing_site_matches(changed,
                                           mtw_intro_alloc_first_expected, 5u),
          "unsupported callsite bytes rejected");

    /* Captured real-game sequence: fallback allocated 640x480, then
     * WM_CLOSE switched dimensions to 800x600 without changing backing. */
    mtw_intro_backing_record_allocation(&tracker, 0x0E8D8020u, 614400u);
    surface.backing = surface.active = 0x0E8D8020u;
    surface.width = 640u;
    surface.height = 480u;
    surface.pitch = 1280u;
    check(mtw_intro_backing_make_plan(&tracker, &surface, &plan) &&
          plan.required_bytes == 614400u && plan.row_bytes == 1280u &&
          !plan.fix_pitch && !plan.grow,
          "ordinary first fallback stays unchanged");

    surface.width = 800u;
    surface.height = 600u;
    surface.pitch = 1600u;
    check(mtw_intro_backing_make_plan(&tracker, &surface, &plan) &&
          plan.required_bytes == 960000u && plan.row_bytes == 1600u &&
          !plan.fix_pitch && plan.grow,
          "captured 640-to-800 stale capacity requests replacement");
    check(!mtw_intro_backing_commit_growth(&tracker, &surface, 0u,
                                            &old_pointer) &&
          tracker.pointer == 0x0E8D8020u &&
          surface.active == 0x0E8D8020u && old_pointer == 0u,
          "allocator failure leaves game ownership unchanged");
    check(mtw_intro_backing_commit_growth(&tracker, &surface,
                                           0x0FBCF020u, &old_pointer) &&
          old_pointer == 0x0E8D8020u &&
          tracker.pointer == 0x0FBCF020u &&
          tracker.capacity_bytes == 960000u &&
          surface.backing == 0x0FBCF020u &&
          surface.active == 0x0FBCF020u && surface.pitch == 1600u,
          "replacement commits new ownership before old free");

    /* Forced Restore-failure path in the supported game emitted an entire
     * 640x480 allocation as pitch; both epilogs need the same correction. */
    mtw_intro_backing_record_allocation(&tracker, 0x0E755020u, 614400u);
    surface.backing = surface.active = 0x0E755020u;
    surface.width = 640u;
    surface.height = 480u;
    surface.pitch = 614400u;
    check(mtw_intro_backing_make_plan(&tracker, &surface, &plan) &&
          plan.fix_pitch && !plan.grow && plan.row_bytes == 1280u,
          "second fallback repairs whole-buffer pitch at first size");
    surface.pitch = plan.row_bytes;
    surface.width = 800u;
    surface.height = 600u;
    surface.pitch = 960000u;
    check(mtw_intro_backing_make_plan(&tracker, &surface, &plan) &&
          plan.fix_pitch && plan.grow && plan.row_bytes == 1600u,
          "second fallback at resize repairs pitch and capacity together");

    surface.active = 0xDEADBEEFu;
    memset(&plan, 0xA5, sizeof plan);
    check(!mtw_intro_backing_make_plan(&tracker, &surface, &plan) &&
          ((const unsigned char *)&plan)[0] == 0xA5u,
          "successful-Lock or alien active surface is untouched");
    surface.active = surface.backing;
    surface.pitch = 0x200u;
    check(!mtw_intro_backing_make_plan(&tracker, &surface, &plan),
          "unknown pitch form is untouched");
    surface.pitch = 1600u;
    surface.width = UINT32_MAX;
    check(!mtw_intro_backing_make_plan(&tracker, &surface, &plan),
          "overflowing dimensions are rejected");
    surface.width = 640u;
    surface.height = 480u;
    surface.pitch = 1280u;
    surface.backing ^= 1u;
    check(!mtw_intro_backing_make_plan(&tracker, &surface, &plan),
          "untracked backing is not freed or rewritten");
    mtw_intro_backing_reconcile(&tracker, surface.backing);
    check(tracker.pointer == 0u && tracker.capacity_bytes == 0u,
          "game-owned release clears stale tracking");

    /* LAA can place backing allocations above 0x80000000. These are opaque
     * unsigned pointer identities; no signed-address test may reject them. */
    mtw_intro_backing_record_allocation(&tracker, (uintptr_t)0xE18D8020u, 614400u);
    surface.backing = surface.active = (uintptr_t)0xE18D8020u;
    surface.width = 800u;
    surface.height = 600u;
    surface.pitch = 1600u;
    check(mtw_intro_backing_make_plan(&tracker, &surface, &plan) && plan.grow &&
          mtw_intro_backing_commit_growth(&tracker, &surface,
                                           (uintptr_t)0xF0BCF020u, &old_pointer) &&
          old_pointer == (uintptr_t)0xE18D8020u &&
          surface.backing == (uintptr_t)0xF0BCF020u,
          "high LAA backing addresses retain exact unsigned identity");

    printf("intro backing repair: %d/%d checks passed\n",
           checks - failures, checks);
    return failures ? 1 : 0;
}
