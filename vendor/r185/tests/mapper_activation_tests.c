#include <assert.h>
#include <stdio.h>

#include "../source/mapper_activation_core.h"

static void test_verified_loading_plane_disarms_mapper(void) {
    assert(mtw_mapper_mode_after_loading(1, 2560u, 1440u, 5120u) == 0);
    assert(mtw_mapper_mode_after_loading(1, 1920u, 1080u, 3840u) == 0);
    assert(mtw_mapper_mode_after_loading(1, 1280u, 720u, 2560u) == 0);
    assert(mtw_mapper_mode_after_loading(1, 1024u, 768u, 2048u) == 0);
    assert(mtw_mapper_mode_after_loading(1, 3440u, 1440u, 6880u) == 0);
    assert(mtw_mapper_mode_after_loading(1, 1600u, 900u, 3264u) == 0);
    assert(mtw_mapper_mode_after_loading(0, 2560u, 1440u, 5120u) == 0);
}

static void test_invalid_loading_plane_does_not_disarm_mapper(void) {
    assert(mtw_mapper_mode_after_loading(1, 0u, 1080u, 3840u) == 1);
    assert(mtw_mapper_mode_after_loading(1, 1920u, 0u, 3840u) == 1);
    assert(mtw_mapper_mode_after_loading(1, 1920u, 1080u, 3839u) == 1);
    assert(mtw_mapper_mode_after_loading(
               1, UINT32_MAX, 1u, UINT32_MAX) == 1);
}

static void test_verified_frontend_lifecycle_rearms_mapper(void) {
    assert(mtw_mapper_mode_after_surface(0, 640u, 480u, 2560u, 4u) == 1);
    assert(mtw_mapper_mode_after_surface(0, 800u, 600u, 1600u, 2u) == 1);
    assert(mtw_mapper_mode_after_surface(0, 800u, 600u, 3200u, 4u) == 0);
    assert(mtw_mapper_mode_after_surface(0, 2560u, 1440u, 5120u, 2u) == 0);
}

static void test_unrelated_surfaces_preserve_current_mode(void) {
    assert(mtw_mapper_mode_after_surface(1, 1024u, 1024u, 4096u, 4u) == 1);
    assert(mtw_mapper_mode_after_surface(0, 1024u, 1024u, 4096u, 4u) == 0);
}

int main(void) {
    test_verified_loading_plane_disarms_mapper();
    test_invalid_loading_plane_does_not_disarm_mapper();
    test_verified_frontend_lifecycle_rearms_mapper();
    test_unrelated_surfaces_preserve_current_mode();
    puts("mapper activation tests passed");
    return 0;
}
