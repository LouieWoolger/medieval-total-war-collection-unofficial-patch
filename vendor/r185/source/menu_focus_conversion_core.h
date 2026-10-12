#ifndef MTW_MENU_FOCUS_CONVERSION_CORE_H
#define MTW_MENU_FOCUS_CONVERSION_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The pinned game calls IDirect3DDevice9::UpdateSurface at this site.  The
 * pinned dgVoodoo 2.87.5 backend calls ID3D11DeviceContext::Draw at the
 * second site.  These instruction bytes are checked before either patch. */
#define MTW_MENU_GAME_CALLSITE_SIZE 5u
#define MTW_MENU_BACKEND_CALLSITE_SIZE 6u
#define MTW_MENU_DXGI_RGBA8_TYPELESS 27u
#define MTW_MENU_DXGI_R16_TYPELESS 53u

typedef struct mtw_menu_conversion_observation {
    unsigned frontend_active;
    unsigned game_update_surface_active;
    unsigned nested_draw_index;
    unsigned vertex_count;
    unsigned pixel_shader_matches;
    unsigned render_target_count;
    unsigned source_width;
    unsigned source_height;
    unsigned source_format;
    unsigned target_width;
    unsigned target_height;
    unsigned target_format;
} mtw_menu_conversion_observation;

static int mtw_menu_game_callsite_supported(const unsigned char *bytes,
                                            size_t size) {
    static const unsigned char expected[MTW_MENU_GAME_CALLSITE_SIZE] = {
        0x8B, 0x41, 0x78, 0xFF, 0xD0
    };
    return bytes != NULL && size == sizeof(expected) &&
           memcmp(bytes, expected, sizeof(expected)) == 0;
}

static int mtw_menu_backend_callsite_supported(const unsigned char *bytes,
                                               size_t size) {
    static const unsigned char expected[MTW_MENU_BACKEND_CALLSITE_SIZE] = {
        0x6A, 0x04, 0x51, 0xFF, 0x50, 0x34
    };
    return bytes != NULL && size == sizeof(expected) &&
           memcmp(bytes, expected, sizeof(expected)) == 0;
}

static int mtw_menu_conversion_should_correct(
    const mtw_menu_conversion_observation *observation) {
    return observation != NULL &&
           observation->frontend_active != 0u &&
           observation->game_update_surface_active != 0u &&
           observation->nested_draw_index == 3u &&
           observation->vertex_count == 4u &&
           observation->pixel_shader_matches != 0u &&
           observation->render_target_count == 1u &&
           observation->source_width == 800u &&
           observation->source_height == 600u &&
           observation->source_format == MTW_MENU_DXGI_RGBA8_TYPELESS &&
           observation->target_width == 800u &&
           observation->target_height == 600u &&
           observation->target_format == MTW_MENU_DXGI_R16_TYPELESS;
}

#endif
