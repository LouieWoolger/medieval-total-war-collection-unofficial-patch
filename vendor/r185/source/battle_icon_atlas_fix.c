#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "battle_icon_atlas_core.h"
#include "battle_icon_atlas_fix.h"

#if !defined(_M_IX86)
#error The battle icon atlas hook is valid only for the verified PE32 game.
#endif

#define MTW_GAME_BASE 0x00400000u
#define MTW_BATTLE_ICON_ATLAS_INIT_CALL_RVA 0x00023E20u
#define MTW_BATTLE_ICON_ATLAS_INIT_TARGET_RVA 0x000DA2A0u
#define MTW_TOP_LEVEL_GAME_MODE_RVA 0x00A77610u
#define MTW_RENDER_INPUT_ROOT_RVA 0x00A6F5D0u
#define MTW_BATTLE_ICON_CACHE_FIRST_RVA 0x003B9354u
#define MTW_BATTLE_ICON_SURFACE_RECORD_RVA 0x00743678u
#define MTW_BATTLE_HUD_SCENE_RVA 0x0074275Cu
#define MTW_BATTLE_HUD_DIRTY_OFFSET 0x94Cu

static const unsigned char atlas_call_expected[5] = {
    0xE8, 0x7B, 0x64, 0x0B, 0x00
};
static const unsigned char atlas_target_expected[11] = {
    0xA1, 0x1C, 0xF5, 0xB8, 0x00, 0xC7,
    0x05, 0x54, 0x93, 0x7B, 0x00
};

typedef int (__cdecl *mtw_battle_icon_atlas_init_fn)(void);
static mtw_battle_icon_atlas_init_fn original_atlas_init;
static volatile LONG install_state;
static volatile LONG rebuild_events;

static int read_exact(uintptr_t address, void *destination, SIZE_T size) {
    SIZE_T transferred = 0u;
    return address != 0u && destination != NULL && size != 0u &&
           ReadProcessMemory(GetCurrentProcess(), (const void *)address,
                             destination, size, &transferred) != 0 &&
           transferred == size;
}

static int write_exact(uintptr_t address, const void *source, SIZE_T size) {
    SIZE_T transferred = 0u;
    return address != 0u && source != NULL && size != 0u &&
           WriteProcessMemory(GetCurrentProcess(), (void *)address,
                              source, size, &transferred) != 0 &&
           transferred == size;
}

static int bytes_match(uintptr_t address, const unsigned char *expected,
                       SIZE_T size) {
    unsigned char actual[16];
    return size <= sizeof(actual) && read_exact(address, actual, size) &&
           memcmp(actual, expected, size) == 0;
}

static int sample_state(mtw_battle_icon_atlas_state *state) {
    uintptr_t scene = 0u;
    uintptr_t surface_record = 0u;
    if (state == NULL) return 0;
    memset(state, 0, sizeof(*state));
    if (!read_exact(MTW_GAME_BASE + MTW_TOP_LEVEL_GAME_MODE_RVA,
                    &state->mode, sizeof(state->mode)) ||
        !read_exact(MTW_GAME_BASE + MTW_RENDER_INPUT_ROOT_RVA,
                    &state->render_root, sizeof(state->render_root)) ||
        !read_exact(MTW_GAME_BASE + MTW_BATTLE_HUD_SCENE_RVA,
                    &scene, sizeof(scene)) ||
        !read_exact(MTW_GAME_BASE + MTW_BATTLE_ICON_CACHE_FIRST_RVA,
                    &state->cache_first, sizeof(state->cache_first)) ||
        !read_exact(MTW_GAME_BASE + MTW_BATTLE_ICON_SURFACE_RECORD_RVA,
                    &surface_record, sizeof(surface_record))) return 0;
    state->scene = (uint32_t)scene;
    if (surface_record != 0u &&
        !read_exact(surface_record, &state->surface, sizeof(state->surface)))
        return 0;
    if (scene != 0u &&
        !read_exact(scene + MTW_BATTLE_HUD_DIRTY_OFFSET,
                    &state->dirty, sizeof(state->dirty))) return 0;
    return 1;
}

/* The reset clears atlas content. A live HUD schedules its ordinary icon
 * rebuild only after the original initializer succeeds on that same scene. */
static int __cdecl atlas_init_hook(void) {
    mtw_battle_icon_atlas_state before, after;
    int have_before, result;
    const uint8_t dirty = 1u;
    if (original_atlas_init == NULL) return 0;
    have_before = sample_state(&before);
    result = original_atlas_init();
    if (have_before && sample_state(&after) &&
        mtw_battle_icon_atlas_should_mark_dirty(&before, &after, result) &&
        write_exact((uintptr_t)after.scene + MTW_BATTLE_HUD_DIRTY_OFFSET,
                    &dirty, sizeof(dirty)) &&
        InterlockedIncrement(&rebuild_events) <= 8) {
        OutputDebugStringA("battle unit-icon atlas live rebuild queued\r\n");
    }
    return result;
}

static int install_hook(void) {
    uintptr_t game_base = (uintptr_t)GetModuleHandleW(NULL);
    uintptr_t callsite, original;
    intptr_t displacement;
    unsigned char patch[5];
    DWORD old_protect, ignored;

    if (game_base != MTW_GAME_BASE) return 0;
    callsite = game_base + MTW_BATTLE_ICON_ATLAS_INIT_CALL_RVA;
    original = game_base + MTW_BATTLE_ICON_ATLAS_INIT_TARGET_RVA;
    if (!bytes_match(original, atlas_target_expected,
                     sizeof(atlas_target_expected)) ||
        !bytes_match(callsite, atlas_call_expected,
                     sizeof(atlas_call_expected))) return 0;
    displacement = (const unsigned char *)atlas_init_hook -
                   ((const unsigned char *)callsite + sizeof(patch));
    if (displacement < INT32_MIN || displacement > INT32_MAX) return 0;
    patch[0] = 0xE8;
    *(int32_t *)&patch[1] = (int32_t)displacement;
    if (!VirtualProtect((void *)callsite, sizeof(patch),
                        PAGE_EXECUTE_READWRITE, &old_protect)) return 0;
    original_atlas_init = (mtw_battle_icon_atlas_init_fn)original;
    memcpy((void *)callsite, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), (void *)callsite, sizeof(patch));
    VirtualProtect((void *)callsite, sizeof(patch), old_protect, &ignored);
    if (bytes_match(callsite, patch, sizeof(patch))) return 1;
    original_atlas_init = NULL;
    return 0;
}

int mtw_battle_icon_atlas_install_once(void) {
    LONG state = InterlockedCompareExchange(&install_state, 1, 0);
    if (state == 0) {
        int result = install_hook();
        InterlockedExchange(&install_state, result ? 2 : 3);
        return result;
    }
    return state == 2;
}
