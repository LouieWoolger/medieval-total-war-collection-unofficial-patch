#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/loading_shadow_core.h"

static int failures;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);  \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

typedef struct test_allocator {
    unsigned long allocations;
    unsigned long releases;
    size_t last_size;
    int fail_next;
} test_allocator;

typedef struct geometry_case {
    size_t width;
    size_t height;
    size_t pitch;
} geometry_case;

static void *test_allocate(size_t size, void *context) {
    test_allocator *allocator = (test_allocator *)context;
    ++allocator->allocations;
    allocator->last_size = size;
    if (allocator->fail_next) {
        allocator->fail_next = 0;
        return NULL;
    }
    return malloc(size);
}

static void test_release(void *memory, void *context) {
    test_allocator *allocator = (test_allocator *)context;
    ++allocator->releases;
    free(memory);
}

static loading_shadow_surface make_surface(void *bits,
                                           size_t width,
                                           size_t height,
                                           size_t pitch,
                                           uintptr_t epoch) {
    loading_shadow_surface surface;
    surface.bits = bits;
    surface.width = width;
    surface.height = height;
    surface.pitch = pitch;
    surface.bytes_per_pixel = 2u;
    surface.epoch = epoch;
    return surface;
}

static void seed_plane(unsigned char *bytes, size_t size, unsigned char seed) {
    memset(bytes, seed, size);
    bytes[0] = (unsigned char)(seed + 1u);
    bytes[size / 2u] = (unsigned char)(seed + 2u);
    bytes[size - 1u] = (unsigned char)(seed + 3u);
}

static void test_supported_geometry_matrix(void) {
    static const geometry_case cases[] = {
        {1920u, 1080u, 3840u},
        {2560u, 1440u, 5120u},
        {1280u, 720u, 2560u},
        {1024u, 768u, 2048u},
        {3440u, 1440u, 6880u},
        {1600u, 900u, 3264u}
    };
    loading_shadow_state state;
    test_allocator allocator = {0};
    size_t largest_capacity = 0u;
    size_t i;

    loading_shadow_initialize(
        &state, test_allocate, test_release, &allocator);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        loading_shadow_surface surface;
        unsigned char *live;
        size_t size = cases[i].pitch * cases[i].height;
        size_t checked_size = 0u;

        live = (unsigned char *)malloc(size);
        CHECK(live != NULL);
        if (live == NULL) continue;
        seed_plane(live, size, (unsigned char)(0x10u + i));
        surface = make_surface(
            live, cases[i].width, cases[i].height, cases[i].pitch, i + 1u);

        CHECK(loading_shadow_surface_size(&surface, &checked_size));
        CHECK(checked_size == size);
        CHECK(loading_shadow_capture(&state, &surface));
        CHECK(state.valid == 1);
        CHECK(state.size == size);
        CHECK(state.capacity >= size);
        CHECK(state.surface.bits == live);
        CHECK(state.surface.width == cases[i].width);
        CHECK(state.surface.height == cases[i].height);
        CHECK(state.surface.pitch == cases[i].pitch);
        CHECK(state.surface.bytes_per_pixel == 2u);
        CHECK(state.surface.epoch == i + 1u);
        CHECK(state.bytes[0] == (unsigned char)(0x11u + i));
        CHECK(state.bytes[size / 2u] == (unsigned char)(0x12u + i));
        CHECK(state.bytes[size - 1u] == (unsigned char)(0x13u + i));

        memset(live, 0, size);
        CHECK(loading_shadow_restore_after_lock(&state, &surface));
        CHECK(live[0] == (unsigned char)(0x11u + i));
        CHECK(live[size / 2u] == (unsigned char)(0x12u + i));
        CHECK(live[size - 1u] == (unsigned char)(0x13u + i));

        live[size - 1u] ^= 0x5Au;
        CHECK(loading_shadow_commit_before_unlock(&state, &surface));
        memset(live, 0, size);
        CHECK(loading_shadow_restore_after_lock(&state, &surface));
        CHECK(live[size - 1u] ==
              (unsigned char)((0x13u + i) ^ 0x5Au));

        if (size > largest_capacity) largest_capacity = size;
        CHECK(state.capacity == largest_capacity);
        free(live);
    }
    CHECK(state.capture_count ==
          sizeof(cases) / sizeof(cases[0]));
    CHECK(state.restore_count ==
          2u * (sizeof(cases) / sizeof(cases[0])));
    CHECK(state.commit_count ==
          sizeof(cases) / sizeof(cases[0]));
    CHECK(allocator.allocations == 3u);
    CHECK(allocator.releases == 2u);
    loading_shadow_release(&state);
    CHECK(allocator.releases == 3u);
    CHECK(state.bytes == NULL);
    CHECK(state.capacity == 0u);
    CHECK(state.valid == 0);
}

static void test_resolution_transitions(void) {
    loading_shadow_state state;
    test_allocator allocator = {0};
    const geometry_case cases[] = {
        {2560u, 1440u, 5120u},
        {1920u, 1080u, 3840u},
        {2560u, 1440u, 5120u}
    };
    uintptr_t epoch;

    loading_shadow_initialize(
        &state, test_allocate, test_release, &allocator);
    for (epoch = 1u; epoch <= 3u; ++epoch) {
        const geometry_case *geometry = &cases[epoch - 1u];
        const size_t size = geometry->pitch * geometry->height;
        unsigned char *live = (unsigned char *)malloc(size);
        loading_shadow_surface surface;

        CHECK(live != NULL);
        if (live == NULL) continue;
        seed_plane(live, size, (unsigned char)(0x30u + epoch));
        surface = make_surface(
            live, geometry->width, geometry->height, geometry->pitch, epoch);
        CHECK(loading_shadow_capture(&state, &surface));
        CHECK(state.surface.epoch == epoch);
        CHECK(state.size == size);
        free(live);
    }
    CHECK(allocator.allocations == 1u);
    CHECK(allocator.releases == 0u);
    loading_shadow_release(&state);
}

static void test_pointer_epoch_and_geometry_invalidation(void) {
    loading_shadow_state state;
    test_allocator allocator = {0};
    const size_t size = 64u * 32u * 2u;
    unsigned char *first = (unsigned char *)malloc(size);
    unsigned char *second = (unsigned char *)malloc(size);
    loading_shadow_surface captured;
    loading_shadow_surface changed;

    CHECK(first != NULL && second != NULL);
    if (first == NULL || second == NULL) {
        free(first);
        free(second);
        return;
    }
    loading_shadow_initialize(
        &state, test_allocate, test_release, &allocator);
    seed_plane(first, size, 0x40u);
    seed_plane(second, size, 0x50u);
    captured = make_surface(first, 64u, 32u, 128u, 1u);
    CHECK(loading_shadow_capture(&state, &captured));

    changed = captured;
    changed.bits = second;
    CHECK(!loading_shadow_restore_after_lock(&state, &changed));
    CHECK(state.valid == 0);
    CHECK(state.invalidation_count == 1u);

    changed.epoch = 2u;
    CHECK(loading_shadow_capture(&state, &changed));
    captured = changed;
    changed.epoch = 3u;
    CHECK(!loading_shadow_commit_before_unlock(&state, &changed));
    CHECK(state.valid == 0);
    CHECK(state.invalidation_count == 2u);

    CHECK(loading_shadow_capture(&state, &captured));
    changed = captured;
    changed.width = 63u;
    CHECK(!loading_shadow_restore_after_lock(&state, &changed));
    CHECK(state.valid == 0);
    CHECK(state.invalidation_count == 3u);

    loading_shadow_release(&state);
    free(first);
    free(second);
}

static void test_allocation_failure_is_fail_closed(void) {
    loading_shadow_state state;
    test_allocator allocator = {0};
    unsigned char small[128];
    unsigned char *large;
    loading_shadow_surface small_surface;
    loading_shadow_surface large_surface;
    unsigned char *original_storage;
    size_t original_capacity;

    loading_shadow_initialize(
        &state, test_allocate, test_release, &allocator);
    memset(small, 0x61, sizeof(small));
    small_surface = make_surface(small, 8u, 8u, 16u, 1u);
    CHECK(loading_shadow_capture(&state, &small_surface));
    original_storage = state.bytes;
    original_capacity = state.capacity;

    large = (unsigned char *)malloc(1920u * 1080u * 2u);
    CHECK(large != NULL);
    if (large == NULL) {
        loading_shadow_release(&state);
        return;
    }
    memset(large, 0x62, 1920u * 1080u * 2u);
    large_surface = make_surface(large, 1920u, 1080u, 3840u, 2u);
    allocator.fail_next = 1;
    CHECK(!loading_shadow_capture(&state, &large_surface));
    CHECK(state.valid == 0);
    CHECK(state.bytes == original_storage);
    CHECK(state.capacity == original_capacity);
    CHECK(allocator.allocations == 2u);
    CHECK(allocator.releases == 0u);

    CHECK(loading_shadow_capture(&state, &large_surface));
    CHECK(state.valid == 1);
    CHECK(state.bytes != original_storage);
    CHECK(state.capacity == 1920u * 1080u * 2u);
    CHECK(allocator.allocations == 3u);
    CHECK(allocator.releases == 1u);

    loading_shadow_release(&state);
    CHECK(allocator.releases == 2u);
    free(large);
}

static void test_invalid_and_overflowing_geometry(void) {
    loading_shadow_state state;
    test_allocator allocator = {0};
    unsigned char live[64] = {0};
    loading_shadow_surface surface =
        make_surface(live, 4u, 4u, 8u, 1u);
    size_t size = 0u;

    loading_shadow_initialize(
        &state, test_allocate, test_release, &allocator);
    CHECK(loading_shadow_surface_size(&surface, &size));
    CHECK(size == 32u);
    CHECK(loading_shadow_capture(&state, &surface));

    surface.bits = NULL;
    CHECK(!loading_shadow_surface_size(&surface, &size));
    CHECK(!loading_shadow_restore_after_lock(&state, &surface));
    CHECK(state.valid == 0);

    surface = make_surface(live, 0u, 4u, 8u, 2u);
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(live, 4u, 0u, 8u, 2u);
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(live, 4u, 4u, 7u, 2u);
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(live, 4u, 4u, 8u, 0u);
    surface.bytes_per_pixel = 0u;
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(live, 4u, 4u, 8u, 0u);
    CHECK(!loading_shadow_surface_size(&surface, &size));

    surface = make_surface(
        live, MTW_LOADING_SHADOW_MAX_DIMENSION + 1u, 1u,
        (MTW_LOADING_SHADOW_MAX_DIMENSION + 1u) * 2u, 3u);
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(
        live, 1u, MTW_LOADING_SHADOW_MAX_DIMENSION + 1u, 2u, 3u);
    CHECK(!loading_shadow_surface_size(&surface, &size));

    surface = make_surface(live, (size_t)-1, 1u, (size_t)-1, 4u);
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(live, 1u, 2u, (size_t)-1, 4u);
    CHECK(!loading_shadow_surface_size(&surface, &size));
    surface = make_surface(
        live, 1u, MTW_LOADING_SHADOW_MAX_DIMENSION,
        MTW_LOADING_SHADOW_MAX_BYTES / MTW_LOADING_SHADOW_MAX_DIMENSION + 1u,
        4u);
    CHECK(!loading_shadow_surface_size(&surface, &size));

    CHECK(allocator.allocations == 1u);
    loading_shadow_release(&state);
}

static void test_incompatible_and_reentrant_operations(void) {
    loading_shadow_state state;
    test_allocator allocator = {0};
    unsigned char live[128];
    loading_shadow_surface surface =
        make_surface(live, 8u, 8u, 16u, 1u);

    loading_shadow_initialize(
        &state, test_allocate, test_release, &allocator);
    memset(live, 0x71, sizeof(live));
    CHECK(!loading_shadow_restore_after_lock(&state, &surface));
    CHECK(!loading_shadow_commit_before_unlock(&state, &surface));

    state.operation_active = 1;
    CHECK(!loading_shadow_capture(&state, &surface));
    CHECK(!loading_shadow_restore_after_lock(&state, &surface));
    CHECK(!loading_shadow_commit_before_unlock(&state, &surface));
    CHECK(state.capture_count == 0u);
    state.operation_active = 0;

    CHECK(loading_shadow_capture(&state, &surface));
    CHECK(state.capture_count == 1u);
    CHECK(state.restore_count == 0u);
    CHECK(state.commit_count == 0u);
    loading_shadow_release(&state);
}

int main(void) {
    test_supported_geometry_matrix();
    test_resolution_transitions();
    test_pointer_epoch_and_geometry_invalidation();
    test_allocation_failure_is_fail_closed();
    test_invalid_and_overflowing_geometry();
    test_incompatible_and_reentrant_operations();

    if (failures != 0) return 1;
    puts("loading shadow tests: PASS");
    return 0;
}
