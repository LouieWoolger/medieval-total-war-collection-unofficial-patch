#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "menu_focus_conversion_core.h"
#include "menu_focus_shader_bytecode.h"

static unsigned passed;
static unsigned failed;

static void check(int condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL %s\n", name); }
}

int main(void) {
    static const unsigned char game_call[MTW_MENU_GAME_CALLSITE_SIZE] = {
        0x8B, 0x41, 0x78, 0xFF, 0xD0
    };
    static const unsigned char backend_draw[MTW_MENU_BACKEND_CALLSITE_SIZE] = {
        0x6A, 0x04, 0x51, 0xFF, 0x50, 0x34
    };
    unsigned char changed[MTW_MENU_BACKEND_CALLSITE_SIZE];
    unsigned char changed_shader[sizeof(mtw_menu_problem_pixel_shader_dxbc)];
    mtw_menu_conversion_observation observation = {
        1, 1, 3, 4, 1, 1,
        800, 600, MTW_MENU_DXGI_RGBA8_TYPELESS,
        800, 600, MTW_MENU_DXGI_R16_TYPELESS
    };

    check(mtw_menu_game_callsite_supported(game_call,
          sizeof(game_call)), "exact game callsite");
    check(mtw_menu_backend_callsite_supported(backend_draw,
          sizeof(backend_draw)), "exact backend draw callsite");
    check(!mtw_menu_game_callsite_supported(game_call,
          sizeof(game_call) - 1), "short game callsite");
    check(!mtw_menu_backend_callsite_supported(backend_draw,
          sizeof(backend_draw) - 1), "short backend callsite");
    memcpy(changed, backend_draw, sizeof(changed));
    changed[5] ^= 1;
    check(!mtw_menu_backend_callsite_supported(changed,
          sizeof(changed)), "changed backend byte");
    check(mtw_menu_problem_pixel_shader_matches(
          mtw_menu_problem_pixel_shader_dxbc,
          sizeof(mtw_menu_problem_pixel_shader_dxbc)), "exact shader bytes");
    memcpy(changed_shader, mtw_menu_problem_pixel_shader_dxbc,
           sizeof(changed_shader));
    changed_shader[sizeof(changed_shader) / 2u] ^= 1;
    check(!mtw_menu_problem_pixel_shader_matches(changed_shader,
          sizeof(changed_shader)), "changed shader middle byte");
    check(!mtw_menu_problem_pixel_shader_matches(
          mtw_menu_problem_pixel_shader_dxbc,
          sizeof(mtw_menu_problem_pixel_shader_dxbc) - 1u),
          "short shader bytes");
    check(sizeof(mtw_menu_pixel_aligned_vertex_shader_dxbc) == 896u,
          "replacement DXBC length");

    check(mtw_menu_conversion_should_correct(&observation),
          "third exact one-target frontend Draw");
    observation.game_update_surface_active = 0;
    check(!mtw_menu_conversion_should_correct(&observation),
          "outside exact game call");
    observation.game_update_surface_active = 1;
    observation.frontend_active = 0;
    check(!mtw_menu_conversion_should_correct(&observation),
          "battle/frontend disabled");
    observation.frontend_active = 1;
    observation.nested_draw_index = 2;
    check(!mtw_menu_conversion_should_correct(&observation),
          "transitional two-Draw call");
    observation.nested_draw_index = 3;
    observation.pixel_shader_matches = 0;
    check(!mtw_menu_conversion_should_correct(&observation),
          "unrecognized shader");
    observation.pixel_shader_matches = 1;
    observation.render_target_count = 2;
    check(!mtw_menu_conversion_should_correct(&observation),
          "two-target conversion");
    observation.render_target_count = 1;
    observation.source_width = 1024;
    check(!mtw_menu_conversion_should_correct(&observation),
          "unknown source dimensions");
    observation.source_width = 800;
    observation.target_format = MTW_MENU_DXGI_RGBA8_TYPELESS;
    check(!mtw_menu_conversion_should_correct(&observation),
          "unknown target format");
    check(!mtw_menu_conversion_should_correct(NULL), "null observation");

    printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
