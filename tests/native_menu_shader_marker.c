#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define COBJMACROS
#include <d3d11.h>
#include <stdio.h>

#include "menu_focus_shader_bytecode.h"
#include "menu_focus_shader_marker.h"

static unsigned passed;
static unsigned failed;

static void check(int condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL %s\n", name); }
}

static int create_warp_device(ID3D11Device **device,
                              ID3D11DeviceContext **context) {
    D3D_FEATURE_LEVEL selected;
    return SUCCEEDED(D3D11CreateDevice(
        NULL, D3D_DRIVER_TYPE_WARP, NULL, 0u, NULL, 0u,
        D3D11_SDK_VERSION, device, &selected, context));
}

static ID3D11PixelShader *create_exact_shader(ID3D11Device *device) {
    ID3D11PixelShader *shader = NULL;
    if (FAILED(ID3D11Device_CreatePixelShader(
            device, mtw_menu_problem_pixel_shader_dxbc,
            sizeof(mtw_menu_problem_pixel_shader_dxbc),
            NULL, &shader))) return NULL;
    return shader;
}

int main(void) {
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11PixelShader *first = NULL;
    ID3D11PixelShader *second = NULL;
    ID3D11PixelShader *untagged = NULL;
    ID3D11PixelShader *recreated = NULL;

    check(mtw_menu_problem_pixel_shader_matches(
              mtw_menu_problem_pixel_shader_dxbc,
              sizeof(mtw_menu_problem_pixel_shader_dxbc)),
          "captured shader bytecode exact");
    if (!create_warp_device(&device, &context)) {
        fprintf(stderr, "FAIL create WARP device\n");
        return 1;
    }
    first = create_exact_shader(device);
    second = create_exact_shader(device);
    untagged = create_exact_shader(device);
    check(first != NULL && second != NULL && untagged != NULL,
          "three exact shader objects created");
    if (first != NULL && second != NULL && untagged != NULL) {
        check(first != second, "distinct identical shader objects");
        check(!mtw_menu_problem_pixel_shader_is_marked(first),
              "first initially unmarked");
        check(!mtw_menu_problem_pixel_shader_is_marked(second),
              "second initially unmarked");
        check(mtw_menu_mark_problem_pixel_shader(first), "mark first");
        check(mtw_menu_mark_problem_pixel_shader(second), "mark second");
        check(mtw_menu_problem_pixel_shader_is_marked(first),
              "first retains marker");
        check(mtw_menu_problem_pixel_shader_is_marked(second),
              "second retains marker");
        check(!mtw_menu_problem_pixel_shader_is_marked(untagged),
              "third exact but untagged shader rejected");
        ID3D11PixelShader_Release(first);
        first = NULL;
        check(mtw_menu_problem_pixel_shader_is_marked(second),
              "second marker survives first release");
    }
    if (first != NULL) ID3D11PixelShader_Release(first);
    if (second != NULL) ID3D11PixelShader_Release(second);
    if (untagged != NULL) ID3D11PixelShader_Release(untagged);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);

    device = NULL;
    context = NULL;
    check(create_warp_device(&device, &context), "recreate WARP device");
    if (device != NULL && context != NULL) {
        recreated = create_exact_shader(device);
        check(recreated != NULL, "create shader on replacement device");
        if (recreated != NULL) {
            check(!mtw_menu_problem_pixel_shader_is_marked(recreated),
                  "replacement shader starts unmarked");
            check(mtw_menu_mark_problem_pixel_shader(recreated),
                  "mark replacement shader");
            check(mtw_menu_problem_pixel_shader_is_marked(recreated),
                  "replacement shader marker readable");
            ID3D11PixelShader_Release(recreated);
        }
        ID3D11DeviceContext_Release(context);
        ID3D11Device_Release(device);
    }
    printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
