#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define COBJMACROS
#include <d3d11.h>

#include <stdint.h>
#include <string.h>

#include "frontend_fix.h"
#include "frontend_epoch_core.h"
#include "focus_plane_shadow_core.h"
#include "focus_span_core.h"
#include "guarded_memory_copy.h"
#include "loading_shadow_core.h"
#include "mapper_activation_core.h"
#include "mapper_shader_clone_core.h"
#include "reentrant_lock_patch_core.h"

extern IMAGE_DOS_HEADER __ImageBase;

#if !defined(_M_IX86)
#error This loading fix is valid only for the verified PE32 game.
#endif

#define MTW_INITIAL_COPY_PRE_UNLOCK_RVA 0x0000BF60u
#define MTW_PROGRESS_POST_LOCK_RVA 0x0031CBC8u
#define MTW_PROGRESS_PRE_UNLOCK_RVA 0x0031CCEBu
#define MTW_BACKING_WIDTH_RVA 0x004D01B4u
#define MTW_BACKING_HEIGHT_RVA 0x004D01B8u
#define MTW_BACKING_PITCH_RVA 0x004D0244u
#define MTW_BACKING_BITS_RVA 0x004D0248u
#define MTW_PRIMARY_SURFACE_UNLOCK_RVA 0x000122D0u
#define MTW_DISPATCH_MESSAGE_IAT_RVA 0x0038428Cu
#define MTW_FRONTEND_FRAMEBUFFER_BITS_RVA 0x00A4587Cu
#define MTW_FRONTEND_FRAMEBUFFER_PITCH_RVA 0x00A45878u
#define MTW_RENDER_INPUT_ROOT_RVA 0x00A6F5D0u
#define MTW_TOP_LEVEL_GAME_MODE_RVA 0x00A77610u
#define MTW_FRONTEND_UI_STATE_OFFSET 0x10u
#define MTW_FRONTEND_PAGE_ARRAY_OFFSET 0x007A2508u
#define MTW_FRONTEND_PAGE_COUNT_OFFSET 0x007A2CB0u
#define MTW_FRONTEND_PAGE_METADATA_OFFSET 0x00009CA0u
#define MTW_FRONTEND_PAGE_CAPACITY 490u
#define MTW_FRONTEND_OWNER_POLL_INTERVAL_MS 16u
#define MTW_PREBATTLE_CONTROLLER_RVA 0x007E666Cu
#define MTW_PREBATTLE_ENTRY_CALL_RVA 0x0024BFA9u
#define MTW_PREBATTLE_ENTRY_TARGET_RVA 0x002526F0u
#define MTW_PREBATTLE_RESOLUTION_RETURN_RVA 0x0024BFAEu
#define MTW_TOP_LEVEL_MODE_CAMPAIGN 1u
#define MTW_TOP_LEVEL_MODE_QUICK_BATTLE 12u

#define DGVOODOO_FAST_SURFACE_CONSTRUCTOR_RVA 0x0002CC1Cu
#define DGVOODOO_REVERSE_DISPATCHER_RVA 0x0002D99Fu
#define DGVOODOO_UPLOAD_FALLBACK_RVA 0x00079CD4u
#define DGVOODOO_UPLOAD_FALLBACK_CALL_RVA 0x0007C330u
#define DGVOODOO_CONSTRUCTOR_GLOBAL_RVA 0x000DFA74u
#define DGVOODOO_OUTER_ACQUIRE_CALL_RVA 0x000948E2u
#define DGVOODOO_OUTER_RELEASE_CALL_RVA 0x00094C0Bu
#define DGVOODOO_INNER_ACQUIRE_CALL_RVA 0x0002AA07u
#define DGVOODOO_INNER_RELEASE_OK_CALL_RVA 0x0002AA39u
#define DGVOODOO_INNER_RELEASE_FAIL_CALL_RVA 0x0002AA49u
#define DGVOODOO_VEH_HANDLED_RVA 0x0002AA4Eu
#define DGVOODOO_CUSTOM_ACQUIRE_RVA 0x00003C76u
#define DGVOODOO_CUSTOM_RELEASE_RVA 0x00003CBFu
#define DGVOODOO_NESTED_ACQUIRE_RVA 0x000297EBu
#define DGVOODOO_NESTED_RELEASE_RVA 0x0002984Bu
#define DGVOODOO_GLOBAL_SLOT_RVA 0x000DFAA4u
#define DGVOODOO_MAPPER_DRAW_CALLSITE_RVA 0x000B47DAu
#define DGVOODOO_D3D11_CREATE_DEVICE_CALLSITE_RVA 0x000B666Bu
#define DGVOODOO_EXPECTED_TIMESTAMP 0x6A088020u
#define DGVOODOO_EXPECTED_IMAGE_SIZE 0x001A3000u
#define MTW_DEVICE_CREATE_PIXEL_SHADER_INDEX 15u

#define MTW_LOADING_BYTES_PER_PIXEL 2u
#define MTW_CONSTRUCTOR_THREAD_CAPACITY 32u
#define MTW_CONSTRUCTOR_RETURN_DEPTH 8u
#define MTW_FRONTEND_WIDTH 800u
#define MTW_FRONTEND_HEIGHT 600u
#define MTW_FRONTEND_ROW_BYTES (MTW_FRONTEND_WIDTH * 2u)
#define MTW_FRONTEND_PLANE_BYTES \
    (MTW_FRONTEND_ROW_BYTES * MTW_FRONTEND_HEIGHT)
#define MTW_R159_SAMPLE_INTERVAL 64u
#define MTW_R159_MAX_DISTINCT_VERSIONS 16u
#define MTW_R160_REVERSE_SLOT_CAPACITY 32u
#define MTW_R160_REVERSE_SAMPLE_INTERVAL 64u
#define MTW_R160_REVERSE_MAX_DISTINCT_VERSIONS 8u
#define R155_DIAGNOSTIC_ONLY 1

static const unsigned char capture_expected[] = {
    0xA1, 0x48, 0xCE, 0x8D, 0x00
};
static const unsigned char restore_expected[] = {
    0x8B, 0x15, 0x44, 0x02, 0x8D, 0x00
};
static const unsigned char commit_expected[] = {
    0xE8, 0xC0, 0x54, 0xCF, 0xFF
};
static const unsigned char fallback_expected[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34
};
static const unsigned char fallback_call_expected[] = {
    0xE8, 0x9F, 0xD9, 0xFF, 0xFF
};
static const unsigned char reverse_dispatcher_expected[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20
};
static const unsigned char primary_surface_unlock_expected[] = {
    0xA1, 0xD0, 0xF5, 0xE6, 0x00
};
static const unsigned char prebattle_entry_call_expected[
    MTW_PREBATTLE_ENTRY_CALL_SIZE] = {
        0xE8, 0x42, 0x67, 0x00, 0x00
};
static const unsigned char prebattle_resolution_return_expected[
    MTW_PREBATTLE_RESOLUTION_RETURN_SIZE] = {
        0x83, 0xC4, 0x10, 0x40, 0x89, 0x84,
        0x24, 0x84, 0x00, 0x00, 0x00
};
static const unsigned char outer_acquire_expected[] = {
    0xE8, 0x8F, 0xF3, 0xF6, 0xFF
};
static const unsigned char outer_release_expected[] = {
    0xE8, 0xAF, 0xF0, 0xF6, 0xFF
};
static const unsigned char inner_acquire_expected[] = {
    0xE8, 0xDF, 0xED, 0xFF, 0xFF
};
static const unsigned char mapper_draw_expected[MTW_MAPPER_CALLSITE_SIZE] = {
    0x8B, 0x08, 0x50, 0xFF, 0x51, 0x34
};
static const unsigned char d3d11_create_device_call_expected[6] = {
    0xFF, 0xD2, 0x85, 0xC0, 0x78, 0x48
};
static INIT_ONCE frontend_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE backend_tracking_once = INIT_ONCE_STATIC_INIT;
static uintptr_t game_base;
static uintptr_t backend_base;
static loading_shadow_state loading_shadow;
static CRITICAL_SECTION loading_shadow_lock;
static volatile LONG loading_shadow_operation_active;
static uintptr_t loading_shadow_epoch;
static focus_span_state focus_span;
static focus_plane_shadow_state focus_plane_shadow;
static unsigned char *frontend_page_snapshot;
static unsigned char *diagnostic_fallback_snapshot;
static unsigned char *diagnostic_framebuffer_snapshot;
static unsigned char *diagnostic_reverse_snapshot;
static uint32_t frontend_page_snapshot_generation;
static int frontend_page_snapshot_valid;
static volatile uintptr_t frontend_page_snapshot_base;
static CRITICAL_SECTION focus_span_lock;
typedef struct constructor_return_thread {
    DWORD thread_id;
    uint32_t depth;
    uintptr_t return_addresses[MTW_CONSTRUCTOR_RETURN_DEPTH];
    uintptr_t objects[MTW_CONSTRUCTOR_RETURN_DEPTH];
} constructor_return_thread;
static constructor_return_thread
    constructor_returns[MTW_CONSTRUCTOR_THREAD_CAPACITY];
static CRITICAL_SECTION constructor_return_lock;
static HANDLE diagnostic_log = INVALID_HANDLE_VALUE;
static void *capture_continue;
static void *restore_continue;
static void *commit_continue;
static void *game_unlock_target;
static void *constructor_continue;
static void *fallback_original_target;
static void *reverse_dispatcher_continue;
static void *primary_surface_unlock_continue;
static void *prebattle_entry_original_target;
static void *prebattle_resolution_return_continue;
static void *constructor_global_target;
static void *custom_acquire_target;
static void *custom_release_target;
static void *nested_acquire_target;
static void *veh_handled_continue;
static void *mapper_draw_continue;
static void *d3d11_create_success_continue;
static void *d3d11_create_failure_continue;
static SRWLOCK mapper_shader_lock = SRWLOCK_INIT;
static SRWLOCK mapper_activation_lock = SRWLOCK_INIT;
static ID3D11Device *mapper_shader_device;
static ID3D11Device *mapper_tracking_device;
static ID3D11PixelShader *mapper_shader_clone;
static ID3D11PixelShader *mapper_shader_original;
static ID3D11SamplerState *mapper_linear_sampler;
static volatile LONG mapper_shader_active_logged;
static volatile LONG mapper_sampler_active_logged;
static volatile LONG mapper_shader_target_logged;
static volatile LONG mapper_shader_all_creations;
static volatile LONG mapper_shader_target_creations;
static volatile LONG mapper_shader_draw_identity_events;
static volatile LONG mapper_frontend_mode = 1;
static volatile LONG mapper_frontend_rearm_armed;
static volatile LONG mapper_frontend_rearm_events;
static volatile LONG mapper_frontend_disarm_events;
static volatile LONG mapper_game_mode_events;
static volatile LONG mapper_frontend_owner;
static volatile LONG mapper_prebattle_entry_active;
static uintptr_t mapper_frontend_owner_root;
static DWORD mapper_frontend_owner_poll_tick;
typedef HRESULT (WINAPI *mtw_d3d11_create_device_fn)(
    IDXGIAdapter *adapter,
    D3D_DRIVER_TYPE driver_type,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL *feature_levels,
    UINT feature_level_count,
    UINT sdk_version,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *selected_feature_level,
    ID3D11DeviceContext **immediate_context);
typedef HRESULT (STDMETHODCALLTYPE *mtw_create_pixel_shader_fn)(
    ID3D11Device *device,
    const void *bytecode,
    SIZE_T bytecode_size,
    ID3D11ClassLinkage *class_linkage,
    ID3D11PixelShader **pixel_shader);
static mtw_d3d11_create_device_fn mapper_original_d3d11_create_device;
static mtw_create_pixel_shader_fn mapper_original_create_pixel_shader;
typedef LRESULT (WINAPI *mtw_dispatch_message_fn)(const MSG *message);
static mtw_dispatch_message_fn original_dispatch_message;
static void *volatile *dispatch_message_iat_slot;
static volatile LONG focus_span_substitutions;
static volatile LONG startup_slow_lane_mutations;
static volatile LONG reentrant_outer_owner_tid;
static volatile LONG reverse_authority_seen;
static focus_span_rect focus_span_full = {0, 0, 800, 599};
static volatile LONG diagnostic_event_number;
static volatile LONG diagnostic_surface_lines;
static volatile LONG diagnostic_publication_lines;
static volatile LONG diagnostic_reverse_lines;
static volatile LONG diagnostic_unmatched_lines;
static volatile LONG diagnostic_shadow_lines;
static volatile LONG diagnostic_shadow_hash_lines;
static volatile LONG diagnostic_wrapped_shadow_lines;
static volatile LONG diagnostic_mapper_lines;
static volatile LONG diagnostic_epoch_generation;
static uint32_t diagnostic_fallback_dump_generation;
static uint32_t diagnostic_backing_sample_generation;
static uint32_t diagnostic_backing_sample_sequence;
static uint32_t diagnostic_backing_last_hash;
static uint32_t diagnostic_backing_distinct_versions;
static uint32_t diagnostic_shadow_sample_generation;
static uint32_t diagnostic_shadow_last_hash;
static uint32_t diagnostic_shadow_distinct_versions;
typedef struct diagnostic_reverse_generation_slot {
    uint32_t content_generation;
    uint32_t surface_generation;
    uint32_t sample_sequence;
    uint32_t last_hash;
    uint32_t distinct_versions;
} diagnostic_reverse_generation_slot;
static diagnostic_reverse_generation_slot
    diagnostic_reverse_generations[MTW_R160_REVERSE_SLOT_CAPACITY];
static volatile LONG diagnostic_reverse_capture_busy;
static volatile LONG content_generation_events;
static volatile LONG preunlock_seed_events;
static volatile LONG reverse_handoff_events;

__declspec(naked) static void constructor_return_hook_stub(void);
__declspec(naked) static void d3d11_create_device_hook_stub(void);
__declspec(naked) static void prebattle_entry_hook_stub(void);
__declspec(naked) static void prebattle_resolution_return_hook_stub(void);
static int install_d3d11_create_device_hook(void);
static int diagnostic_trace_armed(void);
static void diagnostic_begin_content_epoch(uint32_t generation);

static void diagnostic_write(const char *text) {
    DWORD written;
    if (diagnostic_log == INVALID_HANDLE_VALUE || text == NULL) return;
    WriteFile(diagnostic_log, text, (DWORD)strlen(text), &written, NULL);
}

static void diagnostic_mapper_transition(const char *kind,
                                         const char *reason,
                                         uintptr_t callsite,
                                         uintptr_t object,
                                         uintptr_t backing,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t pitch,
                                         uint32_t bytes_per_unit,
                                         int frontend_owner_present,
                                         int frontend_lifecycle_confirmed,
                                         LONG old_mode,
                                         LONG new_mode) {
    char line[384];

    wsprintfA(
        line,
        "mapper %s r187 reason=%s tid=%lu callsite=%08lX obj=%08lX backing=%08lX w=%lu h=%lu pitch=%lu bpu=%lu owner=%d lifecycle=%d old=%ld new=%ld\r\n",
        kind, reason, GetCurrentThreadId(), (DWORD)callsite, (DWORD)object,
        (DWORD)backing, (DWORD)width, (DWORD)height, (DWORD)pitch,
        (DWORD)bytes_per_unit, frontend_owner_present,
        frontend_lifecycle_confirmed, old_mode, new_mode);
    diagnostic_write(line);
}

static int read_current_process_memory_exact(uintptr_t address,
                                             void *destination,
                                             SIZE_T size) {
    SIZE_T transferred = 0u;

    if (address == 0u || destination == NULL || size == 0u) return 0;
    return ReadProcessMemory(
               GetCurrentProcess(), (const void *)address,
               destination, size, &transferred) != 0 &&
           transferred == size;
}

static mtw_mapper_frontend_owner_kind query_frontend_owner(void) {
    mtw_mapper_frontend_owner_kind owner =
        MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
    uintptr_t render_input_root;
    uintptr_t ui_state;
    uint32_t page_count;
    uint32_t index;
    uint32_t campaign_setup_anchor_mask = 0u;

    if (game_base == 0u ||
        !read_current_process_memory_exact(
            game_base + MTW_RENDER_INPUT_ROOT_RVA,
            &render_input_root, sizeof(render_input_root)) ||
        render_input_root == 0u ||
        !read_current_process_memory_exact(
            render_input_root + MTW_FRONTEND_UI_STATE_OFFSET,
            &ui_state, sizeof(ui_state)) ||
        ui_state == 0u ||
        !read_current_process_memory_exact(
            ui_state + MTW_FRONTEND_PAGE_COUNT_OFFSET,
            &page_count, sizeof(page_count)) ||
        page_count == 0u ||
        page_count > MTW_FRONTEND_PAGE_CAPACITY) {
        return MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
    }

    for (index = 0u; index < page_count; ++index) {
        uintptr_t page;
        uintptr_t metadata;

        if (!read_current_process_memory_exact(
                ui_state + MTW_FRONTEND_PAGE_ARRAY_OFFSET +
                    index * sizeof(uintptr_t),
                &page, sizeof(page)) ||
            page == 0u ||
            !read_current_process_memory_exact(
                page + MTW_FRONTEND_PAGE_METADATA_OFFSET,
                &metadata, sizeof(metadata)) ||
            metadata == 0u) {
            return MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
        }
        owner = mtw_mapper_frontend_owner_observe_metadata(
            owner, game_base, metadata);
        campaign_setup_anchor_mask |=
            mtw_mapper_campaign_setup_metadata_anchor(
                game_base, metadata);
    }
    if (owner == MTW_MAPPER_FRONTEND_OWNER_UNKNOWN &&
        mtw_mapper_campaign_setup_metadata_complete(
            campaign_setup_anchor_mask)) {
        owner = MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP;
    }
    return owner;
}

static LONG synchronize_mapper_with_game_mode(void) {
    mtw_mapper_activation_state current;
    mtw_mapper_activation_state next;
    mtw_mapper_frontend_owner_kind owner;
    uint32_t game_mode;
    LONG previous_mode;
    LONG previous_armed;
    LONG previous_owner;
    LONG event_number;
    DWORD now;
    uintptr_t prebattle_controller;
    uintptr_t render_input_root;
    char line[256];

    if (game_base == 0u) {
        return InterlockedCompareExchange(
            &mapper_frontend_mode, 0, 0);
    }

    /*
     * This is the mode consumed by the executable's top-level 13-way state
     * switch.  Mode 0 constructs the frontend, UINT32_MAX is its idle loop,
     * and campaign/battle states use other values.
     */
    game_mode = *(volatile uint32_t *)(
        game_base + MTW_TOP_LEVEL_GAME_MODE_RVA);

    AcquireSRWLockExclusive(&mapper_activation_lock);
    previous_mode = InterlockedCompareExchange(
        &mapper_frontend_mode, 0, 0);
    previous_armed = InterlockedCompareExchange(
        &mapper_frontend_rearm_armed, 0, 0);
    previous_owner = InterlockedCompareExchange(
        &mapper_frontend_owner, 0, 0);
    current.mapper_mode = previous_mode != 0;
    current.frontend_rearm_armed = previous_armed != 0;
    owner = (mtw_mapper_frontend_owner_kind)previous_owner;
    prebattle_controller = 0u;
    render_input_root = *(volatile uintptr_t *)(
        game_base + MTW_RENDER_INPUT_ROOT_RVA);
    (void)read_current_process_memory_exact(
        game_base + MTW_PREBATTLE_CONTROLLER_RVA,
        &prebattle_controller, sizeof(prebattle_controller));
    if (prebattle_controller != 0u) {
        InterlockedExchange(&mapper_prebattle_entry_active, 0);
        owner = MTW_MAPPER_FRONTEND_OWNER_PREBATTLE;
        mapper_frontend_owner_root = 0u;
        mapper_frontend_owner_poll_tick = 0u;
    } else if (InterlockedCompareExchange(
                   &mapper_prebattle_entry_active, 0, 0) != 0) {
        owner = MTW_MAPPER_FRONTEND_OWNER_PREBATTLE;
        mapper_frontend_owner_root = 0u;
        mapper_frontend_owner_poll_tick = 0u;
    } else if (game_mode == MTW_TOP_LEVEL_MODE_CAMPAIGN) {
        /*
         * New Campaign setup shares mode 1 with campaign gameplay.  Preserve
         * the mapper only for the live setup graph and its exact root.  The
         * verified loading-plane transition clears this owner, and the core
         * deliberately refuses to rearm from it.
         */
        if (render_input_root == 0u) {
            owner = MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
            mapper_frontend_owner_root = 0u;
        } else if (owner !=
                       MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP ||
                   mapper_frontend_owner_root != render_input_root) {
            owner = query_frontend_owner();
            if (owner ==
                MTW_MAPPER_FRONTEND_OWNER_CAMPAIGN_SETUP) {
                mapper_frontend_owner_root = render_input_root;
            } else {
                owner = MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
                mapper_frontend_owner_root = 0u;
            }
        }
        mapper_frontend_owner_poll_tick = 0u;
    } else if (game_mode == MTW_TOP_LEVEL_MODE_QUICK_BATTLE) {
        /*
         * Mode 12 covers both the 3D Quick Battle and its 800x600 Battle
         * Results page.  The battle has no render/input frontend root.  The
         * results page has a live root whose page graph contains immutable
         * Battle Results descriptors.  Cache only that exact root, and never
         * let the shared mode value or surface geometry rearm the mapper.
         */
        if (render_input_root == 0u) {
            owner = MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
            mapper_frontend_owner_root = 0u;
        } else if (owner !=
                       MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS ||
                   mapper_frontend_owner_root != render_input_root) {
            owner = MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
            mapper_frontend_owner_root = 0u;
            if (current.frontend_rearm_armed) {
                owner = query_frontend_owner();
                if (owner ==
                    MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS) {
                    mapper_frontend_owner_root = render_input_root;
                } else {
                    owner = MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
                }
            }
        }
        mapper_frontend_owner_poll_tick = 0u;
    } else if (game_mode != 0u && game_mode != UINT32_MAX) {
        owner = MTW_MAPPER_FRONTEND_OWNER_UNKNOWN;
        mapper_frontend_owner_root = 0u;
        mapper_frontend_owner_poll_tick = 0u;
    } else if (owner ==
               MTW_MAPPER_FRONTEND_OWNER_BATTLE_RESULTS) {
        /*
         * Replace the cached result owner when the result page hands control
         * back to the ordinary frontend.  Keeping the mapper enabled is
         * correct, but carrying the old root identity into a later mode-12
         * transition is not.
         */
        owner = query_frontend_owner();
        mapper_frontend_owner_root = 0u;
        mapper_frontend_owner_poll_tick = GetTickCount();
    } else if (current.frontend_rearm_armed) {
        mapper_frontend_owner_root = 0u;
        now = GetTickCount();
        if (mapper_frontend_owner_poll_tick == 0u ||
            now - mapper_frontend_owner_poll_tick >=
                MTW_FRONTEND_OWNER_POLL_INTERVAL_MS) {
            owner = query_frontend_owner();
            mapper_frontend_owner_poll_tick = now;
        }
    }
    next = mtw_mapper_activation_after_game_mode(
        current, game_mode, owner);
    InterlockedExchange(
        &mapper_frontend_mode, (LONG)next.mapper_mode);
    InterlockedExchange(
        &mapper_frontend_rearm_armed,
        (LONG)next.frontend_rearm_armed);
    InterlockedExchange(&mapper_frontend_owner, (LONG)owner);
    ReleaseSRWLockExclusive(&mapper_activation_lock);

    if (previous_mode != (LONG)next.mapper_mode ||
        previous_armed != (LONG)next.frontend_rearm_armed ||
        previous_owner != (LONG)owner) {
        event_number = InterlockedIncrement(&mapper_game_mode_events);
        if (event_number <= 32) {
            wsprintfA(
                line,
                "mapper game-mode r194 event=%ld tid=%lu mode=%08lX owner=%ld->%d old=%ld/%ld new=%d/%d\r\n",
                event_number, GetCurrentThreadId(), (DWORD)game_mode,
                previous_owner, (int)owner,
                previous_mode, previous_armed, next.mapper_mode,
                next.frontend_rearm_armed);
            diagnostic_write(line);
        }
        if (previous_mode == 0 && next.mapper_mode != 0) {
            InterlockedIncrement(&mapper_frontend_rearm_events);
        }
    }
    return (LONG)next.mapper_mode;
}

static void mapper_begin_prebattle_entry(void) {
    mtw_mapper_activation_state current;
    mtw_mapper_activation_state next;

    AcquireSRWLockExclusive(&mapper_activation_lock);
    current.mapper_mode = InterlockedCompareExchange(
        &mapper_frontend_mode, 0, 0) != 0;
    current.frontend_rearm_armed = InterlockedCompareExchange(
        &mapper_frontend_rearm_armed, 0, 0) != 0;
    next = mtw_mapper_activation_after_game_mode(
        current, 1u, MTW_MAPPER_FRONTEND_OWNER_PREBATTLE);
    InterlockedExchange(&mapper_prebattle_entry_active, 1);
    InterlockedExchange(
        &mapper_frontend_owner,
        (LONG)MTW_MAPPER_FRONTEND_OWNER_PREBATTLE);
    mapper_frontend_owner_root = 0u;
    mapper_frontend_owner_poll_tick = 0u;
    InterlockedExchange(
        &mapper_frontend_mode, (LONG)next.mapper_mode);
    InterlockedExchange(
        &mapper_frontend_rearm_armed,
        (LONG)next.frontend_rearm_armed);
    ReleaseSRWLockExclusive(&mapper_activation_lock);
    diagnostic_write("mapper prebattle resolution prearmed r194\r\n");
}

static void mapper_finish_prebattle_resolution(void) {
    mtw_mapper_activation_state current;
    mtw_mapper_activation_state next;
    uint32_t game_mode;
    uintptr_t prebattle_controller;
    LONG previous_mode;
    int controller_present;
    char line[192];

    if (game_base == 0u) return;
    game_mode = *(volatile uint32_t *)(
        game_base + MTW_TOP_LEVEL_GAME_MODE_RVA);
    prebattle_controller = 0u;
    (void)read_current_process_memory_exact(
        game_base + MTW_PREBATTLE_CONTROLLER_RVA,
        &prebattle_controller, sizeof(prebattle_controller));
    controller_present = prebattle_controller != 0u;

    AcquireSRWLockExclusive(&mapper_activation_lock);
    previous_mode = InterlockedCompareExchange(
        &mapper_frontend_mode, 0, 0);
    current.mapper_mode = previous_mode != 0;
    current.frontend_rearm_armed = InterlockedCompareExchange(
        &mapper_frontend_rearm_armed, 0, 0) != 0;
    next = mtw_mapper_activation_after_prebattle_resolution(
        current, game_mode, controller_present);
    InterlockedExchange(&mapper_prebattle_entry_active, 0);
    InterlockedExchange(
        &mapper_frontend_owner,
        controller_present ?
            (LONG)MTW_MAPPER_FRONTEND_OWNER_PREBATTLE :
            (LONG)MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    mapper_frontend_owner_root = 0u;
    mapper_frontend_owner_poll_tick = 0u;
    InterlockedExchange(
        &mapper_frontend_mode, (LONG)next.mapper_mode);
    InterlockedExchange(
        &mapper_frontend_rearm_armed,
        (LONG)next.frontend_rearm_armed);
    ReleaseSRWLockExclusive(&mapper_activation_lock);

    wsprintfA(
        line,
        "mapper prebattle resolution completed r194 controller=%d old=%ld new=%d/%d\r\n",
        controller_present, previous_mode, next.mapper_mode,
        next.frontend_rearm_armed);
    diagnostic_write(line);
}

static LONG observe_mapper_surface(uint16_t width,
                                   uint16_t height,
                                   uint32_t pitch,
                                   uint8_t bytes_per_unit) {
    mtw_mapper_activation_state current;
    mtw_mapper_activation_state next;
    int render_input_root_present;

    render_input_root_present =
        game_base != 0u &&
        *(volatile uintptr_t *)(
            game_base + MTW_RENDER_INPUT_ROOT_RVA) != 0u;

    AcquireSRWLockExclusive(&mapper_activation_lock);
    current.mapper_mode = InterlockedCompareExchange(
        &mapper_frontend_mode, 0, 0) != 0;
    current.frontend_rearm_armed = InterlockedCompareExchange(
        &mapper_frontend_rearm_armed, 0, 0) != 0;
    next = mtw_mapper_activation_after_surface(
        current, render_input_root_present,
        width, height, pitch, bytes_per_unit);
    InterlockedExchange(
        &mapper_frontend_mode, (LONG)next.mapper_mode);
    InterlockedExchange(
        &mapper_frontend_rearm_armed,
        (LONG)next.frontend_rearm_armed);
    ReleaseSRWLockExclusive(&mapper_activation_lock);
    return (LONG)next.mapper_mode;
}

static void diagnostic_dump_page_snapshot(uint32_t generation,
                                          const unsigned char *bytes) {
    char directory[MAX_PATH];
    char path[MAX_PATH * 2];
    DWORD length;
    DWORD written = 0u;
    HANDLE file;

    if (bytes == NULL) return;
    length = GetEnvironmentVariableA(
        "MTW_FRONTEND_DUMP_DIR", directory, (DWORD)sizeof(directory));
    if (length == 0u || length >= (DWORD)sizeof(directory)) return;
    wsprintfA(path, "%s\\r103-page-generation-%lu.raw",
              directory, (DWORD)generation);
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, bytes, MTW_FOCUS_PLANE_BYTES, &written, NULL);
    CloseHandle(file);
}

static LRESULT WINAPI frontend_dispatch_message(const MSG *message) {
    int begin_epoch = 0;
    int transferred = 0;
    uint32_t generation = 0u;
    uint32_t live_count = 0u;
    LRESULT result;
    char line[256];

    if (synchronize_mapper_with_game_mode() == 0) {
        return original_dispatch_message == NULL ? 0 :
               original_dispatch_message(message);
    }

    if (message != NULL) {
        EnterCriticalSection(&focus_span_lock);
        live_count = focus_span_live_count(&focus_span);
        begin_epoch = frontend_epoch_should_begin(
            (uint32_t)message->message, (uintptr_t)message->wParam,
            live_count);
        if (begin_epoch) {
            transferred = focus_span_begin_content_generation(&focus_span);
            generation = focus_span_content_generation(&focus_span);
            frontend_page_snapshot_valid = 0;
            frontend_page_snapshot_generation = 0u;
        }
        LeaveCriticalSection(&focus_span_lock);
        if (begin_epoch) {
            diagnostic_begin_content_epoch(generation);
        }
        if (begin_epoch &&
            InterlockedIncrement(&content_generation_events) <= 256) {
            wsprintfA(line,
                      "frontend content generation r183 gen=%lu msg=%04lX wp=%08lX live=%lu authority_transferred=%d\r\n",
                      (DWORD)generation, (DWORD)message->message,
                      (DWORD)(uintptr_t)message->wParam, (DWORD)live_count,
                      transferred);
            diagnostic_write(line);
        }
    }
    result = original_dispatch_message == NULL ? 0 :
             original_dispatch_message(message);
    return result;
}

static int install_dispatch_message_hook(void) {
    void *expected;
    void *current;
    DWORD old_protect;
    DWORD ignored;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");

    if (game_base == 0u || user32 == NULL) return 0;
    expected = (void *)GetProcAddress(user32, "DispatchMessageA");
    dispatch_message_iat_slot =
        (void *volatile *)(game_base + MTW_DISPATCH_MESSAGE_IAT_RVA);
    current = *dispatch_message_iat_slot;
    if (expected == NULL || current != expected ||
        !VirtualProtect((void *)dispatch_message_iat_slot, sizeof(void *),
                        PAGE_READWRITE, &old_protect)) {
        dispatch_message_iat_slot = NULL;
        return 0;
    }
    original_dispatch_message = (mtw_dispatch_message_fn)current;
    InterlockedExchangePointer(
        (PVOID volatile *)dispatch_message_iat_slot,
        (PVOID)frontend_dispatch_message);
    FlushInstructionCache(
        GetCurrentProcess(), (const void *)dispatch_message_iat_slot,
        sizeof(void *));
    VirtualProtect((void *)dispatch_message_iat_slot, sizeof(void *),
                   old_protect, &ignored);
    return 1;
}

static uintptr_t choose_preunlock_plane_base(uintptr_t framebuffer_bits,
                                             DWORD pitch_bytes) {
    uintptr_t retained = frontend_page_snapshot_base;
    if (framebuffer_bits == 0u || pitch_bytes != MTW_FRONTEND_ROW_BYTES) {
        return 0u;
    }
    if (retained != 0u && framebuffer_bits >= retained &&
        framebuffer_bits - retained < MTW_FRONTEND_PLANE_BYTES) {
        return retained;
    }
    return framebuffer_bits;
}

static void capture_preunlock_page_seed(uintptr_t caller) {
    guarded_memory_copy_stats copy_stats;
    char line[320];
    DWORD pitch_pixels;
    DWORD pitch_bytes;
    uintptr_t framebuffer_bits;
    uintptr_t plane_base = 0u;
    uintptr_t current_offset = 0u;
    uint32_t generation;
    int copied = 0;
    int dump_snapshot = 0;
    LONG event_number;

    if (game_base == 0u || frontend_page_snapshot == NULL) return;
    framebuffer_bits = *(volatile uintptr_t *)(
        game_base + MTW_FRONTEND_FRAMEBUFFER_BITS_RVA);
    pitch_pixels = *(volatile uint16_t *)(
        game_base + MTW_FRONTEND_FRAMEBUFFER_PITCH_RVA);
    pitch_bytes = pitch_pixels * 2u;

    EnterCriticalSection(&focus_span_lock);
    generation = focus_span_content_generation(&focus_span);
    if (generation != 0u &&
        !(frontend_page_snapshot_valid &&
          frontend_page_snapshot_generation == generation)) {
        plane_base = choose_preunlock_plane_base(
            framebuffer_bits, pitch_bytes);
        if (plane_base != 0u) {
            memset(&copy_stats, 0, sizeof(copy_stats));
            copied = guarded_memory_copy(
                frontend_page_snapshot, (const void *)plane_base,
                MTW_FRONTEND_PLANE_BYTES, &copy_stats) &&
                copy_stats.copied_bytes == MTW_FRONTEND_PLANE_BYTES &&
                copy_stats.guard_regions == copy_stats.restored_regions;
            if (!copied && plane_base != framebuffer_bits) {
                memset(&copy_stats, 0, sizeof(copy_stats));
                plane_base = framebuffer_bits;
                copied = guarded_memory_copy(
                    frontend_page_snapshot, (const void *)plane_base,
                    MTW_FRONTEND_PLANE_BYTES, &copy_stats) &&
                    copy_stats.copied_bytes == MTW_FRONTEND_PLANE_BYTES &&
                    copy_stats.guard_regions == copy_stats.restored_regions;
            }
            if (copied) {
                frontend_page_snapshot_base = plane_base;
                current_offset =
                    framebuffer_bits - frontend_page_snapshot_base;
                frontend_page_snapshot_generation = generation;
                frontend_page_snapshot_valid = 1;
                dump_snapshot = 1;
            }
        }
    } else {
        plane_base = frontend_page_snapshot_base;
        if (plane_base != 0u && framebuffer_bits >= plane_base) {
            current_offset = framebuffer_bits - plane_base;
        }
    }
    LeaveCriticalSection(&focus_span_lock);

    event_number = InterlockedIncrement(&preunlock_seed_events);
    if (event_number <= 256) {
        wsprintfA(
            line,
            "R116 preunlock seed event=%ld tid=%lu caller=%08lX gen=%lu ptr=%08lX base=%08lX offset=%lu pitch=%lu copied=%d\r\n",
            event_number, GetCurrentThreadId(), (DWORD)caller,
            (DWORD)generation, (DWORD)framebuffer_bits, (DWORD)plane_base,
            (DWORD)current_offset, pitch_bytes, copied);
        diagnostic_write(line);
    }
    if (dump_snapshot) {
        diagnostic_dump_page_snapshot(generation, frontend_page_snapshot);
    }
}

static void diagnostic_open(void) {
    char path[MAX_PATH];
    DWORD length = GetEnvironmentVariableA(
        "MTW_FRONTEND_FIX_LOG", path, (DWORD)sizeof(path));
    if (length == 0u || length >= (DWORD)sizeof(path)) return;
    if (diagnostic_log == INVALID_HANDLE_VALUE) {
        diagnostic_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                                     NULL, OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

static int backend_image_is_supported(HMODULE module) {
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS32 *nt;
    if (module == NULL) return 0;
    dos = (const IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
    nt = (const IMAGE_NT_HEADERS32 *)((const unsigned char *)module +
                                      (size_t)dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->FileHeader.TimeDateStamp == DGVOODOO_EXPECTED_TIMESTAMP &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->OptionalHeader.SizeOfImage == DGVOODOO_EXPECTED_IMAGE_SIZE;
}

static uint32_t mapper_shader_fingerprint(const void *bytecode,
                                          size_t bytecode_size) {
    const unsigned char *bytes = (const unsigned char *)bytecode;
    uint32_t value = 2166136261u;
    size_t index;
    if (bytes == NULL) return 0u;
    for (index = 0u; index < bytecode_size; ++index) {
        value ^= bytes[index];
        value *= 16777619u;
    }
    return value;
}

static HRESULT STDMETHODCALLTYPE mapper_create_pixel_shader_hook(
    ID3D11Device *device,
    const void *bytecode,
    SIZE_T bytecode_size,
    ID3D11ClassLinkage *class_linkage,
    ID3D11PixelShader **pixel_shader) {
    HRESULT result;
    ID3D11PixelShader *old_shader = NULL;
    ID3D11PixelShader *old_clone = NULL;
    ID3D11PixelShader *replacement_shader = NULL;
    ID3D11SamplerState *old_sampler = NULL;
    ID3D11SamplerState *replacement_sampler = NULL;
    ID3D11Device *old_clone_device = NULL;
    ID3D11Device *held_device = NULL;
    int accepted = 0;
    LONG creation_event;
    LONG all_event;
    uint32_t fingerprint;
    int exact;
    const unsigned char *replacement_bytecode = NULL;
    size_t replacement_bytecode_size = 0u;
    D3D11_SAMPLER_DESC sampler_desc;
    char line[256];

    if (mapper_original_create_pixel_shader == NULL) return E_FAIL;
    result = mapper_original_create_pixel_shader(
        device, bytecode, bytecode_size, class_linkage, pixel_shader);
    if (SUCCEEDED(result) && pixel_shader != NULL && *pixel_shader != NULL) {
        exact = mtw_mapper_problem_pixel_shader_matches(
            bytecode, (size_t)bytecode_size);
        all_event = InterlockedIncrement(&mapper_shader_all_creations);
        fingerprint = mapper_shader_fingerprint(
            bytecode, (size_t)bytecode_size);
        if (all_event <= 128) {
            wsprintfA(line,
                      "mapper shader created r154 event=%ld device=%08lX shader=%08lX bytes=%lu fnv=%08lX exact=%d\r\n",
                      all_event, (DWORD)(uintptr_t)device,
                      (DWORD)(uintptr_t)*pixel_shader,
                      (DWORD)bytecode_size, (DWORD)fingerprint, exact);
            diagnostic_write(line);
        }
    } else {
        exact = 0;
    }
    if (SUCCEEDED(result) && pixel_shader != NULL && *pixel_shader != NULL &&
        exact) {
        replacement_bytecode = mtw_mapper_pixel_shader_bytecode(
            &replacement_bytecode_size);
        ZeroMemory(&sampler_desc, sizeof(sampler_desc));
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MaxAnisotropy = 1u;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_desc.MinLOD = 0.0f;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        if (replacement_bytecode != NULL && replacement_bytecode_size != 0u &&
            SUCCEEDED(mapper_original_create_pixel_shader(
                device, replacement_bytecode, replacement_bytecode_size, NULL,
                &replacement_shader)) && replacement_shader != NULL &&
            SUCCEEDED(ID3D11Device_CreateSamplerState(
                device, &sampler_desc, &replacement_sampler)) &&
            replacement_sampler != NULL) {
            ID3D11PixelShader_AddRef(*pixel_shader);
            ID3D11Device_AddRef(device);
            held_device = device;
            AcquireSRWLockExclusive(&mapper_shader_lock);
            if (device == mapper_tracking_device) {
                old_shader = mapper_shader_original;
                old_clone = mapper_shader_clone;
                old_sampler = mapper_linear_sampler;
                old_clone_device = mapper_shader_device;
                mapper_shader_original = *pixel_shader;
                mapper_shader_clone = replacement_shader;
                mapper_linear_sampler = replacement_sampler;
                mapper_shader_device = held_device;
                replacement_shader = NULL;
                replacement_sampler = NULL;
                held_device = NULL;
                accepted = 1;
            }
            ReleaseSRWLockExclusive(&mapper_shader_lock);
            if (!accepted) {
                ID3D11PixelShader_Release(*pixel_shader);
            }
        }
        if (replacement_shader != NULL) {
            ID3D11PixelShader_Release(replacement_shader);
        }
        if (replacement_sampler != NULL) {
            ID3D11SamplerState_Release(replacement_sampler);
        }
        if (held_device != NULL) ID3D11Device_Release(held_device);
        if (old_shader != NULL) ID3D11PixelShader_Release(old_shader);
        if (old_clone != NULL) ID3D11PixelShader_Release(old_clone);
        if (old_sampler != NULL) ID3D11SamplerState_Release(old_sampler);
        if (old_clone_device != NULL) ID3D11Device_Release(old_clone_device);
        if (accepted &&
            InterlockedCompareExchange(&mapper_shader_target_logged,
                                       1, 0) == 0) {
            diagnostic_write("mapper shader target identified r154\r\n");
            diagnostic_write("mapper shader single-sample linear ready r154\r\n");
        }
        creation_event = InterlockedIncrement(&mapper_shader_target_creations);
        if (creation_event <= 32) {
            wsprintfA(line,
                      "mapper shader target object r154 event=%ld accepted=%d device=%08lX shader=%08lX\r\n",
                      creation_event, accepted, (DWORD)(uintptr_t)device,
                      (DWORD)(uintptr_t)*pixel_shader);
            diagnostic_write(line);
        }
    }
    return result;
}

static int mapper_install_device_shader_hook(ID3D11Device *device) {
    void **vtable;
    void **slot;
    void *current;
    ID3D11Device *old_device = NULL;
    DWORD old_protect;
    DWORD ignored;
    int installed = 0;

    if (device == NULL) return 0;
    vtable = *(void ***)device;
    if (vtable == NULL) return 0;
    slot = &vtable[MTW_DEVICE_CREATE_PIXEL_SHADER_INDEX];
    AcquireSRWLockExclusive(&mapper_shader_lock);
    current = *slot;
    if (current == (void *)mapper_create_pixel_shader_hook &&
        mapper_original_create_pixel_shader != NULL) {
        installed = 1;
    } else if (mapper_original_create_pixel_shader == NULL &&
               current != NULL &&
               VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE,
                              &old_protect)) {
        mapper_original_create_pixel_shader =
            (mtw_create_pixel_shader_fn)current;
        InterlockedExchangePointer((void *volatile *)slot,
                                   (void *)mapper_create_pixel_shader_hook);
        VirtualProtect(slot, sizeof(*slot), old_protect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        installed = *slot == (void *)mapper_create_pixel_shader_hook;
        if (!installed) mapper_original_create_pixel_shader = NULL;
    }
    if (installed) {
        ID3D11Device_AddRef(device);
        old_device = mapper_tracking_device;
        mapper_tracking_device = device;
    }
    ReleaseSRWLockExclusive(&mapper_shader_lock);
    if (old_device != NULL) ID3D11Device_Release(old_device);
    if (installed) {
        diagnostic_write("mapper shader creation hook active r154\r\n");
    }
    return installed;
}

static HRESULT WINAPI mapper_d3d11_create_device_hook(
    IDXGIAdapter *adapter,
    D3D_DRIVER_TYPE driver_type,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL *feature_levels,
    UINT feature_level_count,
    UINT sdk_version,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *selected_feature_level,
    ID3D11DeviceContext **immediate_context) {
    HRESULT result;
    if (mapper_original_d3d11_create_device == NULL) return E_FAIL;
    result = mapper_original_d3d11_create_device(
        adapter, driver_type, software, flags, feature_levels,
        feature_level_count, sdk_version, device, selected_feature_level,
        immediate_context);
    if (SUCCEEDED(result) && device != NULL && *device != NULL) {
        (void)mapper_install_device_shader_hook(*device);
    }
    return result;
}

__declspec(naked) static void d3d11_create_device_hook_stub(void) {
    __asm {
        mov dword ptr [mapper_original_d3d11_create_device], edx
        call mapper_d3d11_create_device_hook
        test eax, eax
        js create_failed
        jmp dword ptr [d3d11_create_success_continue]
    create_failed:
        jmp dword ptr [d3d11_create_failure_continue]
    }
}

static HMODULE load_sibling_backend(void) {
    WCHAR path[MAX_PATH];
    WCHAR *filename;
    DWORD length;
    HMODULE module = GetModuleHandleW(L"dgVoodoo_D3D9.dll");
    if (module != NULL) return module;
    length = GetModuleFileNameW((HMODULE)&__ImageBase, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH) return NULL;
    filename = path + length;
    while (filename > path && filename[-1] != L'\\' &&
           filename[-1] != L'/') {
        --filename;
    }
    if ((size_t)(filename - path) +
            (size_t)lstrlenW(L"dgVoodoo_D3D9.dll") + 1u > MAX_PATH) {
        return NULL;
    }
    lstrcpyW(filename, L"dgVoodoo_D3D9.dll");
    return LoadLibraryW(path);
}

static BOOL CALLBACK prepare_backend_tracking(PINIT_ONCE once,
                                              PVOID parameter,
                                              PVOID *context) {
    HMODULE module;
    (void)once;
    (void)parameter;
    (void)context;
    diagnostic_open();
    module = load_sibling_backend();
    if (!backend_image_is_supported(module)) {
        diagnostic_write("backend tracking unsupported r154\r\n");
        return TRUE;
    }
    backend_base = (uintptr_t)module;
    if (install_d3d11_create_device_hook()) {
        diagnostic_write("D3D11CreateDevice callsite hook installed r154\r\n");
    } else {
        diagnostic_write("D3D11CreateDevice callsite hook unavailable r154\r\n");
    }
    return TRUE;
}

static int diagnostic_trace_armed(void) {
    char path[MAX_PATH];
    DWORD length = GetEnvironmentVariableA(
        "MTW_FRONTEND_TRACE_ARM", path, (DWORD)sizeof(path));
    return length != 0u && length < (DWORD)sizeof(path) &&
           GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static uint32_t diagnostic_fnv1a32(const unsigned char *bytes, size_t length) {
    uint32_t value = 2166136261u;
    size_t index;
    if (bytes == NULL) return 0u;
    for (index = 0u; index < length; ++index) {
        value ^= bytes[index];
        value *= 16777619u;
    }
    return value;
}

static void diagnostic_dump_named_plane(const char *name,
                                        uint32_t generation,
                                        const unsigned char *bytes) {
    char directory[MAX_PATH];
    char path[MAX_PATH * 2];
    DWORD length;
    DWORD written = 0u;
    HANDLE file;

    if (name == NULL || bytes == NULL) return;
    length = GetEnvironmentVariableA(
        "MTW_FRONTEND_DUMP_DIR", directory, (DWORD)sizeof(directory));
    if (length == 0u || length >= (DWORD)sizeof(directory)) return;
    wsprintfA(path, "%s\\r157-%s-generation-%lu.raw",
              directory, name, (DWORD)generation);
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, bytes, MTW_FOCUS_PLANE_BYTES, &written, NULL);
    CloseHandle(file);
}

static void diagnostic_capture_first_fallback_planes(
    uint32_t content_generation,
    uintptr_t backing) {
    guarded_memory_copy_stats backing_stats;
    guarded_memory_copy_stats framebuffer_stats;
    uintptr_t framebuffer_bits;
    uintptr_t framebuffer_base;
    DWORD pitch_pixels;
    DWORD pitch_bytes;
    int backing_copied;
    int framebuffer_copied;
    char line[384];

    if (!diagnostic_trace_armed() || content_generation == 0u ||
        backing == 0u || diagnostic_fallback_snapshot == NULL ||
        diagnostic_framebuffer_snapshot == NULL ||
        diagnostic_fallback_dump_generation == content_generation) {
        return;
    }
    diagnostic_fallback_dump_generation = content_generation;
    memset(&backing_stats, 0, sizeof(backing_stats));
    backing_copied = guarded_memory_copy(
        diagnostic_fallback_snapshot, (const void *)backing,
        MTW_FOCUS_PLANE_BYTES, &backing_stats) &&
        backing_stats.copied_bytes == MTW_FOCUS_PLANE_BYTES &&
        backing_stats.guard_regions == backing_stats.restored_regions;

    framebuffer_bits = *(volatile uintptr_t *)(
        game_base + MTW_FRONTEND_FRAMEBUFFER_BITS_RVA);
    pitch_pixels = *(volatile uint16_t *)(
        game_base + MTW_FRONTEND_FRAMEBUFFER_PITCH_RVA);
    pitch_bytes = pitch_pixels * 2u;
    framebuffer_base = choose_preunlock_plane_base(
        framebuffer_bits, pitch_bytes);
    memset(&framebuffer_stats, 0, sizeof(framebuffer_stats));
    framebuffer_copied = framebuffer_base != 0u && guarded_memory_copy(
        diagnostic_framebuffer_snapshot, (const void *)framebuffer_base,
        MTW_FOCUS_PLANE_BYTES, &framebuffer_stats) &&
        framebuffer_stats.copied_bytes == MTW_FOCUS_PLANE_BYTES &&
        framebuffer_stats.guard_regions == framebuffer_stats.restored_regions;

    if (backing_copied) {
        diagnostic_dump_named_plane(
            "fallback", content_generation, diagnostic_fallback_snapshot);
    }
    if (framebuffer_copied) {
        diagnostic_dump_named_plane(
            "framebuffer", content_generation,
            diagnostic_framebuffer_snapshot);
    }
    wsprintfA(
        line,
        "R157 first-fallback cgen=%lu backing=%08lX backingCopy=%d backingHash=%08lX framebuffer=%08lX base=%08lX pitch=%lu framebufferCopy=%d framebufferHash=%08lX\r\n",
        (DWORD)content_generation, (DWORD)backing, backing_copied,
        backing_copied ? (DWORD)diagnostic_fnv1a32(
            diagnostic_fallback_snapshot, MTW_FOCUS_PLANE_BYTES) : 0u,
        (DWORD)framebuffer_bits, (DWORD)framebuffer_base, pitch_bytes,
        framebuffer_copied,
        framebuffer_copied ? (DWORD)diagnostic_fnv1a32(
            diagnostic_framebuffer_snapshot, MTW_FOCUS_PLANE_BYTES) : 0u);
    diagnostic_write(line);
}

static void diagnostic_dump_versioned_plane(const char *kind,
                                            uint32_t content_generation,
                                            uint32_t version,
                                            const unsigned char *bytes) {
    char directory[MAX_PATH];
    char path[MAX_PATH * 2];
    DWORD length;
    DWORD written = 0u;
    HANDLE file;

    if (kind == NULL || bytes == NULL || version == 0u ||
        version > MTW_R159_MAX_DISTINCT_VERSIONS) return;
    length = GetEnvironmentVariableA(
        "MTW_FRONTEND_DUMP_DIR", directory, (DWORD)sizeof(directory));
    if (length == 0u || length >= (DWORD)sizeof(directory)) return;
    if (lstrcmpA(kind, "backing") == 0) {
        wsprintfA(path, "%s\\r159-backing-cgen-%lu-version-%lu.raw",
                  directory, (DWORD)content_generation, (DWORD)version);
    } else {
        wsprintfA(path, "%s\\r159-shadow-cgen-%lu-version-%lu.raw",
                  directory, (DWORD)content_generation, (DWORD)version);
    }
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, bytes, MTW_FOCUS_PLANE_BYTES, &written, NULL);
    CloseHandle(file);
}

static uint32_t diagnostic_sample_backing_evolution(
    uint32_t content_generation,
    uint32_t surface_generation,
    uintptr_t backing,
    const focus_span_rect *rect) {
    guarded_memory_copy_stats copy_stats;
    uint32_t hash;
    uint32_t version;
    char line[384];

    if (!diagnostic_trace_armed() || content_generation == 0u ||
        backing == 0u || rect == NULL ||
        diagnostic_fallback_snapshot == NULL) return 0u;
    if (diagnostic_backing_sample_generation != content_generation) {
        diagnostic_backing_sample_generation = content_generation;
        diagnostic_backing_sample_sequence = 0u;
        diagnostic_backing_last_hash = 0u;
        diagnostic_backing_distinct_versions = 0u;
    }
    diagnostic_backing_sample_sequence += 1u;
    if (diagnostic_backing_sample_sequence != 1u &&
        diagnostic_backing_sample_sequence % MTW_R159_SAMPLE_INTERVAL != 0u) {
        return 0u;
    }
    memset(&copy_stats, 0, sizeof(copy_stats));
    if (!guarded_memory_copy(
            diagnostic_fallback_snapshot, (const void *)backing,
            MTW_FOCUS_PLANE_BYTES, &copy_stats) ||
        copy_stats.copied_bytes != MTW_FOCUS_PLANE_BYTES ||
        copy_stats.guard_regions != copy_stats.restored_regions) {
        return diagnostic_backing_sample_sequence;
    }
    hash = diagnostic_fnv1a32(
        diagnostic_fallback_snapshot, MTW_FOCUS_PLANE_BYTES);
    if (hash == diagnostic_backing_last_hash &&
        diagnostic_backing_distinct_versions != 0u) {
        return diagnostic_backing_sample_sequence;
    }
    diagnostic_backing_last_hash = hash;
    diagnostic_backing_distinct_versions += 1u;
    version = diagnostic_backing_distinct_versions;
    if (version <= MTW_R159_MAX_DISTINCT_VERSIONS) {
        diagnostic_dump_versioned_plane(
            "backing", content_generation, version,
            diagnostic_fallback_snapshot);
        wsprintfA(
            line,
            "R159 backing-version cgen=%lu sgen=%lu sample=%lu version=%lu backing=%08lX hash=%08lX rect=%ld,%ld,%ld,%ld\r\n",
            (DWORD)content_generation, (DWORD)surface_generation,
            (DWORD)diagnostic_backing_sample_sequence, (DWORD)version,
            (DWORD)backing, (DWORD)hash, (LONG)rect->left,
            (LONG)rect->top, (LONG)rect->right, (LONG)rect->bottom);
        diagnostic_write(line);
    }
    return diagnostic_backing_sample_sequence;
}

static void diagnostic_sample_shadow_evolution(
    uint32_t content_generation,
    uint32_t surface_generation,
    uintptr_t backing,
    const focus_span_rect *rect,
    uint32_t sample_sequence,
    const unsigned char *shadow_bytes,
    int published) {
    uint32_t hash;
    uint32_t version;
    char line[384];

    if (!diagnostic_trace_armed() || content_generation == 0u ||
        backing == 0u || rect == NULL || sample_sequence == 0u ||
        shadow_bytes == NULL) return;
    if (diagnostic_shadow_sample_generation != content_generation) {
        diagnostic_shadow_sample_generation = content_generation;
        diagnostic_shadow_last_hash = 0u;
        diagnostic_shadow_distinct_versions = 0u;
    }
    hash = diagnostic_fnv1a32(shadow_bytes, MTW_FOCUS_PLANE_BYTES);
    if (hash == diagnostic_shadow_last_hash &&
        diagnostic_shadow_distinct_versions != 0u) return;
    diagnostic_shadow_last_hash = hash;
    diagnostic_shadow_distinct_versions += 1u;
    version = diagnostic_shadow_distinct_versions;
    if (version <= MTW_R159_MAX_DISTINCT_VERSIONS) {
        diagnostic_dump_versioned_plane(
            "shadow", content_generation, version, shadow_bytes);
        wsprintfA(
            line,
            "R159 shadow-version cgen=%lu sgen=%lu sample=%lu version=%lu backing=%08lX hash=%08lX publish=%d rect=%ld,%ld,%ld,%ld\r\n",
            (DWORD)content_generation, (DWORD)surface_generation,
            (DWORD)sample_sequence, (DWORD)version, (DWORD)backing,
            (DWORD)hash, published, (LONG)rect->left, (LONG)rect->top,
            (LONG)rect->right, (LONG)rect->bottom);
        diagnostic_write(line);
    }
}

static diagnostic_reverse_generation_slot *
diagnostic_reverse_generation_slot_for(uint32_t content_generation,
                                       uint32_t surface_generation) {
    uint32_t index;
    diagnostic_reverse_generation_slot *empty = NULL;

    for (index = 0u; index < MTW_R160_REVERSE_SLOT_CAPACITY; ++index) {
        diagnostic_reverse_generation_slot *slot =
            &diagnostic_reverse_generations[index];
        if (slot->content_generation == content_generation &&
            slot->surface_generation == surface_generation) {
            return slot;
        }
        if (empty == NULL && slot->content_generation == 0u) empty = slot;
    }
    if (empty == NULL) return NULL;
    memset(empty, 0, sizeof(*empty));
    empty->content_generation = content_generation;
    empty->surface_generation = surface_generation;
    return empty;
}

static void diagnostic_dump_post_reverse_plane(
    uint32_t content_generation,
    uint32_t surface_generation,
    uint32_t version,
    const unsigned char *bytes) {
    char directory[MAX_PATH];
    char path[MAX_PATH * 2];
    DWORD length;
    DWORD written = 0u;
    HANDLE file;

    if (bytes == NULL || version == 0u ||
        version > MTW_R160_REVERSE_MAX_DISTINCT_VERSIONS) return;
    length = GetEnvironmentVariableA(
        "MTW_FRONTEND_DUMP_DIR", directory, (DWORD)sizeof(directory));
    if (length == 0u || length >= (DWORD)sizeof(directory)) return;
    wsprintfA(
        path,
        "%s\\r160-reverse-cgen-%lu-sgen-%lu-version-%lu.raw",
        directory, (DWORD)content_generation, (DWORD)surface_generation,
        (DWORD)version);
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, bytes, MTW_FOCUS_PLANE_BYTES, &written, NULL);
    CloseHandle(file);
}

__declspec(noinline) static void
diagnostic_capture_post_reverse_generation(void *object) {
    diagnostic_reverse_generation_slot *slot;
    guarded_memory_copy_stats copy_stats;
    uintptr_t backing = 0u;
    uint32_t content_generation = 0u;
    uint32_t surface_generation = 0u;
    uint32_t hash;
    uint32_t version;
    SIZE_T transferred = 0u;
    char line[384];

    if (!diagnostic_trace_armed() || object == NULL ||
        diagnostic_reverse_snapshot == NULL ||
        InterlockedCompareExchange(
            &diagnostic_reverse_capture_busy, 1, 0) != 0) {
        return;
    }

    do {
        if (!ReadProcessMemory(
                GetCurrentProcess(),
                (const unsigned char *)object + 0x20u,
                &backing, sizeof(backing), &transferred) ||
            transferred != sizeof(backing) || backing == 0u) {
            break;
        }

        EnterCriticalSection(&focus_span_lock);
        (void)focus_span_refresh_backing(
            &focus_span, (uintptr_t)object, backing);
        surface_generation = focus_span_generation_for_object(
            &focus_span, (uintptr_t)object);
        content_generation = focus_span.content_generation;
        slot = diagnostic_reverse_generation_slot_for(
            content_generation, surface_generation);
        if (slot != NULL) slot->sample_sequence += 1u;
        LeaveCriticalSection(&focus_span_lock);

        if (slot == NULL || content_generation == 0u ||
            surface_generation == 0u ||
            (slot->sample_sequence != 1u &&
             slot->sample_sequence % MTW_R160_REVERSE_SAMPLE_INTERVAL != 0u)) {
            break;
        }

        memset(&copy_stats, 0, sizeof(copy_stats));
        if (!guarded_memory_copy(
                diagnostic_reverse_snapshot, (const void *)backing,
                MTW_FOCUS_PLANE_BYTES, &copy_stats) ||
            copy_stats.copied_bytes != MTW_FOCUS_PLANE_BYTES ||
            copy_stats.guard_regions != copy_stats.restored_regions) {
            break;
        }
        hash = diagnostic_fnv1a32(
            diagnostic_reverse_snapshot, MTW_FOCUS_PLANE_BYTES);
        if (slot->distinct_versions != 0u && slot->last_hash == hash) break;
        slot->last_hash = hash;
        slot->distinct_versions += 1u;
        version = slot->distinct_versions;
        if (version > MTW_R160_REVERSE_MAX_DISTINCT_VERSIONS) break;

        diagnostic_dump_post_reverse_plane(
            content_generation, surface_generation, version,
            diagnostic_reverse_snapshot);
        wsprintfA(
            line,
            "R160 post-reverse cgen=%lu sgen=%lu sample=%lu version=%lu obj=%08lX backing=%08lX hash=%08lX copied=%lu guards=%lu/%lu\r\n",
            (DWORD)content_generation, (DWORD)surface_generation,
            (DWORD)slot->sample_sequence, (DWORD)version,
            (DWORD)(uintptr_t)object, (DWORD)backing, (DWORD)hash,
            (DWORD)copy_stats.copied_bytes,
            (DWORD)copy_stats.guard_regions,
            (DWORD)copy_stats.restored_regions);
        diagnostic_write(line);
    } while (0);

    InterlockedExchange(&diagnostic_reverse_capture_busy, 0);
}

static void diagnostic_begin_content_epoch(uint32_t generation) {
    char line[128];
    if (!diagnostic_trace_armed()) return;
    InterlockedExchange(&diagnostic_surface_lines, 0);
    InterlockedExchange(&diagnostic_publication_lines, 0);
    InterlockedExchange(&diagnostic_reverse_lines, 0);
    InterlockedExchange(&diagnostic_unmatched_lines, 0);
    InterlockedExchange(&diagnostic_shadow_lines, 0);
    InterlockedExchange(&diagnostic_shadow_hash_lines, 0);
    InterlockedExchange(&diagnostic_wrapped_shadow_lines, 0);
    InterlockedExchange(&diagnostic_mapper_lines, 0);
    InterlockedExchange(&mapper_shader_draw_identity_events, 0);
    InterlockedExchange(&diagnostic_epoch_generation, (LONG)generation);
    wsprintfA(line, "diagnostic epoch r155 cgen=%lu\r\n", (DWORD)generation);
    diagnostic_write(line);
}

static void diagnostic_surface_event(const char *kind,
                                     uintptr_t object,
                                     uintptr_t backing,
                                     uint32_t generation,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t pitch,
                                     uint32_t bytes_per_unit) {
    char line[256];
    LONG event_number;
    if (!diagnostic_trace_armed() ||
        InterlockedIncrement(&diagnostic_surface_lines) > 512) return;
    event_number = InterlockedIncrement(&diagnostic_event_number);
    wsprintfA(line,
              "R94 e=%ld tid=%lu %s obj=%08lX backing=%08lX gen=%lu w=%lu h=%lu pitch=%lu bpu=%lu\r\n",
              event_number, GetCurrentThreadId(), kind,
              (DWORD)object, (DWORD)backing, (DWORD)generation,
              (DWORD)width, (DWORD)height, (DWORD)pitch,
              (DWORD)bytes_per_unit);
    diagnostic_write(line);
}

static void diagnostic_publication_event(uintptr_t backing,
                                         uintptr_t raw_argument_1,
                                         uintptr_t raw_argument_2,
                                         uint32_t generation,
                                         uint32_t full_before,
                                         uint32_t full_after,
                                         const focus_span_rect *rect,
                                         int expanded) {
    char line[320];
    LONG event_number;
    if (!diagnostic_trace_armed() || rect == NULL) return;
    if (generation == 0u) {
        if (InterlockedIncrement(&diagnostic_unmatched_lines) > 16) return;
    }
    if (InterlockedIncrement(&diagnostic_publication_lines) > 2048) return;
    event_number = InterlockedIncrement(&diagnostic_event_number);
    wsprintfA(line,
              "R95 e=%ld tid=%lu fallback a1=%08lX a2=%08lX backing=%08lX gen=%lu full=%lu->%lu rect=%ld,%ld,%ld,%ld expand=%d\r\n",
              event_number, GetCurrentThreadId(), (DWORD)raw_argument_1,
              (DWORD)raw_argument_2, (DWORD)backing,
              (DWORD)generation, (DWORD)full_before, (DWORD)full_after,
              (LONG)rect->left, (LONG)rect->top, (LONG)rect->right,
              (LONG)rect->bottom, expanded);
    diagnostic_write(line);
}

static void diagnostic_shadow_event(uintptr_t backing,
                                    uint32_t content_generation,
                                    uint32_t surface_generation,
                                    int slot,
                                    const focus_plane_shadow_plan *plan,
                                    int page_seeded,
                                    int copied,
                                    int published) {
    char line[320];
    LONG event_number;
    if (!diagnostic_trace_armed() || plan == NULL ||
        InterlockedIncrement(&diagnostic_shadow_lines) > 512) return;
    event_number = InterlockedIncrement(&diagnostic_event_number);
    wsprintfA(line,
              "R103 e=%ld tid=%lu shadow backing=%08lX cgen=%lu sgen=%lu slot=%d base=%lu carry=%lu seed=%lu page_seed=%d offset=%lu length=%lu copied=%d publish=%d\r\n",
              event_number, GetCurrentThreadId(), (DWORD)backing,
              (DWORD)content_generation, (DWORD)surface_generation, slot,
              (DWORD)plan->had_valid_base,
              (DWORD)plan->carried_from_prior_content,
              (DWORD)plan->seed_full, page_seeded, (DWORD)plan->offset,
              (DWORD)plan->length, copied, published);
    diagnostic_write(line);
}

static void diagnostic_shadow_hash_event(
    uintptr_t backing,
    uint32_t content_generation,
    uint32_t surface_generation,
    int slot,
    const focus_plane_shadow_plan *plan,
    uint32_t page_hash,
    uint32_t shadow_hash_before,
    uint32_t shadow_hash_after,
    uint32_t region_hash,
    int copied,
    int published) {
    char line[384];
    LONG event_number;
    if (!diagnostic_trace_armed() || plan == NULL) return;
    event_number = InterlockedIncrement(&diagnostic_event_number);
    wsprintfA(
        line,
        "R155 shadow-hash e=%ld tid=%lu backing=%08lX cgen=%lu sgen=%lu slot=%d base=%lu seed=%lu offset=%lu length=%lu page=%08lX pre=%08lX post=%08lX region=%08lX copied=%d publish=%d\r\n",
        event_number, GetCurrentThreadId(), (DWORD)backing,
        (DWORD)content_generation, (DWORD)surface_generation, slot,
        (DWORD)plan->had_valid_base, (DWORD)plan->seed_full,
        (DWORD)plan->offset, (DWORD)plan->length, (DWORD)page_hash,
        (DWORD)shadow_hash_before, (DWORD)shadow_hash_after,
        (DWORD)region_hash, copied, published);
    diagnostic_write(line);
}

static void diagnostic_mapper_draw_event(
    ID3D11DeviceContext *context,
    ID3D11PixelShader *bound,
    ID3D11PixelShader *tracked,
    int applied) {
    uint32_t content_generation;
    LONG line_number;
    LONG event_number;
    char line[256];
    if (!diagnostic_trace_armed()) return;
    line_number = InterlockedIncrement(&diagnostic_mapper_lines);
    if (line_number > 1024) return;
    EnterCriticalSection(&focus_span_lock);
    content_generation = focus_span_content_generation(&focus_span);
    LeaveCriticalSection(&focus_span_lock);
    event_number = InterlockedIncrement(&diagnostic_event_number);
    wsprintfA(
        line,
        "R155 mapper-draw e=%ld tid=%lu cgen=%lu context=%08lX bound=%08lX tracked=%08lX equal=%d applied=%d\r\n",
        event_number, GetCurrentThreadId(), (DWORD)content_generation,
        (DWORD)(uintptr_t)context, (DWORD)(uintptr_t)bound,
        (DWORD)(uintptr_t)tracked, bound == tracked, applied);
    diagnostic_write(line);
}

static void diagnostic_reverse_event(uintptr_t object,
                                     uintptr_t backing,
                                     uint32_t generation,
                                     int acknowledged) {
    char line[192];
    LONG event_number;
    if (!diagnostic_trace_armed()) return;
    if (generation == 0u) {
        if (InterlockedIncrement(&diagnostic_unmatched_lines) > 16) return;
    }
    if (InterlockedIncrement(&diagnostic_reverse_lines) > 512) return;
    event_number = InterlockedIncrement(&diagnostic_event_number);
    wsprintfA(line,
              "R94 e=%ld tid=%lu reverse obj=%08lX backing=%08lX gen=%lu ack=%d\r\n",
              event_number, GetCurrentThreadId(), (DWORD)object,
              (DWORD)backing, (DWORD)generation, acknowledged);
    diagnostic_write(line);
}

static void *allocate_loading_shadow(size_t size, void *context) {
    (void)context;
    return VirtualAlloc(
        NULL, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void free_loading_shadow(void *memory, void *context) {
    (void)context;
    if (memory != NULL) VirtualFree(memory, 0u, MEM_RELEASE);
}

static int begin_loading_shadow_operation(void) {
    if (InterlockedCompareExchange(
            &loading_shadow_operation_active, 1, 0) != 0) {
        return 0;
    }
    EnterCriticalSection(&loading_shadow_lock);
    return 1;
}

static void end_loading_shadow_operation(void) {
    LeaveCriticalSection(&loading_shadow_lock);
    InterlockedExchange(&loading_shadow_operation_active, 0);
}

static int current_loading_surface(loading_shadow_surface *surface) {
    loading_shadow_surface first;
    loading_shadow_surface second;
    size_t size;

    if (game_base == 0u || surface == NULL) return 0;
    first.width =
        *(volatile unsigned long *)(game_base + MTW_BACKING_WIDTH_RVA);
    first.height =
        *(volatile unsigned long *)(game_base + MTW_BACKING_HEIGHT_RVA);
    first.pitch =
        *(volatile unsigned long *)(game_base + MTW_BACKING_PITCH_RVA);
    first.bits = *(void *volatile *)(game_base + MTW_BACKING_BITS_RVA);
    MemoryBarrier();
    second.width =
        *(volatile unsigned long *)(game_base + MTW_BACKING_WIDTH_RVA);
    second.height =
        *(volatile unsigned long *)(game_base + MTW_BACKING_HEIGHT_RVA);
    second.pitch =
        *(volatile unsigned long *)(game_base + MTW_BACKING_PITCH_RVA);
    second.bits = *(void *volatile *)(game_base + MTW_BACKING_BITS_RVA);
    if (first.width != second.width || first.height != second.height ||
        first.pitch != second.pitch || first.bits != second.bits) {
        return 0;
    }
    first.bytes_per_pixel = MTW_LOADING_BYTES_PER_PIXEL;
    first.epoch = 1u;
    if (!loading_shadow_surface_size(&first, &size)) return 0;
    *surface = first;
    return 1;
}

static void disarm_mapper_after_loading(
    const loading_shadow_surface *surface) {
    mtw_mapper_activation_state current;
    mtw_mapper_activation_state next;
    LONG previous_mode;
    LONG previous_armed;
    LONG event_number;

    AcquireSRWLockExclusive(&mapper_activation_lock);
    previous_mode = InterlockedCompareExchange(
        &mapper_frontend_mode, 0, 0);
    previous_armed = InterlockedCompareExchange(
        &mapper_frontend_rearm_armed, 0, 0);
    current.mapper_mode = previous_mode != 0;
    current.frontend_rearm_armed = previous_armed != 0;
    next = mtw_mapper_activation_after_loading(
        current, (uint32_t)surface->width,
        (uint32_t)surface->height, (uint32_t)surface->pitch);
    InterlockedExchange(
        &mapper_frontend_owner,
        (LONG)MTW_MAPPER_FRONTEND_OWNER_UNKNOWN);
    mapper_frontend_owner_root = 0u;
    mapper_frontend_owner_poll_tick = 0u;
    InterlockedExchange(
        &mapper_frontend_mode, (LONG)next.mapper_mode);
    InterlockedExchange(
        &mapper_frontend_rearm_armed,
        (LONG)next.frontend_rearm_armed);
    ReleaseSRWLockExclusive(&mapper_activation_lock);

    if (previous_mode != (LONG)next.mapper_mode ||
        previous_armed != (LONG)next.frontend_rearm_armed) {
        event_number = InterlockedIncrement(
            &mapper_frontend_disarm_events);
        if (event_number <= 8) {
            diagnostic_mapper_transition(
                "transition", "verified-loading",
                game_base + MTW_INITIAL_COPY_PRE_UNLOCK_RVA, 0u,
                (uintptr_t)surface->bits, (uint32_t)surface->width,
                (uint32_t)surface->height, (uint32_t)surface->pitch,
                (uint32_t)surface->bytes_per_pixel,
                0, 0,
                previous_mode, (LONG)next.mapper_mode);
        }
        if (event_number == 1) {
            diagnostic_write(
                "frontend mapper fast bypass active r184\r\n");
        }
    }
}

__declspec(noinline) static void capture_loading_plane(void) {
    loading_shadow_surface surface;

    /*
     * The first loading copy can run before either the message pump or the
     * next D3D draw observes a frontend-to-game mode change.  Synchronize
     * here so a stale menu mapper cannot transform the plane we are about to
     * preserve, even when frontend and gameplay are both 800x600.
     */
    (void)synchronize_mapper_with_game_mode();
    if (!begin_loading_shadow_operation()) return;
    if (!current_loading_surface(&surface)) {
        loading_shadow_invalidate(&loading_shadow);
        end_loading_shadow_operation();
        return;
    }
    ++loading_shadow_epoch;
    if (loading_shadow_epoch == 0u) ++loading_shadow_epoch;
    surface.epoch = loading_shadow_epoch;
    if (loading_shadow_capture(&loading_shadow, &surface)) {
        disarm_mapper_after_loading(&surface);
        diagnostic_write("capture\r\n");
    }
    end_loading_shadow_operation();
}

__declspec(noinline) static void restore_loading_plane_after_lock(void) {
    loading_shadow_surface surface;

    if (!begin_loading_shadow_operation()) return;
    if (!current_loading_surface(&surface)) {
        loading_shadow_invalidate(&loading_shadow);
        end_loading_shadow_operation();
        return;
    }
    surface.epoch = loading_shadow_epoch;
    if (loading_shadow_restore_after_lock(&loading_shadow, &surface)) {
        diagnostic_write("restore\r\n");
    }
    end_loading_shadow_operation();
}

__declspec(noinline) static void commit_loading_plane_before_unlock(void) {
    loading_shadow_surface surface;

    if (!begin_loading_shadow_operation()) return;
    if (!current_loading_surface(&surface)) {
        loading_shadow_invalidate(&loading_shadow);
        end_loading_shadow_operation();
        return;
    }
    surface.epoch = loading_shadow_epoch;
    if (loading_shadow_commit_before_unlock(&loading_shadow, &surface)) {
        diagnostic_write("commit\r\n");
    }
    end_loading_shadow_operation();
}

static int arm_constructor_return(uintptr_t *entry_stack, void *object) {
    DWORD thread_id;
    uint32_t index;
    constructor_return_thread *slot = NULL;
    if (entry_stack == NULL || entry_stack[0] == 0u || object == NULL) return 0;
    thread_id = GetCurrentThreadId();
    EnterCriticalSection(&constructor_return_lock);
    for (index = 0u; index < MTW_CONSTRUCTOR_THREAD_CAPACITY; ++index) {
        if (constructor_returns[index].thread_id == thread_id) {
            slot = &constructor_returns[index];
            break;
        }
        if (slot == NULL && constructor_returns[index].thread_id == 0u) {
            slot = &constructor_returns[index];
        }
    }
    if (slot != NULL && slot->thread_id == 0u) slot->thread_id = thread_id;
    if (slot == NULL || slot->depth >= MTW_CONSTRUCTOR_RETURN_DEPTH) {
        LeaveCriticalSection(&constructor_return_lock);
        return 0;
    }
    slot->return_addresses[slot->depth] = entry_stack[0];
    slot->objects[slot->depth] = (uintptr_t)object;
    ++slot->depth;
    entry_stack[0] = (uintptr_t)constructor_return_hook_stub;
    LeaveCriticalSection(&constructor_return_lock);
    return 1;
}

static uintptr_t complete_constructor_return(void) {
    DWORD thread_id = GetCurrentThreadId();
    uint32_t index;
    uintptr_t return_address = 0u;
    uintptr_t object = 0u;
    unsigned char fields[0x24];
    SIZE_T transferred = 0u;

    EnterCriticalSection(&constructor_return_lock);
    for (index = 0u; index < MTW_CONSTRUCTOR_THREAD_CAPACITY; ++index) {
        constructor_return_thread *slot = &constructor_returns[index];
        if (slot->thread_id != thread_id || slot->depth == 0u) continue;
        --slot->depth;
        return_address = slot->return_addresses[slot->depth];
        object = slot->objects[slot->depth];
        slot->return_addresses[slot->depth] = 0u;
        slot->objects[slot->depth] = 0u;
        if (slot->depth == 0u) slot->thread_id = 0u;
        break;
    }
    LeaveCriticalSection(&constructor_return_lock);

    if (return_address == 0u) return 0u;
    if (object != 0u &&
        ReadProcessMemory(GetCurrentProcess(), (const void *)object,
                          fields, sizeof(fields), &transferred) &&
        transferred == sizeof(fields)) {
        int tracked;
        uint32_t generation;
        EnterCriticalSection(&focus_span_lock);
        tracked = focus_span_complete_surface(
            &focus_span,
            *(const uint16_t *)&fields[0x08],
            *(const uint16_t *)&fields[0x0A],
            *(const uint32_t *)&fields[0x10],
            fields[0x1E],
            object,
            *(const uintptr_t *)&fields[0x20]);
        generation = focus_span_generation_for_object(&focus_span, object);
        LeaveCriticalSection(&focus_span_lock);
        if (tracked) {
            diagnostic_surface_event(
                "complete", object, *(const uintptr_t *)&fields[0x20],
                generation, *(const uint16_t *)&fields[0x08],
                *(const uint16_t *)&fields[0x0A],
                *(const uint32_t *)&fields[0x10], fields[0x1E]);
        }
    }
    return return_address;
}

__declspec(noinline) static void observe_fast_surface(uintptr_t *entry_stack,
                                                      void *object) {
    unsigned char fields[0x24];
    SIZE_T transferred = 0u;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t bytes_per_unit;
    uintptr_t backing;
    int confirmed_frontend_lifecycle;

    if (object == NULL) return;
    if (!ReadProcessMemory(GetCurrentProcess(), object, fields, sizeof(fields),
                           &transferred) ||
        transferred != sizeof(fields)) {
        (void)arm_constructor_return(entry_stack, object);
        return;
    }

    width = *(const uint16_t *)&fields[0x08];
    height = *(const uint16_t *)&fields[0x0A];
    pitch = *(const uint32_t *)&fields[0x10];
    bytes_per_unit = fields[0x1E];
    backing = *(const uintptr_t *)&fields[0x20];

    EnterCriticalSection(&focus_span_lock);
    confirmed_frontend_lifecycle = focus_span_observe_surface(
        &focus_span, width, height, pitch, bytes_per_unit,
        (uintptr_t)object, backing);
    LeaveCriticalSection(&focus_span_lock);

    (void)synchronize_mapper_with_game_mode();
    (void)observe_mapper_surface(
        width, height, pitch, bytes_per_unit);

    if ((width == 640u && height == 480u) ||
        (width == 800u && height == 600u)) {
        diagnostic_surface_event(
            confirmed_frontend_lifecycle ? "pre-slow" : "pre",
            (uintptr_t)object, backing, 0u, width, height, pitch,
            bytes_per_unit);
    }

    if (confirmed_frontend_lifecycle && entry_stack != NULL &&
        entry_stack[4] == 1u) {
        entry_stack[4] = 0u;
        if (InterlockedIncrement(&startup_slow_lane_mutations) == 1) {
            diagnostic_write("startup slow lane applied r6f134\r\n");
        }
    }
    (void)arm_constructor_return(entry_stack, object);
}

__declspec(noinline) static void maybe_expand_focus_span(uintptr_t *entry_stack) {
    focus_span_rect original;
    focus_plane_shadow_plan shadow_plan;
    guarded_memory_copy_stats copy_stats;
    uintptr_t return_address;
    uintptr_t backing;
    uintptr_t rect_pointer;
    uint32_t source_pitch;
    SIZE_T transferred = 0u;
    int expand = 0;
    int shadow_ready = 0;
    int shadow_planned = 0;
    int shadow_publish = 0;
    int page_seeded = 0;
    int backing_seeded = 0;
    int shadow_slot = -1;
    unsigned char *shadow_bytes = NULL;
    uint32_t index;
    uint32_t generation = 0u;
    uint32_t content_generation = 0u;
    uint32_t full_before = 0u;
    uint32_t full_after = 0u;
    uint32_t page_hash = 0u;
    uint32_t shadow_hash_before = 0u;
    uint32_t shadow_hash_after = 0u;
    uint32_t region_hash = 0u;
    uint32_t diagnostic_backing_sample_sequence = 0u;
    int shadow_hash_armed = 0;

    if (entry_stack == NULL || backend_base == 0u) return;
    return_address = entry_stack[0];
    backing = entry_stack[3];
    source_pitch = (uint32_t)entry_stack[4];
    rect_pointer = entry_stack[5];
    if (return_address < backend_base || rect_pointer == 0u ||
        return_address - backend_base != 0x0007C335u ||
        source_pitch != 1600u) {
        return;
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)rect_pointer,
                           &original, sizeof(original), &transferred) ||
        transferred != sizeof(original)) {
        return;
    }

    EnterCriticalSection(&focus_span_lock);
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        uintptr_t object = focus_span.generations[index].object;
        uintptr_t current_backing = 0u;
        if (object == 0u) continue;
        transferred = 0u;
        if (ReadProcessMemory(GetCurrentProcess(),
                              (const void *)(object + 0x20u),
                              &current_backing, sizeof(current_backing),
                              &transferred) &&
            transferred == sizeof(current_backing) && current_backing != 0u) {
            (void)focus_span_refresh_backing(
                &focus_span, object, current_backing);
        } else {
            (void)focus_span_revoke_object(&focus_span, object);
        }
    }
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
        if (focus_span.generations[index].backing == backing) {
            generation = focus_span.generations[index].generation;
            full_before =
                focus_span.generations[index].full_publication_version;
            break;
        }
    }
    expand = focus_span_observe_publication(
        &focus_span, backing, source_pitch,
        return_address - backend_base, &original);
    content_generation = focus_span_content_generation(&focus_span);
    diagnostic_capture_first_fallback_planes(content_generation, backing);
    diagnostic_backing_sample_sequence = diagnostic_sample_backing_evolution(
        content_generation, generation, backing, &original);
    if (generation != 0u) {
        for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
            if (focus_span.generations[index].generation == generation) {
                full_after =
                    focus_span.generations[index].full_publication_version;
                break;
            }
        }
    }
    if (generation != 0u) {
        shadow_slot = focus_plane_shadow_select(
            &focus_plane_shadow, content_generation, generation, backing);
        if (shadow_slot >= 0 &&
            !(frontend_page_snapshot_valid &&
              frontend_page_snapshot_generation == content_generation) &&
            focus_plane_shadow_can_seed_generation(
                &focus_plane_shadow, shadow_slot, content_generation,
                source_pitch, 2u, MTW_FOCUS_PLANE_BYTES)) {
            memset(&copy_stats, 0, sizeof(copy_stats));
            backing_seeded = guarded_memory_copy(
                frontend_page_snapshot, (const void *)backing,
                MTW_FOCUS_PLANE_BYTES, &copy_stats) &&
                copy_stats.copied_bytes == MTW_FOCUS_PLANE_BYTES &&
                copy_stats.guard_regions == copy_stats.restored_regions;
            if (backing_seeded) {
                frontend_page_snapshot_generation = content_generation;
                frontend_page_snapshot_valid = 1;
                diagnostic_dump_page_snapshot(
                    content_generation, frontend_page_snapshot);
                diagnostic_write(
                    "qualified fallback generation seed active r158\r\n");
            }
        }
        if (shadow_slot >= 0 && frontend_page_snapshot_valid &&
            frontend_page_snapshot_generation == content_generation &&
            focus_plane_shadow_needs_content_seed(
                &focus_plane_shadow, shadow_slot, content_generation)) {
            page_seeded = focus_plane_shadow_seed_content(
                &focus_plane_shadow, shadow_slot, content_generation,
                frontend_page_snapshot, MTW_FOCUS_PLANE_BYTES);
        }
        if (shadow_slot >= 0 &&
            (original.right < original.left ||
             original.bottom < original.top) &&
            diagnostic_trace_armed() &&
            InterlockedIncrement(&diagnostic_wrapped_shadow_lines) <= 64) {
            diagnostic_write("wrapped row-major span observed r181\r\n");
        }
        if (shadow_slot >= 0 && focus_plane_shadow_make_plan(
                &focus_plane_shadow, shadow_slot, source_pitch, 2u,
                &original, &shadow_plan)) {
            shadow_planned = 1;
            shadow_bytes = focus_plane_shadow_bytes(
                &focus_plane_shadow, &shadow_plan);
            if (shadow_bytes != NULL && shadow_plan.had_valid_base &&
                !shadow_plan.seed_full && diagnostic_trace_armed() &&
                InterlockedIncrement(&diagnostic_shadow_hash_lines) <= 64) {
                shadow_hash_armed = 1;
                shadow_hash_before = diagnostic_fnv1a32(
                    shadow_bytes, MTW_FOCUS_PLANE_BYTES);
                if (frontend_page_snapshot_valid &&
                    frontend_page_snapshot_generation == content_generation) {
                    page_hash = diagnostic_fnv1a32(
                        frontend_page_snapshot, MTW_FOCUS_PLANE_BYTES);
                }
            }
            memset(&copy_stats, 0, sizeof(copy_stats));
            shadow_ready = shadow_bytes != NULL && guarded_memory_copy(
                shadow_bytes + shadow_plan.offset,
                (const void *)(backing + shadow_plan.offset),
                shadow_plan.length, &copy_stats) &&
                copy_stats.copied_bytes == shadow_plan.length &&
                copy_stats.guard_regions == copy_stats.restored_regions;
            if (shadow_ready) {
                shadow_publish = !shadow_plan.seed_full &&
                                 shadow_plan.had_valid_base;
                if (shadow_hash_armed) {
                    shadow_hash_after = diagnostic_fnv1a32(
                        shadow_bytes, MTW_FOCUS_PLANE_BYTES);
                    region_hash = diagnostic_fnv1a32(
                        shadow_bytes + shadow_plan.offset,
                        shadow_plan.length);
                }
                focus_plane_shadow_commit(
                    &focus_plane_shadow, &shadow_plan);
                diagnostic_sample_shadow_evolution(
                    content_generation, generation, backing, &original,
                    diagnostic_backing_sample_sequence, shadow_bytes,
                    shadow_publish);
                if (shadow_publish) {
                    (void)focus_span_observe_publication(
                        &focus_span, backing, source_pitch,
                        return_address - backend_base, &focus_span_full);
                    for (index = 0u;
                         index < MTW_FOCUS_AUTHORITY_CAPACITY; ++index) {
                        if (focus_span.generations[index].generation ==
                            generation) {
                            full_after = focus_span.generations[index]
                                             .full_publication_version;
                            break;
                        }
                    }
                }
            }
        }
    }
    LeaveCriticalSection(&focus_span_lock);

    diagnostic_publication_event(
        backing, entry_stack[1], entry_stack[2], generation,
        full_before, full_after, &original, expand);
    if (shadow_planned) {
        diagnostic_shadow_event(
            backing, content_generation, generation, shadow_slot,
            &shadow_plan, page_seeded, shadow_ready, shadow_publish);
        if (shadow_hash_armed) {
            diagnostic_shadow_hash_event(
                backing, content_generation, generation, shadow_slot,
                &shadow_plan, page_hash, shadow_hash_before,
                shadow_hash_after, region_hash, shadow_ready,
                shadow_publish);
        }
    }

    if (!shadow_publish || !shadow_ready || shadow_bytes == NULL) return;
    entry_stack[3] = (uintptr_t)shadow_bytes;
    entry_stack[5] = (uintptr_t)&focus_span_full;
    if (InterlockedIncrement(&focus_span_substitutions) == 1) {
        diagnostic_write("post-dispatch page seed active r103\r\n");
    }
}

__declspec(noinline) static int should_ack_selected_reverse(void *object) {
    int acknowledge;
    uintptr_t current_backing = 0u;
    uint32_t generation = 0u;
    SIZE_T transferred = 0u;
    EnterCriticalSection(&focus_span_lock);
    if (object != NULL &&
        ReadProcessMemory(GetCurrentProcess(),
                          (const unsigned char *)object + 0x20u,
                          &current_backing, sizeof(current_backing),
                          &transferred) &&
        transferred == sizeof(current_backing) && current_backing != 0u) {
        (void)focus_span_refresh_backing(
            &focus_span, (uintptr_t)object, current_backing);
    } else if (object != NULL) {
        (void)focus_span_revoke_object(&focus_span, (uintptr_t)object);
    }
    acknowledge =
        focus_span_should_ack_reverse(&focus_span, (uintptr_t)object);
    generation = focus_span_generation_for_object(
        &focus_span, (uintptr_t)object);
    LeaveCriticalSection(&focus_span_lock);
    diagnostic_reverse_event(
        (uintptr_t)object, current_backing, generation, acknowledge);
    if (acknowledge && reverse_authority_seen == 0 &&
        InterlockedCompareExchange(&reverse_authority_seen, 1, 0) == 0) {
        diagnostic_write("generation reverse authority hit r93\r\n");
    }
    return acknowledge;
}

__declspec(noinline) static void
complete_selected_reverse_handoff(void *object) {
    guarded_memory_copy_stats copy_stats;
    uintptr_t backing = 0u;
    uintptr_t verified_backing = 0u;
    uint32_t content_generation = 0u;
    uint32_t surface_generation = 0u;
    SIZE_T transferred = 0u;
    int shadow_slot = -1;
    int copied = 0;
    int shadow_seeded = 0;
    int authority_acquired = 0;
    LONG event_number;
    char line[384];

    if (object == NULL) return;
    if (!ReadProcessMemory(
            GetCurrentProcess(), (const unsigned char *)object + 0x20u,
            &backing, sizeof(backing), &transferred) ||
        transferred != sizeof(backing) || backing == 0u) {
        return;
    }

    EnterCriticalSection(&focus_span_lock);
    (void)focus_span_refresh_backing(
        &focus_span, (uintptr_t)object, backing);
    surface_generation = focus_span_generation_for_object(
        &focus_span, (uintptr_t)object);
    content_generation = focus_span_content_generation(&focus_span);

    /* The original dispatcher has completed a GPU-to-CPU reverse copy. Copy
       that complete physical plane into both R158 authorities before allowing
       later reverse acknowledgements. This replaces any stale full fallback
       observed immediately before the dormant-generation handoff. */
    if (frontend_page_snapshot != NULL && surface_generation != 0u &&
        content_generation != 0u) {
        memset(&copy_stats, 0, sizeof(copy_stats));
        copied = guarded_memory_copy(
            frontend_page_snapshot, (const void *)backing,
            MTW_FOCUS_PLANE_BYTES, &copy_stats) &&
            copy_stats.copied_bytes == MTW_FOCUS_PLANE_BYTES &&
            copy_stats.guard_regions == copy_stats.restored_regions;
        transferred = 0u;
        if (copied &&
            ReadProcessMemory(
                GetCurrentProcess(), (const unsigned char *)object + 0x20u,
                &verified_backing, sizeof(verified_backing), &transferred) &&
            transferred == sizeof(verified_backing) &&
            verified_backing == backing &&
            focus_span_generation_for_object(
                &focus_span, (uintptr_t)object) == surface_generation &&
            focus_span_content_generation(&focus_span) ==
                content_generation) {
            frontend_page_snapshot_generation = content_generation;
            frontend_page_snapshot_valid = 1;
            shadow_slot = focus_plane_shadow_select(
                &focus_plane_shadow, content_generation,
                surface_generation, backing);
            if (shadow_slot >= 0) {
                shadow_seeded = focus_plane_shadow_seed_content(
                    &focus_plane_shadow, shadow_slot, content_generation,
                    frontend_page_snapshot, MTW_FOCUS_PLANE_BYTES);
            }
            if (shadow_seeded) {
                authority_acquired = focus_span_note_reverse_applied(
                    &focus_span, (uintptr_t)object);
            }
        }
    }
    LeaveCriticalSection(&focus_span_lock);

    if (content_generation == 0u || surface_generation == 0u) return;
    event_number = InterlockedIncrement(&reverse_handoff_events);
    if (event_number <= 128) {
        wsprintfA(
            line,
            "R183 reverse handoff event=%ld cgen=%lu sgen=%lu obj=%08lX backing=%08lX copied=%d shadow=%d authority=%d\r\n",
            event_number, (DWORD)content_generation,
            (DWORD)surface_generation, (DWORD)(uintptr_t)object,
            (DWORD)backing, copied, shadow_seeded, authority_acquired);
        diagnostic_write(line);
    }
    if (authority_acquired && event_number == 1) {
        diagnostic_write(
            "active-generation reverse handoff active r183\r\n");
    }
}

static ID3D11PixelShader *mapper_acquire_exact_pixel_shader(
    ID3D11DeviceContext *context) {
    ID3D11Device *device = NULL;
    ID3D11PixelShader *result = NULL;

    if (context == NULL) return NULL;
    ID3D11DeviceContext_GetDevice(context, &device);
    if (device == NULL) return NULL;

    AcquireSRWLockShared(&mapper_shader_lock);
    if (mapper_shader_device == device && mapper_shader_clone != NULL) {
        ID3D11PixelShader_AddRef(mapper_shader_clone);
        result = mapper_shader_clone;
    }
    ReleaseSRWLockShared(&mapper_shader_lock);

    ID3D11Device_Release(device);
    return result;
}

static ID3D11SamplerState *mapper_acquire_exact_sampler(
    ID3D11DeviceContext *context) {
    ID3D11Device *device = NULL;
    ID3D11SamplerState *result = NULL;

    if (context == NULL) return NULL;
    ID3D11DeviceContext_GetDevice(context, &device);
    if (device == NULL) return NULL;

    AcquireSRWLockShared(&mapper_shader_lock);
    if (mapper_shader_device == device && mapper_linear_sampler != NULL) {
        ID3D11SamplerState_AddRef(mapper_linear_sampler);
        result = mapper_linear_sampler;
    }
    ReleaseSRWLockShared(&mapper_shader_lock);

    ID3D11Device_Release(device);
    return result;
}

static ID3D11PixelShader *mapper_acquire_tracked_original(void) {
    ID3D11PixelShader *result = NULL;
    AcquireSRWLockShared(&mapper_shader_lock);
    if (mapper_shader_original != NULL) {
        ID3D11PixelShader_AddRef(mapper_shader_original);
        result = mapper_shader_original;
    }
    ReleaseSRWLockShared(&mapper_shader_lock);
    return result;
}

static void mapper_log_draw_identity(ID3D11DeviceContext *context,
                                     ID3D11PixelShader *original,
                                     ID3D11PixelShader *tracked_original) {
    LONG event = InterlockedIncrement(&mapper_shader_draw_identity_events);
    ID3D11Device *context_device = NULL;
    char line[224];
    if (event > 64) return;
    if (context != NULL) {
        ID3D11DeviceContext_GetDevice(context, &context_device);
    }
    wsprintfA(line,
              "mapper shader draw identity r154 event=%ld context=%08lX device=%08lX trackedDevice=%08lX bound=%08lX tracked=%08lX equal=%d\r\n",
              event, (DWORD)(uintptr_t)context,
              (DWORD)(uintptr_t)context_device,
              (DWORD)(uintptr_t)mapper_tracking_device,
              (DWORD)(uintptr_t)original,
              (DWORD)(uintptr_t)tracked_original,
              original == tracked_original);
    diagnostic_write(line);
    if (context_device != NULL) ID3D11Device_Release(context_device);
}

__declspec(noinline) static void __stdcall mapper_draw_with_exact_clone(
    ID3D11DeviceContext *context, UINT vertex_count, UINT start_vertex) {
    ID3D11PixelShader *replacement;
    ID3D11PixelShader *original = NULL;
    ID3D11PixelShader *tracked_original;
    ID3D11SamplerState *replacement_sampler;
    ID3D11SamplerState *original_sampler = NULL;

    if (synchronize_mapper_with_game_mode() == 0) {
        ID3D11DeviceContext_Draw(context, vertex_count, start_vertex);
        return;
    }

    ID3D11DeviceContext_PSGetShader(context, &original, NULL, NULL);
    tracked_original = mapper_acquire_tracked_original();
    mapper_log_draw_identity(context, original, tracked_original);
    if (tracked_original == NULL || original != tracked_original) {
        diagnostic_mapper_draw_event(context, original, tracked_original, 0);
        if (tracked_original != NULL) {
            ID3D11PixelShader_Release(tracked_original);
        }
        if (original != NULL) ID3D11PixelShader_Release(original);
        ID3D11DeviceContext_Draw(context, vertex_count, start_vertex);
        return;
    }
    ID3D11PixelShader_Release(tracked_original);

    replacement = mapper_acquire_exact_pixel_shader(context);
    if (replacement == NULL) {
        diagnostic_mapper_draw_event(context, original, original, -1);
        if (original != NULL) ID3D11PixelShader_Release(original);
        ID3D11DeviceContext_Draw(context, vertex_count, start_vertex);
        return;
    }

    replacement_sampler = mapper_acquire_exact_sampler(context);
    if (replacement_sampler == NULL) {
        diagnostic_mapper_draw_event(context, original, original, -2);
        if (original != NULL) ID3D11PixelShader_Release(original);
        ID3D11PixelShader_Release(replacement);
        ID3D11DeviceContext_Draw(context, vertex_count, start_vertex);
        return;
    }

    ID3D11DeviceContext_PSGetSamplers(context, 0u, 1u, &original_sampler);
    ID3D11DeviceContext_PSSetShader(context, replacement, NULL, 0u);
    ID3D11DeviceContext_PSSetSamplers(context, 0u, 1u, &replacement_sampler);
    ID3D11DeviceContext_Draw(context, vertex_count, start_vertex);
    ID3D11DeviceContext_PSSetSamplers(context, 0u, 1u, &original_sampler);
    ID3D11DeviceContext_PSSetShader(context, original, NULL, 0u);
    if (original_sampler != NULL) ID3D11SamplerState_Release(original_sampler);
    if (original != NULL) ID3D11PixelShader_Release(original);
    ID3D11SamplerState_Release(replacement_sampler);
    ID3D11PixelShader_Release(replacement);
    diagnostic_mapper_draw_event(context, original, original, 1);

    if (InterlockedCompareExchange(&mapper_shader_active_logged, 1, 0) == 0) {
        diagnostic_write("mapper shader targeted clone active r154\r\n");
    }
    if (InterlockedCompareExchange(&mapper_sampler_active_logged, 1, 0) == 0) {
        diagnostic_write("mapper shader linear sampler active r154\r\n");
    }
}

__declspec(naked) static void mapper_draw_hook_stub(void) {
    __asm {
        push dword ptr [esp + 4]
        push dword ptr [esp + 4]
        push eax
        call mapper_draw_with_exact_clone
        add esp, 8
        jmp dword ptr [mapper_draw_continue]
    }
}

__declspec(naked) static void prebattle_entry_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call mapper_begin_prebattle_entry
        popad
        popfd
        jmp dword ptr [prebattle_entry_original_target]
    }
}

__declspec(naked) static void prebattle_resolution_return_hook_stub(void) {
    __asm {
        add esp, 0x10
        inc eax
        mov dword ptr [esp + 0x84], eax
        pushfd
        pushad
        call mapper_finish_prebattle_resolution
        popad
        popfd
        jmp dword ptr [prebattle_resolution_return_continue]
    }
}

__declspec(naked) static void capture_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call capture_loading_plane
        popad
        popfd
        mov eax, dword ptr [0x008DCE48]
        jmp dword ptr [capture_continue]
    }
}

__declspec(naked) static void restore_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call restore_loading_plane_after_lock
        popad
        popfd
        mov edx, dword ptr [0x008D0244]
        jmp dword ptr [restore_continue]
    }
}

__declspec(naked) static void commit_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call commit_loading_plane_before_unlock
        popad
        popfd
        call dword ptr [game_unlock_target]
        jmp dword ptr [commit_continue]
    }
}

__declspec(naked) static void constructor_hook_stub(void) {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [esp + 24]
        push eax
        lea eax, dword ptr [esp + 40]
        push eax
        call observe_fast_surface
        add esp, 8
        popad
        popfd
        push ebp
        mov ebp, esp
        push ecx
        mov eax, dword ptr [constructor_global_target]
        mov eax, dword ptr [eax]
        jmp dword ptr [constructor_continue]
    }
}

__declspec(naked) static void constructor_return_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call complete_constructor_return
        mov dword ptr [esp + 20], eax
        popad
        popfd
        jmp edx
    }
}

__declspec(naked) static void fallback_callsite_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call synchronize_mapper_with_game_mode
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test eax, eax
        je call_original
        pushfd
        pushad
        lea eax, dword ptr [esp + 36]
        push eax
        call maybe_expand_focus_span
        add esp, 4
        popad
        popfd
    call_original:
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        call dword ptr [fallback_original_target]
        ret 0x14
    }
}

__declspec(naked) static void reverse_dispatcher_original_trampoline(void) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x20
        jmp dword ptr [reverse_dispatcher_continue]
    }
}

__declspec(naked) static void reverse_dispatcher_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call synchronize_mapper_with_game_mode
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test eax, eax
        jne inspect_frontend
        push ebp
        mov ebp, esp
        sub esp, 0x20
        jmp dword ptr [reverse_dispatcher_continue]
    inspect_frontend:
        pushfd
        pushad
        push ecx
        call should_ack_selected_reverse
        add esp, 4
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test eax, eax
        jnz acknowledge
        push ecx
        push dword ptr [esp + 8]
        call reverse_dispatcher_original_trampoline
        pushfd
        pushad
        push dword ptr [esp + 36]
        call complete_selected_reverse_handoff
        add esp, 4
        push dword ptr [esp + 36]
        call diagnostic_capture_post_reverse_generation
        add esp, 4
        popad
        popfd
        add esp, 4
        ret 4
    acknowledge:
        mov eax, 1
        ret 4
    }
}

__declspec(naked) static void primary_surface_unlock_hook_stub(void) {
    __asm {
        pushfd
        pushad
        call synchronize_mapper_with_game_mode
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test eax, eax
        jne inspect_frontend
        mov eax, dword ptr [0x00E6F5D0]
        jmp dword ptr [primary_surface_unlock_continue]
    inspect_frontend:
        mov eax, dword ptr [esp]
        pushfd
        pushad
        push eax
        call capture_preunlock_page_seed
        add esp, 4
        popad
        popfd
        mov eax, dword ptr [0x00E6F5D0]
        jmp dword ptr [primary_surface_unlock_continue]
    }
}

__declspec(noinline) static void record_reentrant_outer_owner(void *guard) {
    if (guard == NULL || *(volatile unsigned char *)guard != 0u) return;
    InterlockedExchange(&reentrant_outer_owner_tid,
                        (LONG)GetCurrentThreadId());
}

__declspec(noinline) static void clear_reentrant_outer_owner(void) {
    LONG thread_id = (LONG)GetCurrentThreadId();
    if (InterlockedCompareExchange(&reentrant_outer_owner_tid, 0, 0) !=
        thread_id) {
        return;
    }
    InterlockedExchange(&reentrant_outer_owner_tid, 0);
}

__declspec(noinline) static int should_divert_nested_guard_tracking(void) {
    uintptr_t global_object;
    uint32_t owner_thread_id;
    uint32_t current_thread_id = GetCurrentThreadId();
    uint32_t fast_owner_flag = 0u;

    if (backend_base == 0u) return 0;
    owner_thread_id = (uint32_t)InterlockedCompareExchange(
        &reentrant_outer_owner_tid, 0, 0);
    global_object = *(volatile uintptr_t *)(
        backend_base + DGVOODOO_GLOBAL_SLOT_RVA);
    if (global_object != 0u) {
        fast_owner_flag = *(volatile uint32_t *)(global_object + 0x1Cu);
    }
    return mtw_should_divert_outer_owned_guard_fault(
        owner_thread_id, current_thread_id, fast_owner_flag);
}

__declspec(naked) static void reentrant_outer_acquire_stub(void) {
    __asm {
        push dword ptr [esp + 4]
        call dword ptr [custom_acquire_target]
        pushfd
        pushad
        push eax
        call record_reentrant_outer_owner
        add esp, 4
        popad
        popfd
        ret 4
    }
}

__declspec(naked) static void reentrant_outer_release_stub(void) {
    __asm {
        call dword ptr [custom_release_target]
        pushfd
        pushad
        call clear_reentrant_outer_owner
        popad
        popfd
        ret
    }
}

__declspec(naked) static void reentrant_inner_acquire_stub(void) {
    __asm {
        pushfd
        pushad
        call should_divert_nested_guard_tracking
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test eax, eax
        jnz divert
        jmp dword ptr [nested_acquire_target]
    divert:
        add esp, 4
        jmp dword ptr [veh_handled_continue]
    }
}

static int bytes_match(uintptr_t address,
                       const unsigned char *expected,
                       size_t size) {
    return address != 0u && expected != NULL &&
           memcmp((const void *)address, expected, size) == 0;
}

static void build_constructor_expected(unsigned char expected[9]) {
    expected[0] = 0x55;
    expected[1] = 0x8B;
    expected[2] = 0xEC;
    expected[3] = 0x51;
    expected[4] = 0xA1;
    *(uint32_t *)&expected[5] =
        (uint32_t)(backend_base + DGVOODOO_CONSTRUCTOR_GLOBAL_RVA);
}

static int verify_all_hook_signatures(void) {
    unsigned char constructor_expected[9];
    mtw_reentrant_callsite_bundle reentrant_calls;
    if (game_base != 0x00400000u) return 0;
    if (backend_base == 0u) return 0;
    build_constructor_expected(constructor_expected);
    reentrant_calls.outer_acquire = (const unsigned char *)(
        backend_base + DGVOODOO_OUTER_ACQUIRE_CALL_RVA);
    reentrant_calls.outer_acquire_size = MTW_REENTRANT_CALL_SIZE;
    reentrant_calls.outer_release = (const unsigned char *)(
        backend_base + DGVOODOO_OUTER_RELEASE_CALL_RVA);
    reentrant_calls.outer_release_size = MTW_REENTRANT_CALL_SIZE;
    reentrant_calls.inner_acquire = (const unsigned char *)(
        backend_base + DGVOODOO_INNER_ACQUIRE_CALL_RVA);
    reentrant_calls.inner_acquire_size = MTW_REENTRANT_CALL_SIZE;
    reentrant_calls.inner_release_ok = (const unsigned char *)(
        backend_base + DGVOODOO_INNER_RELEASE_OK_CALL_RVA);
    reentrant_calls.inner_release_ok_size = MTW_REENTRANT_CALL_SIZE;
    reentrant_calls.inner_release_fail = (const unsigned char *)(
        backend_base + DGVOODOO_INNER_RELEASE_FAIL_CALL_RVA);
    reentrant_calls.inner_release_fail_size = MTW_REENTRANT_CALL_SIZE;
    return bytes_match(game_base + MTW_INITIAL_COPY_PRE_UNLOCK_RVA,
                       capture_expected, sizeof(capture_expected)) &&
           bytes_match(game_base + MTW_PROGRESS_POST_LOCK_RVA,
                       restore_expected, sizeof(restore_expected)) &&
           bytes_match(game_base + MTW_PROGRESS_PRE_UNLOCK_RVA,
                       commit_expected, sizeof(commit_expected)) &&
           bytes_match(backend_base + DGVOODOO_FAST_SURFACE_CONSTRUCTOR_RVA,
                       constructor_expected, sizeof(constructor_expected)) &&
           bytes_match(backend_base + DGVOODOO_UPLOAD_FALLBACK_RVA,
                       fallback_expected, sizeof(fallback_expected)) &&
           bytes_match(backend_base + DGVOODOO_UPLOAD_FALLBACK_CALL_RVA,
                       fallback_call_expected,
                       sizeof(fallback_call_expected)) &&
           bytes_match(backend_base + DGVOODOO_REVERSE_DISPATCHER_RVA,
                       reverse_dispatcher_expected,
                       sizeof(reverse_dispatcher_expected)) &&
           bytes_match(game_base + MTW_PRIMARY_SURFACE_UNLOCK_RVA,
                       primary_surface_unlock_expected,
                       sizeof(primary_surface_unlock_expected)) &&
           mtw_prebattle_entry_callsite_supported(
               (const unsigned char *)(
                   game_base + MTW_PREBATTLE_ENTRY_CALL_RVA),
               MTW_PREBATTLE_ENTRY_CALL_SIZE) &&
           mtw_prebattle_resolution_return_supported(
                (const unsigned char *)(
                    game_base + MTW_PREBATTLE_RESOLUTION_RETURN_RVA),
                MTW_PREBATTLE_RESOLUTION_RETURN_SIZE) &&
           mtw_validate_reentrant_callsites(&reentrant_calls);
}

static int write_rel_jump(uintptr_t target,
                          const void *hook,
                          const unsigned char *expected,
                          size_t size) {
    unsigned char patch[16];
    DWORD old_protect;
    DWORD ignored;
    intptr_t displacement;
    size_t index;

    if (size < 5u || size > sizeof(patch) ||
        !bytes_match(target, expected, size)) {
        return 0;
    }
    displacement = (const unsigned char *)hook -
                   ((const unsigned char *)target + 5u);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return 0;
    patch[0] = 0xE9;
    *(int32_t *)&patch[1] = (int32_t)displacement;
    for (index = 5u; index < size; ++index) patch[index] = 0x90;
    if (!VirtualProtect((void *)target, size, PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        return 0;
    }
    memcpy((void *)target, patch, size);
    FlushInstructionCache(GetCurrentProcess(), (void *)target, size);
    VirtualProtect((void *)target, size, old_protect, &ignored);
    return 1;
}

static int install_d3d11_create_device_hook(void) {
    uintptr_t target;
    if (backend_base == 0u) return 0;
    target = backend_base + DGVOODOO_D3D11_CREATE_DEVICE_CALLSITE_RVA;
    d3d11_create_success_continue = (void *)(target + 6u);
    d3d11_create_failure_continue = (void *)(target + 6u + 0x48u);
    return write_rel_jump(target, d3d11_create_device_hook_stub,
                          d3d11_create_device_call_expected,
                          sizeof(d3d11_create_device_call_expected));
}

static int install_mapper_shader_clone_hook(void) {
    uintptr_t target = backend_base + DGVOODOO_MAPPER_DRAW_CALLSITE_RVA;
    if (!mtw_mapper_callsite_supported(
            (const unsigned char *)target, MTW_MAPPER_CALLSITE_SIZE)) {
        return 0;
    }
    mapper_draw_continue = (void *)(target + MTW_MAPPER_CALLSITE_SIZE);
    return write_rel_jump(target, mapper_draw_hook_stub,
                          mapper_draw_expected,
                          MTW_MAPPER_CALLSITE_SIZE);
}

static int write_rel_call(uintptr_t target,
                          const void *hook,
                          const unsigned char expected[MTW_REENTRANT_CALL_SIZE]) {
    unsigned char patch[MTW_REENTRANT_CALL_SIZE];
    DWORD old_protect;
    DWORD ignored;
    intptr_t displacement;

    if (!bytes_match(target, expected, MTW_REENTRANT_CALL_SIZE)) return 0;
    displacement = (const unsigned char *)hook -
                   ((const unsigned char *)target + MTW_REENTRANT_CALL_SIZE);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return 0;
    patch[0] = 0xE8;
    *(int32_t *)&patch[1] = (int32_t)displacement;
    if (!VirtualProtect((void *)target, sizeof(patch), PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        return 0;
    }
    memcpy((void *)target, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), (void *)target, sizeof(patch));
    VirtualProtect((void *)target, sizeof(patch), old_protect, &ignored);
    return bytes_match(target, patch, sizeof(patch));
}

static void restore_original(uintptr_t target,
                             const unsigned char *original,
                             size_t size) {
    DWORD old_protect;
    DWORD ignored;
    if (!VirtualProtect((void *)target, size, PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        return;
    }
    memcpy((void *)target, original, size);
    FlushInstructionCache(GetCurrentProcess(), (void *)target, size);
    VirtualProtect((void *)target, size, old_protect, &ignored);
}

static int install_outer_owned_guard_diversion(void) {
    uintptr_t outer_acquire =
        backend_base + DGVOODOO_OUTER_ACQUIRE_CALL_RVA;
    uintptr_t outer_release =
        backend_base + DGVOODOO_OUTER_RELEASE_CALL_RVA;
    uintptr_t inner_acquire =
        backend_base + DGVOODOO_INNER_ACQUIRE_CALL_RVA;
    int outer_acquire_installed = 0;
    int outer_release_installed = 0;
    int inner_acquire_installed = 0;

    outer_acquire_installed = write_rel_call(
        outer_acquire, reentrant_outer_acquire_stub, outer_acquire_expected);
    if (!outer_acquire_installed) goto fail;
    outer_release_installed = write_rel_call(
        outer_release, reentrant_outer_release_stub, outer_release_expected);
    if (!outer_release_installed) goto fail;
    inner_acquire_installed = write_rel_call(
        inner_acquire, reentrant_inner_acquire_stub, inner_acquire_expected);
    if (!inner_acquire_installed) goto fail;
    return 1;

fail:
    if (inner_acquire_installed) {
        restore_original(inner_acquire, inner_acquire_expected,
                         sizeof(inner_acquire_expected));
    }
    if (outer_release_installed) {
        restore_original(outer_release, outer_release_expected,
                         sizeof(outer_release_expected));
    }
    if (outer_acquire_installed) {
        restore_original(outer_acquire, outer_acquire_expected,
                         sizeof(outer_acquire_expected));
    }
    return 0;
}

static int install_all_hooks_transactionally(void) {
    unsigned char constructor_expected[9];
    uintptr_t capture_target = game_base + MTW_INITIAL_COPY_PRE_UNLOCK_RVA;
    uintptr_t restore_target = game_base + MTW_PROGRESS_POST_LOCK_RVA;
    uintptr_t commit_target = game_base + MTW_PROGRESS_PRE_UNLOCK_RVA;
    uintptr_t constructor_target =
        backend_base + DGVOODOO_FAST_SURFACE_CONSTRUCTOR_RVA;
    uintptr_t fallback_target = backend_base + DGVOODOO_UPLOAD_FALLBACK_RVA;
    uintptr_t fallback_call_target =
        backend_base + DGVOODOO_UPLOAD_FALLBACK_CALL_RVA;
    uintptr_t reverse_dispatcher_target =
        backend_base + DGVOODOO_REVERSE_DISPATCHER_RVA;
    uintptr_t primary_surface_unlock_target =
        game_base + MTW_PRIMARY_SURFACE_UNLOCK_RVA;
    uintptr_t prebattle_entry_call_target =
        game_base + MTW_PREBATTLE_ENTRY_CALL_RVA;
    uintptr_t prebattle_resolution_return_target =
        game_base + MTW_PREBATTLE_RESOLUTION_RETURN_RVA;
    int capture_installed = 0;
    int restore_installed = 0;
    int commit_installed = 0;
    int constructor_installed = 0;
    int reverse_dispatcher_installed = 0;
    int primary_surface_unlock_installed = 0;
    int prebattle_entry_installed = 0;
    int prebattle_resolution_return_installed = 0;

    if (!verify_all_hook_signatures()) return 0;
    build_constructor_expected(constructor_expected);
    capture_continue = (void *)(capture_target + sizeof(capture_expected));
    restore_continue = (void *)(restore_target + sizeof(restore_expected));
    commit_continue = (void *)(commit_target + sizeof(commit_expected));
    game_unlock_target = (void *)(game_base + 0x000121B0u);
    constructor_continue =
        (void *)(constructor_target + sizeof(constructor_expected));
    fallback_original_target = (void *)fallback_target;
    reverse_dispatcher_continue = (void *)(
        reverse_dispatcher_target + sizeof(reverse_dispatcher_expected));
    primary_surface_unlock_continue = (void *)(
        primary_surface_unlock_target +
        sizeof(primary_surface_unlock_expected));
    prebattle_entry_original_target =
        (void *)(game_base + MTW_PREBATTLE_ENTRY_TARGET_RVA);
    prebattle_resolution_return_continue = (void *)(
        prebattle_resolution_return_target +
        sizeof(prebattle_resolution_return_expected));
    constructor_global_target =
        (void *)(backend_base + DGVOODOO_CONSTRUCTOR_GLOBAL_RVA);
    custom_acquire_target =
        (void *)(backend_base + DGVOODOO_CUSTOM_ACQUIRE_RVA);
    custom_release_target =
        (void *)(backend_base + DGVOODOO_CUSTOM_RELEASE_RVA);
    nested_acquire_target =
        (void *)(backend_base + DGVOODOO_NESTED_ACQUIRE_RVA);
    veh_handled_continue =
        (void *)(backend_base + DGVOODOO_VEH_HANDLED_RVA);

    capture_installed = write_rel_jump(capture_target, capture_hook_stub,
                                       capture_expected,
                                       sizeof(capture_expected));
    if (!capture_installed) return 0;
    restore_installed = write_rel_jump(restore_target, restore_hook_stub,
                                       restore_expected,
                                       sizeof(restore_expected));
    if (!restore_installed) {
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    commit_installed = write_rel_jump(commit_target, commit_hook_stub,
                                      commit_expected,
                                      sizeof(commit_expected));
    if (!commit_installed) {
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    constructor_installed = write_rel_jump(
        constructor_target, constructor_hook_stub, constructor_expected,
        sizeof(constructor_expected));
    if (!constructor_installed) {
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    if (!write_rel_call(fallback_call_target, fallback_callsite_hook_stub,
                        fallback_call_expected)) {
        restore_original(constructor_target, constructor_expected,
                         sizeof(constructor_expected));
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    reverse_dispatcher_installed = write_rel_jump(
        reverse_dispatcher_target, reverse_dispatcher_hook_stub,
        reverse_dispatcher_expected, sizeof(reverse_dispatcher_expected));
    if (!reverse_dispatcher_installed) {
        restore_original(fallback_call_target, fallback_call_expected,
                         sizeof(fallback_call_expected));
        restore_original(constructor_target, constructor_expected,
                         sizeof(constructor_expected));
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    primary_surface_unlock_installed = write_rel_jump(
        primary_surface_unlock_target, primary_surface_unlock_hook_stub,
        primary_surface_unlock_expected,
        sizeof(primary_surface_unlock_expected));
    if (!primary_surface_unlock_installed) {
        restore_original(reverse_dispatcher_target,
                         reverse_dispatcher_expected,
                         sizeof(reverse_dispatcher_expected));
        restore_original(fallback_call_target, fallback_call_expected,
                         sizeof(fallback_call_expected));
        restore_original(constructor_target, constructor_expected,
                         sizeof(constructor_expected));
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    prebattle_entry_installed = write_rel_call(
        prebattle_entry_call_target, prebattle_entry_hook_stub,
        prebattle_entry_call_expected);
    if (!prebattle_entry_installed) {
        restore_original(primary_surface_unlock_target,
                         primary_surface_unlock_expected,
                         sizeof(primary_surface_unlock_expected));
        restore_original(reverse_dispatcher_target,
                         reverse_dispatcher_expected,
                         sizeof(reverse_dispatcher_expected));
        restore_original(fallback_call_target, fallback_call_expected,
                         sizeof(fallback_call_expected));
        restore_original(constructor_target, constructor_expected,
                         sizeof(constructor_expected));
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    prebattle_resolution_return_installed = write_rel_jump(
        prebattle_resolution_return_target,
        prebattle_resolution_return_hook_stub,
        prebattle_resolution_return_expected,
        sizeof(prebattle_resolution_return_expected));
    if (!prebattle_resolution_return_installed) {
        restore_original(prebattle_entry_call_target,
                         prebattle_entry_call_expected,
                         sizeof(prebattle_entry_call_expected));
        restore_original(primary_surface_unlock_target,
                         primary_surface_unlock_expected,
                         sizeof(primary_surface_unlock_expected));
        restore_original(reverse_dispatcher_target,
                         reverse_dispatcher_expected,
                         sizeof(reverse_dispatcher_expected));
        restore_original(fallback_call_target, fallback_call_expected,
                         sizeof(fallback_call_expected));
        restore_original(constructor_target, constructor_expected,
                         sizeof(constructor_expected));
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    if (!install_outer_owned_guard_diversion()) {
        restore_original(prebattle_resolution_return_target,
                         prebattle_resolution_return_expected,
                         sizeof(prebattle_resolution_return_expected));
        restore_original(prebattle_entry_call_target,
                         prebattle_entry_call_expected,
                         sizeof(prebattle_entry_call_expected));
        restore_original(primary_surface_unlock_target,
                         primary_surface_unlock_expected,
                         sizeof(primary_surface_unlock_expected));
        restore_original(reverse_dispatcher_target,
                         reverse_dispatcher_expected,
                         sizeof(reverse_dispatcher_expected));
        restore_original(fallback_call_target, fallback_call_expected,
                         sizeof(fallback_call_expected));
        restore_original(constructor_target, constructor_expected,
                         sizeof(constructor_expected));
        restore_original(commit_target, commit_expected,
                         sizeof(commit_expected));
        restore_original(restore_target, restore_expected,
                         sizeof(restore_expected));
        restore_original(capture_target, capture_expected,
                         sizeof(capture_expected));
        return 0;
    }
    if (!install_dispatch_message_hook()) {
        diagnostic_write("frontend input generation hook unavailable r96\r\n");
    } else {
        diagnostic_write("frontend content-shadow hook active r98\r\n");
    }
    return 1;
}

static BOOL CALLBACK loading_frontend_install(PINIT_ONCE once,
                                              PVOID parameter,
                                              PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    diagnostic_open();
    game_base = (uintptr_t)GetModuleHandleW(NULL);
    backend_base = (uintptr_t)GetModuleHandleW(L"dgVoodoo_D3D9.dll");
    InitializeCriticalSection(&loading_shadow_lock);
    InitializeCriticalSection(&focus_span_lock);
    InitializeCriticalSection(&constructor_return_lock);
    loading_shadow_initialize(
        &loading_shadow,
        allocate_loading_shadow,
        free_loading_shadow,
        NULL);
    focus_plane_shadow_initialize(
        &focus_plane_shadow,
        (unsigned char *)VirtualAlloc(
            NULL, MTW_FOCUS_PLANE_HISTORY_BYTES,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE),
        MTW_FOCUS_PLANE_HISTORY_BYTES);
    frontend_page_snapshot = (unsigned char *)VirtualAlloc(
        NULL, MTW_FOCUS_PLANE_BYTES,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    diagnostic_fallback_snapshot = (unsigned char *)VirtualAlloc(
        NULL, MTW_FOCUS_PLANE_BYTES,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    diagnostic_framebuffer_snapshot = (unsigned char *)VirtualAlloc(
        NULL, MTW_FOCUS_PLANE_BYTES,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    diagnostic_reverse_snapshot = (unsigned char *)VirtualAlloc(
        NULL, MTW_FOCUS_PLANE_BYTES,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (focus_plane_shadow.bytes == NULL || frontend_page_snapshot == NULL) {
        loading_shadow_release(&loading_shadow);
        if (focus_plane_shadow.bytes != NULL) {
            VirtualFree(focus_plane_shadow.bytes, 0u, MEM_RELEASE);
            memset(&focus_plane_shadow, 0, sizeof(focus_plane_shadow));
        }
        if (frontend_page_snapshot != NULL) {
            VirtualFree(frontend_page_snapshot, 0u, MEM_RELEASE);
            frontend_page_snapshot = NULL;
        }
        diagnostic_write("install allocation failed\r\n");
        return TRUE;
    }
    if (install_all_hooks_transactionally()) {
        if (install_mapper_shader_clone_hook()) {
            diagnostic_write("mapper shader clone hook installed r154\r\n");
        } else {
            diagnostic_write("mapper shader clone hook unavailable r154\r\n");
        }
        diagnostic_write("focus-group reverse authority active r6f160\r\n");
        diagnostic_write("outer-owned guard diversion active r6f160\r\n");
        diagnostic_write("retained-base preunlock generation seed active r116\r\n");
        diagnostic_write("exact fallback callsite active r122\r\n");
        diagnostic_write("wrapped shadow span guard active r156\r\n");
        diagnostic_write("first fallback plane oracle active r157\r\n");
        diagnostic_write("qualified backing generation policy ready r158\r\n");
        diagnostic_write(
            "backing content evolution diagnostic active r159\r\n");
        diagnostic_write(
            "post-reverse generation diagnostic active r160\r\n");
        diagnostic_write(
            "generation-seeded wrapped row-major publication active r181\r\n");
        diagnostic_write(
            "active-generation reverse continuity active r183\r\n");
        diagnostic_write(
            "prebattle resolution semantic prearm active r194\r\n");
        diagnostic_write("install active r6f160\r\n");
    } else {
        loading_shadow_release(&loading_shadow);
        VirtualFree(focus_plane_shadow.bytes, 0u, MEM_RELEASE);
        memset(&focus_plane_shadow, 0, sizeof(focus_plane_shadow));
        VirtualFree(frontend_page_snapshot, 0u, MEM_RELEASE);
        frontend_page_snapshot = NULL;
        diagnostic_write("install fail closed\r\n");
    }
    return TRUE;
}

void frontend_fix_prepare_backend_tracking(void) {
    InitOnceExecuteOnce(&backend_tracking_once, prepare_backend_tracking,
                        NULL, NULL);
}

void frontend_fix_install_once(void) {
    InitOnceExecuteOnce(&frontend_once, loading_frontend_install, NULL, NULL);
}
