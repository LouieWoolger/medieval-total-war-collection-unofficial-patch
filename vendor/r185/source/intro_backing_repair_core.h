#ifndef MTW_INTRO_BACKING_REPAIR_CORE_H
#define MTW_INTRO_BACKING_REPAIR_CORE_H

#include <stdint.h>
#include <string.h>

/* Supported PE32 Medieval_TW.exe. These sites are inside the game's single
 * primary Lock/recovery function. Both allocator calls own the same backing
 * global; both epilogs precede any caller that uses the active pointer. */
#define MTW_INTRO_ALLOC_FIRST_RVA 0x00012048u
#define MTW_INTRO_ALLOC_SECOND_RVA 0x00012149u
#define MTW_INTRO_RETURN_FIRST_RVA 0x00012076u
#define MTW_INTRO_RETURN_SECOND_RVA 0x0001219Cu
#define MTW_INTRO_FIRST_PITCH_RVA 0x00012061u
#define MTW_INTRO_SECOND_PITCH_RVA 0x00012160u
#define MTW_INTRO_ALLOCATOR_RVA 0x0036D20Du
#define MTW_INTRO_DEALLOCATOR_RVA 0x00361AF7u
#define MTW_INTRO_BACKING_SITE_SIZE 5u

static const unsigned char mtw_intro_alloc_first_expected[5] = {
    0xE8u, 0xC0u, 0xB1u, 0x35u, 0x00u
};
static const unsigned char mtw_intro_alloc_second_expected[5] = {
    0xE8u, 0xBFu, 0xB0u, 0x35u, 0x00u
};
static const unsigned char mtw_intro_return_expected[5] = {
    0x5Bu, 0x83u, 0xC4u, 0x7Cu, 0xC3u
};
static const unsigned char mtw_intro_allocator_expected[6] = {
    0x6Au, 0x01u, 0xFFu, 0x74u, 0x24u, 0x08u
};
static const unsigned char mtw_intro_deallocator_expected[4] = {
    0xFFu, 0x74u, 0x24u, 0x04u
};
static const unsigned char mtw_intro_first_pitch_expected[7] = {
    0x0Fu, 0xAFu, 0x0Du, 0xB4u, 0x01u, 0x8Du, 0x00u
};
static const unsigned char mtw_intro_second_pitch_expected[7] = {
    0x0Fu, 0xAFu, 0x0Du, 0xB4u, 0x01u, 0x8Du, 0x00u
};

typedef struct mtw_intro_backing_tracker {
    uintptr_t pointer;
    uint32_t capacity_bytes;
} mtw_intro_backing_tracker;

typedef struct mtw_intro_backing_snapshot {
    uintptr_t backing;
    uintptr_t active;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} mtw_intro_backing_snapshot;

typedef struct mtw_intro_backing_plan {
    uint32_t row_bytes;
    uint32_t required_bytes;
    int fix_pitch;
    int grow;
} mtw_intro_backing_plan;

static inline int mtw_intro_backing_site_matches(
    const unsigned char *current, const unsigned char *expected,
    uint32_t size) {
    return current != NULL && expected != NULL && size != 0u &&
           memcmp(current, expected, size) == 0;
}

static inline void mtw_intro_backing_record_allocation(
    mtw_intro_backing_tracker *tracker, uintptr_t pointer,
    uint32_t capacity_bytes) {
    if (tracker == NULL) return;
    tracker->pointer = pointer != 0u && capacity_bytes != 0u ? pointer : 0u;
    tracker->capacity_bytes = tracker->pointer != 0u ? capacity_bytes : 0u;
}

static inline void mtw_intro_backing_reconcile(
    mtw_intro_backing_tracker *tracker, uintptr_t backing) {
    if (tracker != NULL && tracker->pointer != backing) {
        tracker->pointer = 0u;
        tracker->capacity_bytes = 0u;
    }
}

/* Accept only the two known fallback pitch forms: one row or the whole
 * allocation. A normal successful surface Lock has active != backing. */
static inline int mtw_intro_backing_make_plan(
    const mtw_intro_backing_tracker *tracker,
    const mtw_intro_backing_snapshot *surface,
    mtw_intro_backing_plan *plan) {
    uint32_t row_bytes;
    uint32_t required_bytes;
    mtw_intro_backing_plan next;

    if (tracker == NULL || surface == NULL || plan == NULL ||
        tracker->pointer == 0u || tracker->capacity_bytes == 0u ||
        tracker->pointer != surface->backing ||
        surface->active != surface->backing ||
        surface->width == 0u || surface->height == 0u ||
        surface->width > UINT32_MAX / 2u) {
        return 0;
    }
    row_bytes = surface->width * 2u;
    if (surface->height > UINT32_MAX / row_bytes) return 0;
    required_bytes = row_bytes * surface->height;
    if (surface->pitch != row_bytes &&
        surface->pitch != required_bytes) {
        return 0;
    }
    next.row_bytes = row_bytes;
    next.required_bytes = required_bytes;
    next.fix_pitch = surface->pitch != row_bytes;
    next.grow = required_bytes > tracker->capacity_bytes;
    *plan = next;
    return 1;
}

/* Pure decision/commit step used after allocation and a fresh state read.
 * On rejection, all inputs and old_pointer_out remain untouched. */
static inline int mtw_intro_backing_commit_growth(
    mtw_intro_backing_tracker *tracker,
    mtw_intro_backing_snapshot *surface,
    uintptr_t replacement, uintptr_t *old_pointer_out) {
    mtw_intro_backing_plan plan;
    uintptr_t old_pointer;
    if (tracker == NULL || surface == NULL || old_pointer_out == NULL ||
        replacement == 0u || replacement == tracker->pointer ||
        !mtw_intro_backing_make_plan(tracker, surface, &plan) ||
        !plan.grow) {
        return 0;
    }
    old_pointer = tracker->pointer;
    tracker->pointer = replacement;
    tracker->capacity_bytes = plan.required_bytes;
    surface->backing = replacement;
    surface->active = replacement;
    surface->pitch = plan.row_bytes;
    *old_pointer_out = old_pointer;
    return 1;
}

#endif
