#ifndef MTW_MENU_FOCUS_SHADER_MARKER_H
#define MTW_MENU_FOCUS_SHADER_MARKER_H

#include <stdint.h>
#include <string.h>
#include <d3d11.h>

/* Each exact backend PS bytecode instance gets its own D3D11 private-data tag.
 * This survives a second identical shader object on the same device without
 * retaining a COM reference to a stale object after device recreation. */
static const GUID mtw_menu_problem_ps_guid = {
    0xfff8c29e, 0x15a1, 0x41bf,
    {0xb1, 0x7c, 0x09, 0x88, 0x64, 0xc0, 0x9f, 0xde}
};
static const uint32_t mtw_menu_problem_ps_marker = 0x3238304du;

static int mtw_menu_mark_problem_pixel_shader(ID3D11PixelShader *shader) {
    return shader != NULL &&
           SUCCEEDED(ID3D11PixelShader_SetPrivateData(
               shader, &mtw_menu_problem_ps_guid,
               sizeof(mtw_menu_problem_ps_marker),
               &mtw_menu_problem_ps_marker));
}

static int mtw_menu_problem_pixel_shader_is_marked(
    ID3D11PixelShader *shader) {
    uint32_t value = 0u;
    UINT size = sizeof(value);
    return shader != NULL &&
           SUCCEEDED(ID3D11PixelShader_GetPrivateData(
               shader, &mtw_menu_problem_ps_guid, &size, &value)) &&
           size == sizeof(value) &&
           value == mtw_menu_problem_ps_marker;
}

#endif
