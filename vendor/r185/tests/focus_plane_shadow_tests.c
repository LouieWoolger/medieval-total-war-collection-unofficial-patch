#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/focus_plane_shadow_core.h"

static int failures;
static unsigned char storage[MTW_FOCUS_PLANE_HISTORY_BYTES];
static unsigned char page_seed[MTW_FOCUS_PLANE_BYTES];

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression);  \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static void commit_full(focus_plane_shadow_state *state,
                        int entry_index,
                        const focus_span_rect *full) {
    focus_plane_shadow_plan plan;
    CHECK(focus_plane_shadow_make_plan(
              state, entry_index, 1600u, 2u, full, &plan));
    CHECK(plan.seed_full);
    CHECK(!plan.had_valid_base);
    CHECK(plan.offset == 0u);
    CHECK(plan.length == MTW_FOCUS_PLANE_BYTES);
    CHECK(focus_plane_shadow_bytes(state, &plan) != NULL);
    focus_plane_shadow_commit(state, &plan);
}

int main(void) {
    focus_plane_shadow_state state;
    focus_plane_shadow_plan plan;
    focus_span_rect full = {0, 0, 800, 599};
    focus_span_rect partial = {320, 38, 544, 71};
    focus_span_rect wrapped_middle = {320, 166, 224, 481};
    focus_span_rect wrapped_lower = {480, 537, 256, 568};
    focus_span_rect observed_r180 = {416, 299, 64, 302};
    focus_span_rect reverse_vertical = {64, 302, 416, 299};
    int first;
    int second;
    unsigned int index;

    memset(storage, 0x5A, sizeof(storage));
    focus_plane_shadow_initialize(&state, storage, sizeof(storage));

    first = focus_plane_shadow_select(
        &state, 4u, 7u, (uintptr_t)0x16000000u);
    CHECK(first >= 0);
    CHECK(!focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &partial, &plan));
    commit_full(&state, first, &full);

    /* A physical history may survive a page boundary, but prior-page bytes
       are not a publishable base for the new content generation. */
    CHECK(focus_plane_shadow_select(
              &state, 5u, 7u, (uintptr_t)0x16000000u) == first);
    CHECK(!focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &partial, &plan));
    CHECK(focus_plane_shadow_needs_content_seed(&state, first, 5u));
    CHECK(!focus_plane_shadow_can_seed_generation(
        &state, first, 4u, 1600u, 2u, MTW_FOCUS_PLANE_BYTES));
    CHECK(!focus_plane_shadow_can_seed_generation(
        &state, first, 5u, 1598u, 2u, MTW_FOCUS_PLANE_BYTES));
    CHECK(!focus_plane_shadow_can_seed_generation(
        &state, first, 5u, 1600u, 2u,
        MTW_FOCUS_PLANE_BYTES - 2u));
    CHECK(focus_plane_shadow_can_seed_generation(
        &state, first, 5u, 1600u, 2u, MTW_FOCUS_PLANE_BYTES));
    CHECK(!focus_plane_shadow_seed_content(
        &state, first, 4u, page_seed, sizeof(page_seed)));
    memset(page_seed, 0xA7, sizeof(page_seed));
    CHECK(focus_plane_shadow_seed_content(
        &state, first, 5u, page_seed, sizeof(page_seed)));
    CHECK(!focus_plane_shadow_needs_content_seed(&state, first, 5u));
    CHECK(focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &partial, &plan));
    CHECK(!plan.seed_full);
    CHECK(plan.had_valid_base);
    CHECK(!plan.carried_from_prior_content);

    /* R180 proves dgVoodoo's horizontal wrap denotes one forward row-major
       dirty byte interval. It is safe only because this entry was seeded from
       a complete backing in the current content generation. */
    CHECK(focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &wrapped_middle, &plan));
    CHECK(plan.offset == 266240u);
    CHECK(plan.length == 503808u);
    CHECK(focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &wrapped_lower, &plan));
    CHECK(plan.offset == 860160u);
    CHECK(plan.length == 49152u);
    CHECK(focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &observed_r180, &plan));
    CHECK(plan.offset == 479232u);
    CHECK(plan.length == 4096u);
    CHECK(!focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &reverse_vertical, &plan));
    CHECK(focus_plane_shadow_bytes(&state, &plan)[0] == 0xA7);
    CHECK(focus_plane_shadow_bytes(&state, &plan)[
              MTW_FOCUS_PLANE_BYTES - 1u] == 0xA7);
    focus_plane_shadow_commit(&state, &plan);
    CHECK(focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &partial, &plan));
    CHECK(plan.had_valid_base);
    CHECK(!plan.carried_from_prior_content);

    /* Interleaved physical surfaces keep independent composition histories. */
    second = focus_plane_shadow_select(
        &state, 5u, 8u, (uintptr_t)0x17000000u);
    CHECK(second >= 0 && second != first);
    CHECK(!focus_plane_shadow_make_plan(
        &state, second, 1600u, 2u, &partial, &plan));
    commit_full(&state, second, &full);
    CHECK(focus_plane_shadow_select(
              &state, 5u, 7u, (uintptr_t)0x16000000u) == first);
    CHECK(focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &partial, &plan));
    CHECK(plan.had_valid_base);

    /* Pointer/backing reuse is not identity reuse. */
    CHECK(focus_plane_shadow_select(
              &state, 6u, 9u, (uintptr_t)0x16000000u) != first);
    CHECK(!focus_plane_shadow_make_plan(
        &state,
        focus_plane_shadow_select(
            &state, 6u, 9u, (uintptr_t)0x16000000u),
        1600u, 2u, &partial, &plan));

    /* The registry is bounded and evicts by physical identity/age, never by
       page name or constructor ordinal. An evicted identity cannot inherit
       stale bytes when it reappears. */
    for (index = 0u; index < MTW_FOCUS_PLANE_HISTORY_CAPACITY + 2u; ++index) {
        int slot = focus_plane_shadow_select(
            &state, 10u + index, 100u + index,
            (uintptr_t)(0x20000000u + index * 0x10000u));
        CHECK(slot >= 0);
        commit_full(&state, slot, &full);
    }
    CHECK(focus_plane_shadow_live_count(&state) ==
          MTW_FOCUS_PLANE_HISTORY_CAPACITY);
    first = focus_plane_shadow_select(
        &state, 99u, 7u, (uintptr_t)0x16000000u);
    CHECK(first >= 0);
    CHECK(!focus_plane_shadow_make_plan(
        &state, first, 1600u, 2u, &partial, &plan));

    if (failures != 0) {
        fprintf(stderr, "%d composition-history test(s) failed\n", failures);
        return 1;
    }
    puts("bounded composition-history tests passed");
    return 0;
}
