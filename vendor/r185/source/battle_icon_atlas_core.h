#ifndef MTW_BATTLE_ICON_ATLAS_CORE_H
#define MTW_BATTLE_ICON_ATLAS_CORE_H

#include <stdint.h>

/* Observed stock PE32 state around its graphics-init call. The scene pointer
 * is the live battle HUD object that receives update code 2 each frame. */
typedef struct mtw_battle_icon_atlas_state {
    uint32_t mode;
    uint32_t render_root;
    uint32_t scene;
    uint32_t cache_first;
    uint32_t surface;
    uint8_t dirty;
} mtw_battle_icon_atlas_state;

static inline int mtw_battle_icon_atlas_should_mark_dirty(
    const mtw_battle_icon_atlas_state *before,
    const mtw_battle_icon_atlas_state *after,
    int init_succeeded) {
    return before != 0 && after != 0 && init_succeeded &&
           before->scene != 0u && before->scene == after->scene &&
           before->cache_first != UINT32_MAX &&
           after->cache_first == UINT32_MAX && after->surface != 0u &&
           after->render_root == 0u &&
           after->mode != UINT32_MAX &&
           /* Battle tutorials keep top-level mode zero, like menus. The old
            * surface is already released at this callsite during focus reset;
            * the stable live HUD scene and nonempty icon cache identify the
            * battle state before the cache is cleared. */
           (after->mode != 0u ||
            (before->mode == 0u && before->render_root == 0u)) &&
           after->dirty == 0u;
}

#endif
