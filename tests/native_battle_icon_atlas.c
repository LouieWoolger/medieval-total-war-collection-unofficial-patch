#include <stdint.h>
#include <stdio.h>

#include "battle_icon_atlas_core.h"

static unsigned passed;
static unsigned failed;

static void check(const char *name, int actual, int expected) {
    if (actual == expected) {
        ++passed;
    } else {
        ++failed;
        fprintf(stderr, "FAIL %s got=%d expected=%d\n", name, actual, expected);
    }
}

int main(void) {
    mtw_battle_icon_atlas_state before = {
        12u, 0u, 0x120000u, 10u, 0x140000u, 0u
    };
    mtw_battle_icon_atlas_state after = {
        12u, 0u, 0x120000u, UINT32_MAX, 0x150000u, 0u
    };
    mtw_battle_icon_atlas_state changed;

    check("active focus reset", mtw_battle_icon_atlas_should_mark_dirty(&before, &after, 1), 1);
    changed = before;
    changed.mode = 1u;
    after.mode = 1u;
    check("campaign battle mode", mtw_battle_icon_atlas_should_mark_dirty(&changed, &after, 1), 1);
    after.mode = 12u;
    check("failed init", mtw_battle_icon_atlas_should_mark_dirty(&before, &after, 0), 0);
    changed = before;
    changed.scene = 0u;
    check("no battle before", mtw_battle_icon_atlas_should_mark_dirty(&changed, &after, 1), 0);
    changed = after;
    changed.scene = 0u;
    check("no battle after", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    changed = after;
    changed.scene = 0x160000u;
    check("scene changed", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    changed = before;
    changed.cache_first = UINT32_MAX;
    check("empty old cache", mtw_battle_icon_atlas_should_mark_dirty(&changed, &after, 1), 0);
    changed = after;
    changed.cache_first = 10u;
    check("new cache already populated", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    changed = after;
    changed.surface = 0u;
    check("no new atlas", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    changed = after;
    changed.render_root = 0x170000u;
    check("frontend active", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    changed = after;
    changed.mode = 0u;
    check("menu setup", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    before.mode = 0u;
    check("battle tutorial with live atlas", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 1);
    before.surface = 0u;
    check("tutorial reset after old surface released", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 1);
    before.surface = 0x140000u;
    before.render_root = 0x170000u;
    check("zero mode with frontend root", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    before.render_root = 0u;
    changed.mode = UINT32_MAX;
    check("menu idle", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);
    changed = after;
    changed.dirty = 1u;
    check("already scheduled", mtw_battle_icon_atlas_should_mark_dirty(&before, &changed, 1), 0);

    printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
