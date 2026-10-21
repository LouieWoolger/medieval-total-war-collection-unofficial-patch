#ifndef MTW_KEY_EXE_PATCH_H
#define MTW_KEY_EXE_PATCH_H

/* Last independent PE32 section transform for the optional keybinding fix.
   The complete input and output identities are pinned. */
#include "../vendor/key_exe/key_hook.inc"

#define MTW_KEY_PAGE 0x1000u
#define MTW_KEY_GHOST_OFFSET 0x200u
#define MTW_KEY_HELD_RAW 0xD5D80u
#define MTW_KEY_HELD_VA 0x4D5D80u
#define MTW_KEY_GHOST_RAW 0xCC040u
#define MTW_KEY_GHOST_VA 0x4CC040u
#define MTW_KEY_SPECIAL_RAW 0xD585Cu
#define MTW_KEY_BACKSPACE_FLAG_RAW 0x391552u
#define MTW_KEY_SPACE_FLAG_RAW 0x391551u
#define MTW_KEY_DELETE_FLAG_68_RAW 0x391531u
#define MTW_KEY_DELETE_FLAG_106_RAW 0x391557u
#define MTW_KEY_DISPATCH_RAW 0xC8E56u
#define MTW_KEY_DISPATCH_VA 0x4C8E56u
#define MTW_KEY_DISPATCH_OFFSET 0x400u
#define MTW_KEY_LABEL_RAW 0x31238Eu
#define MTW_KEY_LABEL_VA 0x71238Eu
#define MTW_KEY_LABEL_OFFSET 0x700u
#define MTW_KEY_COLLISION_RAW 0x3116F0u
static const unsigned char mtw_key_extra_fixed_slots[] = {
    74u, 75u, 78u, 79u, 80u, 81u, 82u, 83u, 84u, 85u, 86u, 87u,
    88u, 89u, 90u, 91u, 92u, 93u, 94u, 95u, 96u, 97u,
    103u, 104u, 105u, 107u, 108u, 109u, 110u
};
static const unsigned char mtw_key_original_collision[] = {0x8B, 0x14, 0x8D, 0x38, 0x94, 0xC7, 0x00};
static const unsigned char mtw_key_original_held[] = {0xA1, 0xB0, 0x29, 0xB4, 0x00};
static const unsigned char mtw_key_original_ghost[] = {0x8A, 0x41, 0x04, 0x84, 0xC0};
static const unsigned char mtw_key_original_dispatch[] = {0xBE, 0x70, 0x00, 0x00, 0x00, 0xC7, 0x45, 0x14, 0x34, 0x83, 0xE7, 0x00};
static const unsigned char mtw_key_original_label[] = {0xFF, 0x15, 0xE0, 0x42, 0x78, 0x00};
static const unsigned char mtw_key_original_special[] = {0xA1, 0x88, 0x81, 0xE7, 0x00, 0x25, 0xFF, 0xFF, 0x00, 0x00, 0x50, 0xFF, 0x15, 0xC0, 0x42, 0x78, 0x00, 0x66, 0x8B, 0xD8, 0xC1, 0xEB, 0x0F, 0x80, 0xE3, 0x01};
static const unsigned char mtw_key_special_patch[] = {0x6A, 0x05, 0xE8, 0x1D, 0x05, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x89, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

static inline int mtw_key_payload(uint32_t rva,
                                  const unsigned char **payload, size_t *length) {
    if (!payload || !length) return 0;
    switch (rva) {
    case 0xB4C000u: *payload = mtw_key_hook_b4c000; *length = MTW_KEY_HOOK_B4C000_LENGTH; return 1;
    case 0xB4D000u: *payload = mtw_key_hook_b4d000; *length = MTW_KEY_HOOK_B4D000_LENGTH; return 1;
    case 0xB4E000u: *payload = mtw_key_hook_b4e000; *length = MTW_KEY_HOOK_B4E000_LENGTH; return 1;
    case 0xB4F000u: *payload = mtw_key_hook_b4f000; *length = MTW_KEY_HOOK_B4F000_LENGTH; return 1;
    case 0xB50000u: *payload = mtw_key_hook_b50000; *length = MTW_KEY_HOOK_B50000_LENGTH; return 1;
    case 0xB51000u: *payload = mtw_key_hook_b51000; *length = MTW_KEY_HOOK_B51000_LENGTH; return 1;
    default: return 0;
    }
}

static inline void mtw_key_redirect(unsigned char out[5], uint32_t site_va, uint32_t destination_va) {
    int32_t relative = (int32_t)((int64_t)destination_va - (site_va + 5u));
    out[0] = 0xE9;
    mtw_scroll_put32(out + 1, (uint32_t)relative);
}

static inline void mtw_key_dispatch_redirect(unsigned char out[12], uint32_t image_size) {
    memset(out, 0x90, sizeof mtw_key_original_dispatch);
    mtw_key_redirect(out, MTW_KEY_DISPATCH_VA,
                     0x400000u + image_size + MTW_KEY_DISPATCH_OFFSET);
}

static inline void mtw_key_label_redirect(unsigned char out[6], uint32_t image_size) {
    mtw_key_redirect(out, MTW_KEY_LABEL_VA,
                     0x400000u + image_size + MTW_KEY_LABEL_OFFSET);
    out[0] = 0xE8u; /* CALL preserves the original GetKeyNameTextA result path. */
    out[5] = 0x90u;
}

static inline int mtw_key_extra_flags_match(const unsigned char *source, unsigned char expected) {
    size_t i;
    for (i = 0u; i < sizeof mtw_key_extra_fixed_slots; ++i)
        if (source[0x39152Cu + mtw_key_extra_fixed_slots[i] - 63u] != expected) return 0;
    return 1;
}

static inline void mtw_key_extra_flags_set(unsigned char *target, unsigned char value) {
    size_t i;
    for (i = 0u; i < sizeof mtw_key_extra_fixed_slots; ++i)
        target[0x39152Cu + mtw_key_extra_fixed_slots[i] - 63u] = value;
}

static inline int mtw_key_layout(const unsigned char *source, size_t length,
                                 const MtwExeVariant *variant, uint32_t *count,
                                 uint32_t *image_size, uint32_t *slot) {
    uint32_t expected_count, expected_size;
    if (!source || !variant || !count || !image_size || !slot || length < 0x1000u ||
        source[0] != 'M' || source[1] != 'Z' || mtw_scroll_u32(source + 0x3Cu) != 0x80u ||
        memcmp(source + 0x80u, "PE\0\0", 4u) ||
        mtw_scroll_u32(source + 0x84u) % 0x10000u != 0x14Cu ||
        mtw_scroll_u32(source + 0x98u) % 0x10000u != 0x10Bu ||
        mtw_scroll_u32(source + 0xB4u) != 0x400000u ||
        mtw_scroll_u32(source + 0xB8u) != 0x1000u ||
        mtw_scroll_u32(source + 0xBCu) != 0x1000u ||
        mtw_scroll_u32(source + 0xD4u) != 0x1000u) return 0;
    expected_count = 6u + 2u * variant->scroll + variant->sprite +
                     variant->ui + variant->economy + variant->key;
    expected_size = 0xB4C000u + 0x2000u * variant->scroll +
                    0x1000u * variant->sprite + 0x1000u * variant->ui +
                    0x1000u * variant->economy + 0x1000u * variant->key;
    if ((uint32_t)(source[0x86u] | ((uint32_t)source[0x87u] << 8)) != expected_count ||
        mtw_scroll_u32(source + 0xD0u) != expected_size ||
        length != 0x4E7000u + 0x2000u * variant->scroll +
                  0x1000u * variant->sprite + 0x1000u * variant->ui +
                  0x1000u * variant->economy + 0x1000u * variant->key ||
        0x178u + (expected_count + (variant->key ? 0u : 1u)) * 40u > 0x1000u)
        return 0;
    *count = expected_count;
    *image_size = expected_size;
    *slot = 0x178u + (expected_count - variant->key) * 40u;
    return 1;
}

static inline int mtw_key_add(PatchContext *context,
                              const unsigned char *source, size_t length,
                              const MtwExeVariant *variant,
                              unsigned char **result, size_t *result_length,
                              PatchError *error) {
    const MtwExeVariant *wanted;
    const unsigned char *payload;
    unsigned char *target, held_jump[5], ghost_jump[5], dispatch_jump[12], label_call[6];
    size_t payload_length;
    uint32_t count, image_size, slot;
    char digest[65];
    static const unsigned char empty_slot[40] = {0};
    if (!variant || variant->key ||
        !mtw_key_layout(source, length, variant, &count, &image_size, &slot) ||
        !mtw_key_payload(image_size, &payload, &payload_length) ||
        memcmp(source + slot, empty_slot, 40u) ||
        memcmp(source + MTW_KEY_COLLISION_RAW, mtw_key_original_collision, sizeof mtw_key_original_collision) ||
        memcmp(source + MTW_KEY_HELD_RAW, mtw_key_original_held, sizeof mtw_key_original_held) ||
        memcmp(source + MTW_KEY_GHOST_RAW, mtw_key_original_ghost, sizeof mtw_key_original_ghost) ||
        memcmp(source + MTW_KEY_DISPATCH_RAW, mtw_key_original_dispatch, sizeof mtw_key_original_dispatch) ||
        memcmp(source + MTW_KEY_LABEL_RAW, mtw_key_original_label, sizeof mtw_key_original_label) ||
        memcmp(source + MTW_KEY_SPECIAL_RAW, mtw_key_original_special, sizeof mtw_key_original_special) ||
        source[MTW_KEY_BACKSPACE_FLAG_RAW] != 0u ||
        source[MTW_KEY_SPACE_FLAG_RAW] != 0u ||
        source[MTW_KEY_DELETE_FLAG_68_RAW] != 0u ||
        source[MTW_KEY_DELETE_FLAG_106_RAW] != 0u ||
        !mtw_key_extra_flags_match(source, 0u)) {
        patch_error_set(error, "unsupported_executable", "The key section slot or instructions changed.", ERROR_INVALID_DATA);
        return 0;
    }
    wanted = mtw_exe_variant_for_options_key(
        variant->scroll, variant->sprite, variant->tutorial, variant->laa,
        variant->checksum_style, variant->ui, variant->economy, 1u);
    if (!wanted || wanted->file_length != length + MTW_KEY_PAGE ||
        !patch_alloc(context, wanted->file_length, 1, (void **)&target, error)) return 0;
    memset(target, 0, wanted->file_length);
    memcpy(target, source, length);
    mtw_scroll_put16(target + 0x86u, (uint16_t)(count + 1u));
    mtw_scroll_put32(target + 0x9Cu, mtw_scroll_u32(source + 0x9Cu) + MTW_KEY_PAGE);
    mtw_scroll_put32(target + 0xD0u, image_size + MTW_KEY_PAGE);
    mtw_scroll_section(target + slot, ".mtwkey", (uint32_t)payload_length,
                       image_size, (uint32_t)length, 0x60000020u);
    memcpy(target + length, payload, payload_length);
    mtw_key_redirect(held_jump, MTW_KEY_HELD_VA, 0x400000u + image_size);
    mtw_key_redirect(ghost_jump, MTW_KEY_GHOST_VA, 0x400000u + image_size + MTW_KEY_GHOST_OFFSET);
    mtw_key_dispatch_redirect(dispatch_jump, image_size);
    mtw_key_label_redirect(label_call, image_size);
    memcpy(target + MTW_KEY_HELD_RAW, held_jump, sizeof held_jump);
    memcpy(target + MTW_KEY_GHOST_RAW, ghost_jump, sizeof ghost_jump);
    memcpy(target + MTW_KEY_DISPATCH_RAW, dispatch_jump, sizeof dispatch_jump);
    memcpy(target + MTW_KEY_LABEL_RAW, label_call, sizeof label_call);
    memcpy(target + MTW_KEY_SPECIAL_RAW, mtw_key_special_patch, sizeof mtw_key_special_patch);
    target[MTW_KEY_BACKSPACE_FLAG_RAW] = 1u;
    target[MTW_KEY_SPACE_FLAG_RAW] = 1u;
    target[MTW_KEY_DELETE_FLAG_68_RAW] = 1u;
    target[MTW_KEY_DELETE_FLAG_106_RAW] = 1u;
    mtw_key_extra_flags_set(target, 1u);
    if (!strcmp(variant->checksum_style, "base") ||
        !strcmp(variant->checksum_style, "recompute")) {
        mtw_scroll_put32(target + 0xD8u, 0u);
        mtw_scroll_put32(target + 0xD8u, mtw_direct_checksum(target, wanted->file_length));
    } else if (!strcmp(variant->checksum_style, "zero"))
        mtw_scroll_put32(target + 0xD8u, 0u);
    if (!patch_sha256_bytes(context, target, wanted->file_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        patch_context_free(context, target);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "The key executable did not match its pinned output.", ERROR_INVALID_DATA);
        patch_context_free(context, target);
        return 0;
    }
    *result = target;
    *result_length = wanted->file_length;
    return 1;
}

static inline int mtw_key_strip(PatchContext *context,
                                const unsigned char *source, size_t length,
                                const MtwExeVariant *variant,
                                unsigned char **result, size_t *result_length,
                                PatchError *error) {
    const MtwExeVariant *wanted;
    const unsigned char *payload;
    unsigned char *target, held_jump[5], ghost_jump[5], dispatch_jump[12], label_call[6], section[40];
    size_t payload_length, i;
    uint32_t count, image_size, slot, prior_size;
    char digest[65];
    if (!variant || !variant->key ||
        !mtw_key_layout(source, length, variant, &count, &image_size, &slot) ||
        length < MTW_KEY_PAGE) {
        patch_error_set(error, "unsupported_executable", "The key executable layout is not pinned.", ERROR_INVALID_DATA);
        return 0;
    }
    prior_size = image_size - MTW_KEY_PAGE;
    if (!mtw_key_payload(prior_size, &payload, &payload_length)) {
        patch_error_set(error, "unsupported_executable", "The key payload is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    memset(section, 0, sizeof section);
    mtw_scroll_section(section, ".mtwkey", (uint32_t)payload_length,
                       prior_size, (uint32_t)(length - MTW_KEY_PAGE), 0x60000020u);
    mtw_key_redirect(held_jump, MTW_KEY_HELD_VA, 0x400000u + prior_size);
    mtw_key_redirect(ghost_jump, MTW_KEY_GHOST_VA, 0x400000u + prior_size + MTW_KEY_GHOST_OFFSET);
    mtw_key_dispatch_redirect(dispatch_jump, prior_size);
    mtw_key_label_redirect(label_call, prior_size);
    if (memcmp(source + slot, section, 40u) ||
        memcmp(source + length - MTW_KEY_PAGE, payload, payload_length) ||
        memcmp(source + MTW_KEY_HELD_RAW, held_jump, sizeof held_jump) ||
        memcmp(source + MTW_KEY_GHOST_RAW, ghost_jump, sizeof ghost_jump) ||
        memcmp(source + MTW_KEY_DISPATCH_RAW, dispatch_jump, sizeof dispatch_jump) ||
        memcmp(source + MTW_KEY_LABEL_RAW, label_call, sizeof label_call) ||
        memcmp(source + MTW_KEY_SPECIAL_RAW, mtw_key_special_patch, sizeof mtw_key_special_patch) ||
        memcmp(source + MTW_KEY_COLLISION_RAW, mtw_key_original_collision, sizeof mtw_key_original_collision) ||
        source[MTW_KEY_BACKSPACE_FLAG_RAW] != 1u ||
        source[MTW_KEY_SPACE_FLAG_RAW] != 1u ||
        source[MTW_KEY_DELETE_FLAG_68_RAW] != 1u ||
        source[MTW_KEY_DELETE_FLAG_106_RAW] != 1u ||
        !mtw_key_extra_flags_match(source, 1u)) {
        patch_error_set(error, "transform_mismatch", "The key executable section or redirection changed.", ERROR_INVALID_DATA);
        return 0;
    }
    for (i = payload_length; i < MTW_KEY_PAGE; ++i)
        if (source[length - MTW_KEY_PAGE + i]) {
            patch_error_set(error, "transform_mismatch", "The key executable padding changed.", ERROR_INVALID_DATA);
            return 0;
        }
    wanted = mtw_exe_variant_for_options_key(
        variant->scroll, variant->sprite, variant->tutorial, variant->laa,
        variant->checksum_style, variant->ui, variant->economy, 0u);
    if (!wanted || wanted->file_length + MTW_KEY_PAGE != length ||
        !patch_alloc(context, wanted->file_length, 1, (void **)&target, error)) return 0;
    memcpy(target, source, wanted->file_length);
    mtw_scroll_put16(target + 0x86u, (uint16_t)(count - 1u));
    mtw_scroll_put32(target + 0x9Cu, mtw_scroll_u32(source + 0x9Cu) - MTW_KEY_PAGE);
    mtw_scroll_put32(target + 0xD0u, prior_size);
    memset(target + slot, 0, 40u);
    memcpy(target + MTW_KEY_HELD_RAW, mtw_key_original_held, sizeof mtw_key_original_held);
    memcpy(target + MTW_KEY_GHOST_RAW, mtw_key_original_ghost, sizeof mtw_key_original_ghost);
    memcpy(target + MTW_KEY_DISPATCH_RAW, mtw_key_original_dispatch, sizeof mtw_key_original_dispatch);
    memcpy(target + MTW_KEY_LABEL_RAW, mtw_key_original_label, sizeof mtw_key_original_label);
    memcpy(target + MTW_KEY_SPECIAL_RAW, mtw_key_original_special, sizeof mtw_key_original_special);
    target[MTW_KEY_BACKSPACE_FLAG_RAW] = 0u;
    target[MTW_KEY_SPACE_FLAG_RAW] = 0u;
    target[MTW_KEY_DELETE_FLAG_68_RAW] = 0u;
    target[MTW_KEY_DELETE_FLAG_106_RAW] = 0u;
    mtw_key_extra_flags_set(target, 0u);
    if (!strcmp(variant->checksum_style, "zero"))
        mtw_scroll_put32(target + 0xD8u, 0u);
    else if (!strcmp(variant->checksum_style, "recompute")) {
        mtw_scroll_put32(target + 0xD8u, 0u);
        mtw_scroll_put32(target + 0xD8u, mtw_direct_checksum(target, wanted->file_length));
    } else if (!strcmp(variant->checksum_style, "retain"))
        mtw_scroll_put32(target + 0xD8u, mtw_scroll_u32(source + 0xD8u));
    else mtw_scroll_put32(target + 0xD8u, wanted->canonical_checksum);
    if (!patch_sha256_bytes(context, target, wanted->file_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        patch_context_free(context, target);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "Key removal did not restore a pinned executable.", ERROR_INVALID_DATA);
        patch_context_free(context, target);
        return 0;
    }
    *result = target;
    *result_length = wanted->file_length;
    return 1;
}

#endif
