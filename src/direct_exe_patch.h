#ifndef MTW_DIRECT_EXE_PATCH_H
#define MTW_DIRECT_EXE_PATCH_H

/* Exact-hash, reversible PE32 transforms for independent Scroll, Sprite and
   campaign-tutorial options. No complete game executable is packaged. */
#include "scroll_exe_patch.h"
#include "../include/mtw_exe_variants.h"
#include "../vendor/sprite_exe/sprite_hook.inc"

#define MTW_SPRITE_ONLY_SIZE 0x4E8000u
#define MTW_SCROLL_SPRITE_SIZE 0x4EA000u
#define MTW_BLITTER_ENTRY_RAW 0x33D47Eu
#define MTW_BLITTER_BODY_RAW 0x4DE389u
#define MTW_TUTORIAL_RESET_RAW 0x306A5Cu

static const unsigned char mtw_tutorial_reset_original[6] =
    {0x89, 0x1D, 0xF0, 0xD1, 0xE7, 0x00};
/* RVA 0x306A5C -> RVA 0x306AD9, then retain the six-byte instruction span. */
static const unsigned char mtw_tutorial_reset_skip[6] =
    {0xE9, 0x78, 0x00, 0x00, 0x00, 0x90};

static inline uint32_t mtw_direct_checksum(const unsigned char *data, size_t length) {
    uint64_t sum = 0;
    size_t index;
    for (index = 0; index < length; index += 4) {
        if (index == 0xD8u) continue;
        sum += mtw_scroll_u32(data + index);
        sum = (sum & 0xFFFFFFFFULL) + (sum >> 32);
    }
    sum = (sum & 0xFFFFULL) + (sum >> 16);
    sum += sum >> 16;
    return (uint32_t)((sum & 0xFFFFULL) + length);
}

static inline int mtw_direct_strip_sprite(PatchContext *context,
                                           const unsigned char *source, size_t source_length,
                                           int scroll, unsigned char **result,
                                           size_t *result_length, PatchError *error) {
    const size_t base_length = scroll ? MTW_SCROLL_PATCHED_SIZE : MTW_SCROLL_STOCK_SIZE;
    const uint32_t section_offset = scroll ? 0x2B8u : 0x268u;
    const uint32_t section_count = scroll ? 8u : 6u;
    const uint32_t original_image_size = scroll ? 0xB4E000u : 0xB4C000u;
    const uint32_t original_checksum = scroll ? 0x4EE44Eu : 0x4F65E7u;
    const char *expected = scroll ? MTW_SCROLL_PATCHED_SHA256 : MTW_SCROLL_STOCK_SHA256;
    unsigned char *target;
    char digest[65];
    static const unsigned char entry[5] = {0xE9, 0x06, 0x5F, 0x80, 0x00};
    if (!patch_alloc(context, base_length, 1, (void **)&target, error)) return 0;
    memcpy(target, source, base_length);
    mtw_scroll_put16(target + 0x86, (uint16_t)section_count);
    mtw_scroll_put32(target + 0x9C, mtw_scroll_u32(target + 0x9C) - 0x1000u);
    mtw_scroll_put32(target + 0xD0, original_image_size);
    mtw_scroll_put32(target + 0xD8, original_checksum);
    memset(target + section_offset, 0, 40);
    memcpy(target + MTW_BLITTER_ENTRY_RAW, entry, sizeof entry);
    if (!patch_sha256_bytes(context, target, base_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (strcmp(digest, expected)) {
        patch_error_set(error, "transform_mismatch",
                        "Sprite removal did not restore the exact supported executable.", ERROR_INVALID_DATA);
        return 0;
    }
    (void)source_length;
    *result = target;
    *result_length = base_length;
    return 1;
}

static inline int mtw_direct_add_sprite(PatchContext *context,
                                         const unsigned char *base, size_t base_length,
                                         int scroll, unsigned char **result,
                                         size_t *result_length, PatchError *error) {
    const uint32_t rva = scroll ? 0xB4E000u : 0xB4C000u;
    const uint32_t raw = scroll ? 0x4E9000u : 0x4E7000u;
    const uint32_t section_offset = scroll ? 0x2B8u : 0x268u;
    const uint32_t section_count = scroll ? 8u : 6u;
    const size_t length = base_length + 0x1000u;
    const unsigned char *hook = scroll ? mtw_sprite_combined_hook_bytes : mtw_sprite_only_hook_bytes;
    const size_t hook_length = scroll ? MTW_SPRITE_COMBINED_HOOK_LENGTH : MTW_SPRITE_ONLY_HOOK_LENGTH;
    const char *expected = scroll ? MTW_SCROLL_SPRITE_SHA256 : MTW_SPRITE_ONLY_SHA256;
    static const unsigned char entry[5] = {0xE9, 0x06, 0x5F, 0x80, 0x00};
    static const unsigned char body[7] = {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x18, 0x00};
    unsigned char *target;
    char digest[65];
    if (base_length != raw ||
        memcmp(base + MTW_BLITTER_ENTRY_RAW, entry, sizeof entry) ||
        memcmp(base + MTW_BLITTER_BODY_RAW, body, sizeof body) ||
        memcmp(base + section_offset, "\0\0\0\0\0\0\0\0", 8)) {
        patch_error_set(error, "unsupported_executable",
                        "The exact blitter entry, body or PE section slot changed.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, length, 1, (void **)&target, error)) return 0;
    memset(target, 0, length);
    memcpy(target, base, base_length);
    mtw_scroll_put16(target + 0x86, (uint16_t)(section_count + 1u));
    mtw_scroll_put32(target + 0x9C, mtw_scroll_u32(target + 0x9C) + 0x1000u);
    mtw_scroll_put32(target + 0xD0, rva + 0x1000u);
    mtw_scroll_section(target + section_offset, ".mtwspr", (uint32_t)hook_length,
                       rva, raw, 0x60000020u);
    target[MTW_BLITTER_ENTRY_RAW] = 0xE9;
    mtw_scroll_put32(target + MTW_BLITTER_ENTRY_RAW + 1,
                     (0x400000u + rva) - (0x73D47Eu + 5u));
    memcpy(target + raw, hook, hook_length);
    mtw_scroll_put32(target + 0xD8, 0);
    mtw_scroll_put32(target + 0xD8, mtw_direct_checksum(target, length));
    if (!patch_sha256_bytes(context, target, length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (strcmp(digest, expected)) {
        patch_error_set(error, "transform_mismatch",
                        "The direct Sprite EXE transform did not match its pinned output hash.", ERROR_INVALID_DATA);
        return 0;
    }
    *result = target;
    *result_length = length;
    return 1;
}

static inline int mtw_direct_exe_transform_canonical(PatchContext *context,
                                            const unsigned char *source, size_t source_length,
                                            int desired_scroll, int desired_sprite,
                                            unsigned char **result, size_t *result_length,
                                            PatchError *error) {
    char source_hash[65];
    const unsigned char *base = source, *stock = source;
    unsigned char *stripped = NULL, *restored = NULL, *desired_base = NULL;
    size_t base_length = source_length, stock_length = source_length, desired_base_length = 0;
    int state;
    if (result) *result = NULL;
    if (result_length) *result_length = 0;
    if (!context || !source || !result || !result_length ||
        (desired_scroll != 0 && desired_scroll != 1) ||
        (desired_sprite != 0 && desired_sprite != 1)) {
        patch_error_set(error, "invalid_argument", "Invalid direct EXE transform arguments.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_sha256_bytes(context, source, source_length, source_hash)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (source_length == MTW_SCROLL_STOCK_SIZE && !strcmp(source_hash, MTW_SCROLL_STOCK_SHA256)) state = 0;
    else if (source_length == MTW_SCROLL_PATCHED_SIZE && !strcmp(source_hash, MTW_SCROLL_PATCHED_SHA256)) state = 1;
    else if (source_length == MTW_SPRITE_ONLY_SIZE && !strcmp(source_hash, MTW_SPRITE_ONLY_SHA256)) state = 2;
    else if (source_length == MTW_SCROLL_SPRITE_SIZE && !strcmp(source_hash, MTW_SCROLL_SPRITE_SHA256)) state = 3;
    else {
        patch_error_set(error, "unsupported_executable",
                        "Medieval_TW.exe is not one of the four exact supported direct-fix states.", ERROR_INVALID_DATA);
        return 0;
    }
    if (state >= 2) {
        if (!mtw_direct_strip_sprite(context, source, source_length, state == 3,
                                      &stripped, &base_length, error)) return 0;
        base = stripped;
    }
    if (state == 1 || state == 3) {
        if (!mtw_scroll_transform(context, base, base_length, 0,
                                  &restored, &stock_length, error)) return 0;
        stock = restored;
    } else {
        stock = base;
        stock_length = base_length;
    }
    if (!mtw_scroll_transform(context, stock, stock_length, desired_scroll,
                              &desired_base, &desired_base_length, error)) return 0;
    if (!desired_sprite) {
        *result = desired_base;
        *result_length = desired_base_length;
        return 1;
    }
    return mtw_direct_add_sprite(context, desired_base, desired_base_length,
                                  desired_scroll, result, result_length, error);
}

/* Only pinned full-file identities reach this point. A changed
   direct-fix state is built through the original verified non-LAA transform,
   then the LAA bit and checksum are restored and the result is hash-checked. */
static inline int mtw_direct_exe_transform_base(PatchContext *context,
                                            const unsigned char *source, size_t source_length,
                                            int desired_scroll, int desired_sprite,
                                            unsigned char **result, size_t *result_length,
                                            PatchError *error) {
    MtwExeVariant source_variant;
    const MtwExeVariant *canonical_variant, *output_variant;
    unsigned char *canonical, *transformed;
    size_t transformed_length;
    char digest[65];
    if (result) *result = NULL;
    if (result_length) *result_length = 0;
    if (!context || !source || !result || !result_length ||
        (desired_scroll != 0 && desired_scroll != 1) ||
        (desired_sprite != 0 && desired_sprite != 1)) {
        patch_error_set(error, "invalid_argument", "Invalid direct EXE transform arguments.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_sha256_bytes(context, source, source_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (!mtw_exe_variant_from_hash(digest, source_length, &source_variant) ||
        source_variant.tutorial) {
        patch_error_set(error, "unsupported_executable",
                        "Medieval_TW.exe is not an exact supported direct-fix or LAA state.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!source_variant.laa)
        return mtw_direct_exe_transform_canonical(context, source, source_length,
                                                   desired_scroll, desired_sprite,
                                                   result, result_length, error);
    if (source_variant.scroll == desired_scroll && source_variant.sprite == desired_sprite) {
        if (!patch_alloc(context, source_length, 1, (void **)&transformed, error)) return 0;
        memcpy(transformed, source, source_length);
        *result = transformed;
        *result_length = source_length;
        return 1;
    }
    canonical_variant = mtw_exe_variant_for_state(source_variant.scroll, source_variant.sprite, 0u, "base");
    output_variant = mtw_exe_variant_for_state((unsigned int)desired_scroll,
                                                (unsigned int)desired_sprite, 1u, "recompute");
    if (!canonical_variant || !output_variant || source_length < 0xDCu) {
        patch_error_set(error, "unsupported_executable", "The LAA state table is incomplete.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, source_length, 1, (void **)&canonical, error)) return 0;
    memcpy(canonical, source, source_length);
    canonical[0x96u] = (unsigned char)(canonical[0x96u] & ~0x20u);
    mtw_scroll_put32(canonical + 0xD8u, source_variant.canonical_checksum);
    if (!patch_sha256_bytes(context, canonical, source_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (strcmp(digest, canonical_variant->sha256) ||
        !mtw_direct_exe_transform_canonical(context, canonical, source_length,
                                             desired_scroll, desired_sprite,
                                             &transformed, &transformed_length, error)) {
        if (strcmp(digest, canonical_variant->sha256))
            patch_error_set(error, "transform_mismatch", "LAA normalization did not restore the pinned EXE.", ERROR_INVALID_DATA);
        return 0;
    }
    transformed[0x96u] = (unsigned char)(transformed[0x96u] | 0x20u);
    mtw_scroll_put32(transformed + 0xD8u, 0u);
    mtw_scroll_put32(transformed + 0xD8u, mtw_direct_checksum(transformed, transformed_length));
    if (!patch_sha256_bytes(context, transformed, transformed_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (transformed_length != output_variant->file_length || strcmp(digest, output_variant->sha256)) {
        patch_error_set(error, "transform_mismatch", "The LAA direct EXE transform did not match its pinned output.", ERROR_INVALID_DATA);
        return 0;
    }
    *result = transformed;
    *result_length = transformed_length;
    return 1;
}

/* Normalize a verified tutorial state to its exact old identity, run the
   established Scroll/Sprite transform, then apply the six-byte tutorial
   redirection. Every intermediate and final state has a pinned full hash. */
static inline int mtw_direct_exe_transform(PatchContext *context,
                                           const unsigned char *source, size_t source_length,
                                           int desired_scroll, int desired_sprite,
                                           int desired_tutorial,
                                           unsigned char **result, size_t *result_length,
                                           PatchError *error) {
    MtwExeVariant source_variant, transformed_variant;
    const MtwExeVariant *without_tutorial, *wanted;
    unsigned char *normalized = NULL, *transformed = NULL;
    const unsigned char *input = source;
    size_t transformed_length = 0;
    char digest[65];
    if (result) *result = NULL;
    if (result_length) *result_length = 0;
    if (!context || !source || !result || !result_length ||
        (desired_scroll != 0 && desired_scroll != 1) ||
        (desired_sprite != 0 && desired_sprite != 1) ||
        (desired_tutorial != 0 && desired_tutorial != 1) ||
        source_length < MTW_TUTORIAL_RESET_RAW + sizeof mtw_tutorial_reset_original) {
        patch_error_set(error, "invalid_argument", "Invalid direct EXE transform arguments.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_sha256_bytes(context, source, source_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (!mtw_exe_variant_from_hash(digest, source_length, &source_variant)) {
        patch_error_set(error, "unsupported_executable", "The game EXE is not an exact supported state.", ERROR_INVALID_DATA);
        return 0;
    }
    if (source_variant.scroll == desired_scroll &&
        source_variant.sprite == desired_sprite &&
        source_variant.tutorial == desired_tutorial) {
        if (!patch_alloc(context, source_length, 1, (void **)&normalized, error)) return 0;
        memcpy(normalized, source, source_length);
        *result = normalized;
        *result_length = source_length;
        return 1;
    }
    if (source_variant.tutorial) {
        without_tutorial = mtw_exe_variant_for_state(
            source_variant.scroll, source_variant.sprite,
            source_variant.laa, source_variant.checksum_style);
        if (!without_tutorial ||
            memcmp(source + MTW_TUTORIAL_RESET_RAW, mtw_tutorial_reset_skip,
                   sizeof mtw_tutorial_reset_skip)) {
            patch_error_set(error, "transform_mismatch", "The tutorial EXE instruction is not pinned.", ERROR_INVALID_DATA);
            goto failed;
        }
        if (!patch_alloc(context, source_length, 1, (void **)&normalized, error)) goto failed;
        memcpy(normalized, source, source_length);
        memcpy(normalized + MTW_TUTORIAL_RESET_RAW, mtw_tutorial_reset_original,
               sizeof mtw_tutorial_reset_original);
        if (!strcmp(source_variant.checksum_style, "zero"))
            mtw_scroll_put32(normalized + 0xD8u, 0u);
        else if (!strcmp(source_variant.checksum_style, "recompute")) {
            mtw_scroll_put32(normalized + 0xD8u, 0u);
            mtw_scroll_put32(normalized + 0xD8u,
                             mtw_direct_checksum(normalized, source_length));
        } else
            mtw_scroll_put32(normalized + 0xD8u,
                             without_tutorial->canonical_checksum);
        if (!patch_sha256_bytes(context, normalized, source_length, digest)) {
            patch_error_set(error, context->error.code, context->error.message, context->error.win32);
            goto failed;
        }
        if (strcmp(digest, without_tutorial->sha256)) {
            patch_error_set(error, "transform_mismatch", "Tutorial removal did not restore a pinned executable.", ERROR_INVALID_DATA);
            goto failed;
        }
        input = normalized;
    }
    if (!mtw_direct_exe_transform_base(context, input, source_length,
                                       desired_scroll, desired_sprite,
                                       &transformed, &transformed_length, error)) goto failed;
    if (normalized) patch_context_free(context, normalized);
    normalized = NULL;
    if (!desired_tutorial) {
        if (source_variant.tutorial && source_variant.laa) {
            wanted = mtw_exe_variant_for_state(
                (unsigned int)desired_scroll, (unsigned int)desired_sprite,
                1u, "recompute");
            mtw_scroll_put32(transformed + 0xD8u, 0u);
            mtw_scroll_put32(transformed + 0xD8u,
                             mtw_direct_checksum(transformed, transformed_length));
            if (!wanted || transformed_length != wanted->file_length ||
                !patch_sha256_bytes(context, transformed, transformed_length, digest) ||
                strcmp(digest, wanted->sha256)) {
                patch_error_set(error, "transform_mismatch", "Tutorial removal did not match its pinned LAA output.", ERROR_INVALID_DATA);
                goto failed;
            }
        }
        *result = transformed;
        *result_length = transformed_length;
        return 1;
    }
    if (memcmp(transformed + MTW_TUTORIAL_RESET_RAW, mtw_tutorial_reset_original,
               sizeof mtw_tutorial_reset_original) ||
        !patch_sha256_bytes(context, transformed, transformed_length, digest)) {
        patch_error_set(error, "transform_mismatch", "The tutorial reset instruction changed.", ERROR_INVALID_DATA);
        goto failed;
    }
    if (!mtw_exe_variant_from_hash(digest, transformed_length, &transformed_variant) ||
        transformed_variant.tutorial) {
        patch_error_set(error, "transform_mismatch", "The intermediate EXE is not a pinned state.", ERROR_INVALID_DATA);
        goto failed;
    }
    memcpy(transformed + MTW_TUTORIAL_RESET_RAW, mtw_tutorial_reset_skip,
           sizeof mtw_tutorial_reset_skip);
    mtw_scroll_put32(transformed + 0xD8u, 0u);
    mtw_scroll_put32(transformed + 0xD8u,
                     mtw_direct_checksum(transformed, transformed_length));
    wanted = mtw_exe_variant_for_options(
        (unsigned int)desired_scroll, (unsigned int)desired_sprite, 1u,
        source_variant.laa, source_variant.laa ? "recompute" : "base");
    if (!wanted || transformed_length != wanted->file_length ||
        !patch_sha256_bytes(context, transformed, transformed_length, digest)) {
        patch_error_set(error, "transform_mismatch", "The tutorial EXE output identity is unavailable.", ERROR_INVALID_DATA);
        goto failed;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "The tutorial EXE did not match its pinned output.", ERROR_INVALID_DATA);
        goto failed;
    }
    *result = transformed;
    *result_length = transformed_length;
    return 1;
failed:
    if (normalized) patch_context_free(context, normalized);
    if (transformed) patch_context_free(context, transformed);
    return 0;
}

/* Rebase a verified pre-patch stock snapshot after a user independently sets
   LAA on a managed EXE. The caller stages this exact verified result as the
   new original in the same transaction as the repaired receipt. */
static inline int mtw_direct_laa_stock_from_base(PatchContext *context,
                                                  const unsigned char *source, size_t source_length,
                                                  const char *checksum_style,
                                                  unsigned char **result, size_t *result_length,
                                                  PatchError *error) {
    const MtwExeVariant *wanted = mtw_exe_variant_for_state(0u, 0u, 1u, checksum_style);
    unsigned char *copy;
    char digest[65];
    if (result) *result = NULL;
    if (result_length) *result_length = 0;
    if (!context || !source || !result || !result_length || !wanted ||
        source_length != MTW_SCROLL_STOCK_SIZE) {
        patch_error_set(error, "invalid_argument", "Invalid LAA original snapshot request.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_sha256_bytes(context, source, source_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (strcmp(digest, MTW_SCROLL_STOCK_SHA256)) {
        patch_error_set(error, "unsupported_executable", "The previous original is not the exact supported stock EXE.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, source_length, 1, (void **)&copy, error)) return 0;
    memcpy(copy, source, source_length);
    copy[0x96u] = (unsigned char)(copy[0x96u] | 0x20u);
    if (!strcmp(checksum_style, "zero"))
        mtw_scroll_put32(copy + 0xD8u, 0u);
    else if (!strcmp(checksum_style, "recompute")) {
        mtw_scroll_put32(copy + 0xD8u, 0u);
        mtw_scroll_put32(copy + 0xD8u, mtw_direct_checksum(copy, source_length));
    }
    if (!patch_sha256_bytes(context, copy, source_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "The LAA original did not match its pinned identity.", ERROR_INVALID_DATA);
        return 0;
    }
    *result = copy;
    *result_length = source_length;
    return 1;
}

#endif
