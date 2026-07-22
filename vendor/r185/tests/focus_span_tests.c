#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/focus_span_core.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression);    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static int register_surface(focus_span_state *state,
                            uintptr_t object,
                            uintptr_t backing) {
    int startup_slow_lane = focus_span_observe_surface(
        state, 800u, 600u, 1600u, 2u, object, backing);
    CHECK(focus_span_complete_surface(
              state, 800u, 600u, 1600u, 2u, object, backing));
    return startup_slow_lane;
}

static int publish(focus_span_state *state,
                   uintptr_t backing,
                   const focus_span_rect *rect) {
    return focus_span_observe_publication(
        state, backing, 1600u, 0x7C335u, rect);
}

static void begin_frontend(focus_span_state *state) {
    memset(state, 0, sizeof(*state));
    CHECK(focus_span_observe_surface(
              state, 640u, 480u, 2560u, 4u, 0x1000u, 0x2000u) == 0);
}

static void test_actual_publication_establishes_authority(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect partial = {224, 33, 704, 570};
    uintptr_t objects[4] = {0x10000u, 0x11000u, 0x12000u, 0x13000u};
    uintptr_t backings[4] = {0x20000u, 0x21000u, 0x22000u, 0x23000u};
    unsigned int index;

    begin_frontend(&state);
    for (index = 0u; index < 4u; ++index) {
        CHECK(register_surface(&state, objects[index], backings[index]) ==
              (index == 0u));
    }

    /* Constructor ordinal alone never grants content authority. */
    for (index = 0u; index < 4u; ++index) {
        CHECK(!focus_span_should_ack_reverse(&state, objects[index]));
        CHECK(!publish(&state, backings[index], &partial));
    }

    /* A complete publication establishes authority for that exact generation. */
    CHECK(!publish(&state, backings[2], &full));
    CHECK(focus_span_should_ack_reverse(&state, objects[2]));
    CHECK(!focus_span_should_ack_reverse(&state, objects[0]));
    CHECK(publish(&state, backings[2], &partial));

    /* Any behavioral role can become authoritative; no 5 + 4n arithmetic. */
    CHECK(!publish(&state, backings[1], &full));
    CHECK(focus_span_should_ack_reverse(&state, objects[1]));
    CHECK(publish(&state, backings[1], &partial));
}

static void test_arbitrary_partial_spans_expand(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect spans[] = {
        {224, 33, 704, 570},
        {64, 46, 448, 66},
        {64, 174, 800, 191},
        {480, 537, 256, 568},
        {32, 599, 800, 599},
        {0, 0, 384, 20}
    };
    unsigned int index;

    begin_frontend(&state);
    CHECK(register_surface(&state, 0x30000u, 0x40000u));
    CHECK(!publish(&state, 0x40000u, &full));
    for (index = 0u; index < sizeof(spans) / sizeof(spans[0]); ++index) {
        CHECK(publish(&state, 0x40000u, &spans[index]));
    }

    CHECK(!focus_span_observe_publication(
        &state, 0x40000u, 1599u, 0x7C335u, &spans[0]));
    CHECK(!focus_span_observe_publication(
        &state, 0x40000u, 1600u, 0x7C334u, &spans[0]));
    CHECK(!publish(&state, 0xDEADu, &spans[0]));
    CHECK(!publish(&state, 0x40000u, NULL));
}

static void test_object_and_backing_reuse_create_new_generations(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect partial = {320, 38, 544, 71};
    uint32_t first_generation;

    begin_frontend(&state);
    CHECK(register_surface(&state, 0x50000u, 0x60000u));
    first_generation = focus_span_generation_for_object(&state, 0x50000u);
    CHECK(first_generation != 0u);
    CHECK(!publish(&state, 0x60000u, &full));
    CHECK(publish(&state, 0x60000u, &partial));

    /* Raw object reuse revokes old content authority. */
    CHECK(!register_surface(&state, 0x50000u, 0x61000u));
    CHECK(focus_span_generation_for_object(&state, 0x50000u) >
          first_generation);
    CHECK(!focus_span_should_ack_reverse(&state, 0x50000u));
    CHECK(!publish(&state, 0x61000u, &partial));
    CHECK(!publish(&state, 0x60000u, &partial));

    /* Backing reuse by another object also creates a clean generation. */
    CHECK(!register_surface(&state, 0x51000u, 0x61000u));
    CHECK(focus_span_generation_for_object(&state, 0x50000u) == 0u);
    CHECK(!publish(&state, 0x61000u, &partial));
    CHECK(!publish(&state, 0x61000u, &full));
    CHECK(publish(&state, 0x61000u, &partial));
    CHECK(focus_span_should_ack_reverse(&state, 0x51000u));
}

static void test_prepublication_bootstrap_is_generation_bound(void) {
    focus_span_state state;
    unsigned int index;
    begin_frontend(&state);
    for (index = 0u; index < 5u; ++index) {
        (void)register_surface(
            &state, 0x90000u + index * 0x100u,
            0xA0000u + index * 0x1000u);
    }
    CHECK(focus_span_should_ack_reverse(&state, 0x90400u));
    CHECK(state.bootstrap_authority_count == 1u);
    CHECK(!focus_span_should_ack_reverse(&state, 0x90000u));

    /* Reusing the raw pointer for a non-bootstrap generation revokes it. */
    CHECK(!register_surface(&state, 0x90400u, 0xB0000u));
    CHECK(!focus_span_should_ack_reverse(&state, 0x90400u));
    CHECK(state.bootstrap_authority_count == 0u);

    /* Destruction removes both lifecycle and bootstrap authority. */
    for (index = 0u; index < 3u; ++index) {
        (void)register_surface(
            &state, 0x91000u + index * 0x100u,
            0xC0000u + index * 0x1000u);
    }
    CHECK(focus_span_should_ack_reverse(&state, 0x91200u));
    CHECK(state.bootstrap_authority_count == 1u);
    CHECK(focus_span_revoke_object(&state, 0x91200u));
    CHECK(!focus_span_should_ack_reverse(&state, 0x91200u));
    CHECK(state.bootstrap_authority_count == 0u);
}

static void test_null_backing_still_orders_startup_and_cohorts(void) {
    focus_span_state state;
    unsigned int index;

    begin_frontend(&state);
    CHECK(focus_span_observe_surface(
              &state, 800u, 600u, 1600u, 2u, 0x92000u, 0u));
    CHECK(focus_span_live_count(&state) == 0u);
    for (index = 0u; index < 4u; ++index) {
        CHECK(!register_surface(
            &state, 0x93000u + index * 0x100u,
            0xD0000u + index * 0x1000u));
    }
    CHECK(focus_span_should_ack_reverse(&state, 0x93300u));
    CHECK(!focus_span_should_ack_reverse(&state, 0x93000u));
}

static void test_backing_replacement_and_destruction_revoke_authority(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect partial = {352, 253, 736, 273};
    uint32_t generation;

    begin_frontend(&state);
    CHECK(register_surface(&state, 0x70000u, 0x80000u));
    CHECK(!publish(&state, 0x80000u, &full));
    CHECK(publish(&state, 0x80000u, &partial));
    generation = focus_span_generation_for_object(&state, 0x70000u);

    CHECK(focus_span_refresh_backing(&state, 0x70000u, 0x81000u));
    CHECK(focus_span_generation_for_object(&state, 0x70000u) > generation);
    CHECK(!focus_span_should_ack_reverse(&state, 0x70000u));
    CHECK(!publish(&state, 0x80000u, &partial));
    CHECK(!publish(&state, 0x81000u, &partial));
    CHECK(!publish(&state, 0x81000u, &full));
    CHECK(publish(&state, 0x81000u, &partial));

    CHECK(focus_span_revoke_object(&state, 0x70000u));
    CHECK(focus_span_generation_for_object(&state, 0x70000u) == 0u);
    CHECK(!publish(&state, 0x81000u, &partial));
    CHECK(!focus_span_should_ack_reverse(&state, 0x70000u));
}

static void test_content_generation_retains_current_physical_authority(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect partial = {480, 537, 256, 568};
    uint32_t surface_generation;
    uint32_t content_generation;

    begin_frontend(&state);
    CHECK(register_surface(&state, 0xA1000u, 0xB1000u));
    surface_generation =
        focus_span_generation_for_object(&state, 0xA1000u);
    CHECK(!publish(&state, 0xB1000u, &full));
    CHECK(focus_span_should_ack_reverse(&state, 0xA1000u));
    CHECK(publish(&state, 0xB1000u, &partial));

    content_generation = focus_span_content_generation(&state);
    CHECK(focus_span_begin_content_generation(&state));
    CHECK(focus_span_content_generation(&state) > content_generation);

    /* A page-content boundary must not invent a new surface lifetime. */
    CHECK(focus_span_generation_for_object(&state, 0xA1000u) ==
          surface_generation);
    CHECK(focus_span_live_count(&state) == 1u);

    /* A mouse/key input epoch predicts a page change; it is not itself a GPU
       content write or a physical lifetime boundary. The complete published
       CPU plane must therefore remain authoritative until object/backing
       replacement proves a different resource generation. This prevents the
       first lock in a click handler from reverse-copying an older GPU stripe
       into the still-current CPU backing. */
    CHECK(focus_span_should_ack_reverse(&state, 0xA1000u));
    CHECK(state.generations[0].full_publication_version != 0u);
    CHECK(state.generations[0].authority_content_generation ==
          focus_span_content_generation(&state));
    CHECK(publish(&state, 0xB1000u, &partial));

    /* A complete publication by the new page supersedes the carried version
       without changing the physical surface generation. */
    CHECK(!publish(&state, 0xB1000u, &full));
    CHECK(focus_span_should_ack_reverse(&state, 0xA1000u));
    CHECK(state.generations[0].authority_content_generation ==
          focus_span_content_generation(&state));
    CHECK(publish(&state, 0xB1000u, &partial));

    /* A real reset/focus generation admits its own complete publication. */
    CHECK(focus_span_observe_surface(
              &state, 640u, 480u, 2560u, 4u, 0xC1000u, 0xD1000u) == 0);
    CHECK(register_surface(&state, 0xA2000u, 0xB2000u));
    CHECK(!publish(&state, 0xB2000u, &full));
    CHECK(focus_span_should_ack_reverse(&state, 0xA2000u));
    CHECK(publish(&state, 0xB2000u, &partial));
}

static void test_dormant_generation_requires_one_reverse_handoff(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect partial = {416, 299, 64, 302};
    uintptr_t active_object = 0xA3000u;
    uintptr_t active_backing = 0xB3000u;
    uintptr_t dormant_object = 0xA3400u;
    uintptr_t dormant_backing = 0xB7000u;
    unsigned int index;

    begin_frontend(&state);
    CHECK(register_surface(&state, active_object, active_backing));
    for (index = 1u; index < 5u; ++index) {
        CHECK(!register_surface(
            &state, active_object + index * 0x100u,
            active_backing + index * 0x1000u));
    }

    /* Generation 1 is the current composed plane. Generation 5 is the old
       R6F160 bootstrap cohort member that remains physically allocated but
       does no work through many page-content generations. */
    CHECK(!publish(&state, active_backing, &full));
    CHECK(focus_span_should_ack_reverse(&state, active_object));
    for (index = 0u; index < 54u; ++index) {
        CHECK(focus_span_begin_content_generation(&state));
        CHECK(focus_span_should_ack_reverse(&state, active_object));
        CHECK(!focus_span_should_ack_reverse(&state, dormant_object));
    }

    /* A stale full fallback from the dormant CPU plane cannot make it current.
       The first reverse must run so the newer GPU page can seed this exact
       object/backing generation. */
    CHECK(!publish(&state, dormant_backing, &full));
    CHECK(!focus_span_should_ack_reverse(&state, dormant_object));
    CHECK(focus_span_note_reverse_applied(&state, dormant_object));
    CHECK(focus_span_should_ack_reverse(&state, dormant_object));
    CHECK(!focus_span_should_ack_reverse(&state, active_object));
    CHECK(publish(&state, dormant_backing, &partial));

    /* Once handed off, ordinary R182 continuity applies to the new active
       physical generation. */
    CHECK(focus_span_begin_content_generation(&state));
    CHECK(focus_span_should_ack_reverse(&state, dormant_object));
    CHECK(!focus_span_should_ack_reverse(&state, active_object));
}

static void test_empty_content_generation_is_bounded_and_idempotent(void) {
    focus_span_state state;
    unsigned int index;

    begin_frontend(&state);
    CHECK(!focus_span_begin_content_generation(&state));
    for (index = 0u; index < 1000u; ++index) {
        CHECK(!focus_span_begin_content_generation(&state));
    }
    CHECK(focus_span_live_count(&state) == 0u);
    CHECK(focus_span_content_generation(&state) != 0u);
}

static void test_registry_is_bounded_and_reset_scoped(void) {
    focus_span_state state;
    focus_span_rect full = {0, 0, 800, 599};
    unsigned int index;

    begin_frontend(&state);
    for (index = 0u; index < MTW_FOCUS_AUTHORITY_CAPACITY + 8u; ++index) {
        (void)register_surface(
            &state, 0x100000u + index * 0x100u,
            0x200000u + index * 0x1000u);
    }
    CHECK(focus_span_live_count(&state) == MTW_FOCUS_AUTHORITY_CAPACITY);
    CHECK(focus_span_generation_for_object(&state, 0x100000u) == 0u);
    CHECK(focus_span_generation_for_object(
              &state,
              0x100000u + (MTW_FOCUS_AUTHORITY_CAPACITY + 7u) * 0x100u) != 0u);

    CHECK(!publish(
        &state,
        0x200000u + (MTW_FOCUS_AUTHORITY_CAPACITY + 7u) * 0x1000u,
        &full));
    CHECK(focus_span_live_count(&state) == MTW_FOCUS_AUTHORITY_CAPACITY);

    CHECK(focus_span_observe_surface(
              &state, 640u, 480u, 2560u, 4u, 0x90000u, 0xA0000u) == 0);
    CHECK(focus_span_live_count(&state) == 0u);
}

int main(void) {
    test_actual_publication_establishes_authority();
    test_arbitrary_partial_spans_expand();
    test_object_and_backing_reuse_create_new_generations();
    test_prepublication_bootstrap_is_generation_bound();
    test_null_backing_still_orders_startup_and_cohorts();
    test_backing_replacement_and_destruction_revoke_authority();
    test_content_generation_retains_current_physical_authority();
    test_dormant_generation_requires_one_reverse_handoff();
    test_empty_content_generation_is_bounded_and_idempotent();
    test_registry_is_bounded_and_reset_scoped();

    if (failures != 0) {
        fprintf(stderr, "%d focus authority test(s) failed\n", failures);
        return 1;
    }
    puts("focus generation authority tests passed");
    return 0;
}
