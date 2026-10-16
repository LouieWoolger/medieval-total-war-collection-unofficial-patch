#ifndef MTW_UI_EXE_PATCH_H
#define MTW_UI_EXE_PATCH_H

/* Independent R-06 character parchment executable section. The input and
   output must both be full-file identities from mtw_exe_variants.h. */
#include "../vendor/ui_exe/ui_hook.inc"

#define MTW_UI_PAGE 0x1000u
#define MTW_UI_SECTION_FLAGS 0x60000020u

static const uint32_t mtw_ui_sites[4] = {0x58EBD8u, 0x591245u, 0x58EC82u, 0x58ECBFu};
static const unsigned char mtw_ui_original[4][9] = {
    {0x8B,0x8E,0x2C,0x01,0x00,0x00},
    {0x57,0x68,0x00,0x00,0x00,0x01},
    {0x83,0xC4,0x0C,0x89,0x15,0x48,0x02,0x8D,0x00},
    {0x8B,0x45,0x84,0x8B,0x0D,0x48,0x02,0x8D,0x00},
};
static const size_t mtw_ui_site_lengths[4] = {6u, 6u, 9u, 9u};

static inline int mtw_ui_payload(uint32_t image_size, const unsigned char **payload,
                                 size_t *length) {
    if (!payload || !length) return 0;
    switch (image_size) {
    case 0xB4C000u: *payload = mtw_ui_hook_b4c000; *length = MTW_UI_HOOK_B4C000_LENGTH; return 1;
    case 0xB4D000u: *payload = mtw_ui_hook_b4d000; *length = MTW_UI_HOOK_B4D000_LENGTH; return 1;
    case 0xB4E000u: *payload = mtw_ui_hook_b4e000; *length = MTW_UI_HOOK_B4E000_LENGTH; return 1;
    case 0xB4F000u: *payload = mtw_ui_hook_b4f000; *length = MTW_UI_HOOK_B4F000_LENGTH; return 1;
    default: return 0;
    }
}

static inline void mtw_ui_hook_bytes(unsigned char *out, size_t length,
                                     uint32_t site, uint32_t destination) {
    size_t i;
    int32_t relative = (int32_t)((int64_t)destination - ((int64_t)site + 5));
    out[0] = 0xE9;
    mtw_scroll_put32(out + 1, (uint32_t)relative);
    for (i = 5u; i < length; ++i) out[i] = 0x90;
}

static inline int mtw_ui_section_layout(const unsigned char *source, size_t source_length,
                                        const MtwExeVariant *variant, uint32_t *count,
                                        uint32_t *image_size, uint32_t *slot) {
    uint32_t expected_count, expected_size, table = 0x178u;
    if (!source || !variant || !count || !image_size || !slot || source_length < 0x1000u ||
        source[0] != 'M' || source[1] != 'Z' || mtw_scroll_u32(source + 0x3Cu) != 0x80u ||
        memcmp(source + 0x80u, "PE\0\0", 4u) || mtw_scroll_u32(source + 0x84u) % 0x10000u != 0x14Cu ||
        mtw_scroll_u32(source + 0x98u) % 0x10000u != 0x10Bu ||
        mtw_scroll_u32(source + 0xB4u) != 0x400000u ||
        mtw_scroll_u32(source + 0xB8u) != 0x1000u ||
        mtw_scroll_u32(source + 0xBCu) != 0x1000u ||
        mtw_scroll_u32(source + 0xD4u) != 0x1000u) return 0;
    expected_count = 6u + 2u * variant->scroll + variant->sprite + variant->ui;
    expected_size = 0xB4C000u + 0x2000u * variant->scroll +
                    0x1000u * variant->sprite + 0x1000u * variant->ui;
    if ((uint32_t)(source[0x86u] | ((uint32_t)source[0x87u] << 8)) != expected_count ||
        mtw_scroll_u32(source + 0xD0u) != expected_size ||
        source_length != 0x4E7000u + 0x2000u * variant->scroll +
                         0x1000u * variant->sprite + 0x1000u * variant->ui ||
        table + (expected_count + (variant->ui ? 0u : 1u)) * 40u > 0x1000u)
        return 0;
    *count = expected_count;
    *image_size = expected_size;
    *slot = table + (expected_count - variant->ui) * 40u;
    return 1;
}

static inline int mtw_ui_add(PatchContext *context, const unsigned char *source,
                             size_t source_length, const MtwExeVariant *source_variant,
                             unsigned char **result, size_t *result_length,
                             PatchError *error) {
    const MtwExeVariant *wanted;
    const unsigned char *payload;
    unsigned char *target;
    size_t payload_length, i;
    uint32_t count, image_size, slot, offset;
    char digest[65];
    static const unsigned char empty_slot[40] = {0};
    if (!source_variant || source_variant->ui ||
        !mtw_ui_section_layout(source, source_length, source_variant,
                               &count, &image_size, &slot) ||
        !mtw_ui_payload(image_size, &payload, &payload_length) ||
        memcmp(source + slot, empty_slot, sizeof empty_slot)) {
        patch_error_set(error, "unsupported_executable", "The UI section slot or PE layout changed.", ERROR_INVALID_DATA);
        return 0;
    }
    for (i = 0; i < 4u; ++i) {
        offset = mtw_ui_sites[i] - 0x400000u;
        if (memcmp(source + offset, mtw_ui_original[i], mtw_ui_site_lengths[i])) {
            patch_error_set(error, "transform_mismatch", "A character parchment instruction changed.", ERROR_INVALID_DATA);
            return 0;
        }
    }
    wanted = mtw_exe_variant_for_options_ui(source_variant->scroll, source_variant->sprite,
                                             source_variant->tutorial, source_variant->laa,
                                             source_variant->checksum_style, 1u);
    if (!wanted || wanted->file_length != source_length + MTW_UI_PAGE) {
        patch_error_set(error, "unsupported_executable", "The UI output identity is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, source_length + MTW_UI_PAGE, 1, (void **)&target, error)) return 0;
    memset(target, 0, source_length + MTW_UI_PAGE);
    memcpy(target, source, source_length);
    mtw_scroll_put16(target + 0x86u, (uint16_t)(count + 1u));
    mtw_scroll_put32(target + 0x9Cu, mtw_scroll_u32(source + 0x9Cu) + MTW_UI_PAGE);
    mtw_scroll_put32(target + 0xD0u, image_size + MTW_UI_PAGE);
    mtw_scroll_section(target + slot, ".mtwui", (uint32_t)payload_length,
                       image_size, (uint32_t)source_length, MTW_UI_SECTION_FLAGS);
    memcpy(target + source_length, payload, payload_length);
    for (i = 0; i < 4u; ++i) {
        offset = mtw_ui_sites[i] - 0x400000u;
        mtw_ui_hook_bytes(target + offset, mtw_ui_site_lengths[i], mtw_ui_sites[i],
                          0x400000u + image_size + mtw_scroll_u32(payload + 4u * i));
    }
    if (!strcmp(source_variant->checksum_style, "base") ||
        !strcmp(source_variant->checksum_style, "recompute")) {
        mtw_scroll_put32(target + 0xD8u, 0u);
        mtw_scroll_put32(target + 0xD8u, mtw_direct_checksum(target, source_length + MTW_UI_PAGE));
    } else if (!strcmp(source_variant->checksum_style, "zero"))
        mtw_scroll_put32(target + 0xD8u, 0u);
    if (!patch_sha256_bytes(context, target, source_length + MTW_UI_PAGE, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        patch_context_free(context, target);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "The UI executable did not match its pinned output.", ERROR_INVALID_DATA);
        patch_context_free(context, target);
        return 0;
    }
    *result = target;
    *result_length = source_length + MTW_UI_PAGE;
    return 1;
}

static inline int mtw_ui_strip(PatchContext *context, const unsigned char *source,
                               size_t source_length, const MtwExeVariant *source_variant,
                               unsigned char **result, size_t *result_length,
                               PatchError *error) {
    const MtwExeVariant *wanted;
    const unsigned char *payload;
    unsigned char *target;
    unsigned char expected_hook[9], expected_section[40];
    size_t payload_length, i;
    uint32_t count, image_size, slot, offset, original_size;
    char digest[65];
    if (!source_variant || !source_variant->ui ||
        !mtw_ui_section_layout(source, source_length, source_variant,
                               &count, &image_size, &slot) || source_length < MTW_UI_PAGE) {
        patch_error_set(error, "unsupported_executable", "The UI executable layout is not pinned.", ERROR_INVALID_DATA);
        return 0;
    }
    original_size = image_size - MTW_UI_PAGE;
    if (!mtw_ui_payload(original_size, &payload, &payload_length)) {
        patch_error_set(error, "unsupported_executable", "The UI executable payload is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    memset(expected_section, 0, sizeof expected_section);
    mtw_scroll_section(expected_section, ".mtwui", (uint32_t)payload_length,
                       original_size, (uint32_t)(source_length - MTW_UI_PAGE), MTW_UI_SECTION_FLAGS);
    if (memcmp(source + slot, expected_section, sizeof expected_section) ||
        memcmp(source + source_length - MTW_UI_PAGE, payload, payload_length)) {
        patch_error_set(error, "transform_mismatch", "The UI executable section changed.", ERROR_INVALID_DATA);
        return 0;
    }
    for (i = payload_length; i < MTW_UI_PAGE; ++i)
        if (source[source_length - MTW_UI_PAGE + i]) {
            patch_error_set(error, "transform_mismatch", "The UI executable padding changed.", ERROR_INVALID_DATA);
            return 0;
        }
    for (i = 0; i < 4u; ++i) {
        offset = mtw_ui_sites[i] - 0x400000u;
        mtw_ui_hook_bytes(expected_hook, mtw_ui_site_lengths[i], mtw_ui_sites[i],
                          0x400000u + original_size + mtw_scroll_u32(payload + 4u * i));
        if (memcmp(source + offset, expected_hook, mtw_ui_site_lengths[i])) {
            patch_error_set(error, "transform_mismatch", "A UI executable redirection changed.", ERROR_INVALID_DATA);
            return 0;
        }
    }
    wanted = mtw_exe_variant_for_options_ui(source_variant->scroll, source_variant->sprite,
                                             source_variant->tutorial, source_variant->laa,
                                             source_variant->checksum_style, 0u);
    if (!wanted || wanted->file_length + MTW_UI_PAGE != source_length) {
        patch_error_set(error, "unsupported_executable", "The pre-UI executable identity is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, wanted->file_length, 1, (void **)&target, error)) return 0;
    memcpy(target, source, wanted->file_length);
    mtw_scroll_put16(target + 0x86u, (uint16_t)(count - 1u));
    mtw_scroll_put32(target + 0x9Cu, mtw_scroll_u32(source + 0x9Cu) - MTW_UI_PAGE);
    mtw_scroll_put32(target + 0xD0u, original_size);
    memset(target + slot, 0, 40u);
    for (i = 0; i < 4u; ++i) {
        offset = mtw_ui_sites[i] - 0x400000u;
        memcpy(target + offset, mtw_ui_original[i], mtw_ui_site_lengths[i]);
    }
    if (!strcmp(source_variant->checksum_style, "zero"))
        mtw_scroll_put32(target + 0xD8u, 0u);
    else if (!strcmp(source_variant->checksum_style, "recompute")) {
        mtw_scroll_put32(target + 0xD8u, 0u);
        mtw_scroll_put32(target + 0xD8u, mtw_direct_checksum(target, wanted->file_length));
    } else mtw_scroll_put32(target + 0xD8u, wanted->canonical_checksum);
    if (!patch_sha256_bytes(context, target, wanted->file_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        patch_context_free(context, target);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "UI removal did not restore the pinned executable.", ERROR_INVALID_DATA);
        patch_context_free(context, target);
        return 0;
    }
    *result = target;
    *result_length = wanted->file_length;
    return 1;
}

#endif
