#ifndef MTW_DUST_DRAW_IDENTITY_H
#define MTW_DUST_DRAW_IDENTITY_H

#include <stdint.h>
#include <string.h>

/* The supported executable's dust renderer reaches DrawIndexedPrimitiveUP
 * from this return site in both observed diffuse-color phases. The gray
 * phase is admitted only at that site; the brown phase keeps its established
 * classifier so existing game paths retain their behavior. */
#define MTW_DUST_DRAW_RETURN_RVA 0x00B3FDC7u
/* This is the direct return of the game's DrawIndexedPrimitiveUP thunk.
 * Its outer particle-manager caller (RVA 1B9F0D) appears in some stack
 * walks, but is not the return address read by the proxy selector. */
#define MTW_DUST_BROWN_RGB 0x0094771Fu
#define MTW_DUST_GRAY_RGB 0x00202020u
#define MTW_DUST_NONE 0
#define MTW_DUST_BROWN_PHASE 1
#define MTW_DUST_GRAY_PHASE 2
#define MTW_DUST_MIXED_PHASE 3

static inline int mtw_dust_color_phase(uint32_t diffuse, uintptr_t caller,
                                       uintptr_t game_image) {
    const uint32_t rgb = diffuse & 0x00FFFFFFu;
    if (rgb == MTW_DUST_BROWN_RGB) return MTW_DUST_BROWN_PHASE;
    if (rgb == MTW_DUST_GRAY_RGB && game_image != 0u &&
        caller >= game_image &&
        caller - game_image == MTW_DUST_DRAW_RETURN_RVA)
        return MTW_DUST_GRAY_PHASE;
    return MTW_DUST_NONE;
}

/* Diffuse lives at byte 16 of each 32-byte transformed vertex. A billboard
 * has five vertices; only its center's diffuse identifies that particle.
 * The observed mixed game callsite must be checked across the draw, not just
 * its first (sometimes black or white) billboard. The caller has already
 * verified primitive shape, FVF, and texture before this scan. */
static inline int mtw_dust_batch_phase(const void *vertices,
                                       uint32_t vertex_count, uint32_t stride,
                                       uintptr_t caller, uintptr_t game_image) {
    if (!vertices || stride != 32u || vertex_count < 5u ||
        vertex_count % 5u != 0u ||
        vertex_count > (SIZE_MAX - 16u) / stride)
        return MTW_DUST_NONE;
    const uint8_t *bytes = (const uint8_t *)vertices;
    uint32_t diffuse = 0u;
    memcpy(&diffuse, bytes + 16u, sizeof(diffuse));
    const int first_phase = mtw_dust_color_phase(diffuse, caller, game_image);
    if (first_phase == MTW_DUST_BROWN_PHASE) return first_phase;
    if (game_image == 0u || caller < game_image ||
        caller - game_image != MTW_DUST_DRAW_RETURN_RVA)
        return first_phase;
    const uint32_t quads = vertex_count / 5u;
    for (uint32_t quad = 1u; quad < quads; ++quad) {
        const size_t offset = (size_t)quad * 5u * stride + 16u;
        memcpy(&diffuse, bytes + offset, sizeof(diffuse));
        if ((diffuse & 0x00FFFFFFu) == MTW_DUST_BROWN_RGB)
            return MTW_DUST_MIXED_PHASE;
    }
    return first_phase;
}

/* The calibrated native-step work model was fitted with the brown phase as
 * dust and the gray phase as non-dust. Preserve that model when repairing the
 * graphics path, so this change cannot alter particle simulation cadence. */
static inline int mtw_dust_counts_as_reference_work(int phase) {
    return phase == MTW_DUST_BROWN_PHASE || phase == MTW_DUST_MIXED_PHASE;
}

#endif
