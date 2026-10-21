#ifndef MTW_ECONOMY_EXE_PATCH_H
#define MTW_ECONOMY_EXE_PATCH_H

/* Final independent PE32 section transform for the optional Huge economy.
   Both the input and output are pinned full-file identities. */
#include "../vendor/economy_exe/economy_hook.inc"

#define MTW_ECONOMY_PAGE 0x1000u
#define MTW_ECONOMY_SECTION_FLAGS 0x60000020u

static const uint32_t mtw_economy_sites[11] = {
    0x550ADBu, 0x57BC2Cu, 0x632FE3u, 0x7305BDu, 0x73060Cu,
    0x61D1A8u, 0x61D268u, 0x61D527u, 0x62B098u, 0x62B0F9u, 0x57A841u
};
static const unsigned char mtw_economy_original[11][7] = {
    {0xE8,0xE0,0x37,0xED,0xFF},
    {0xE8,0x8F,0x86,0xEA,0xFF},
    {0xE8,0xD8,0x12,0xDF,0xFF},
    {0x8B,0x0D,0xF8,0x65,0xBE,0x00},
    {0x0F,0xAF,0x35,0xF8,0x65,0xBE,0x00},
    {0x0F,0xAF,0x0D,0xF8,0x65,0xBE,0x00},
    {0x0F,0xAF,0x0D,0xF8,0x65,0xBE,0x00},
    {0x0F,0xAF,0x0D,0xF8,0x65,0xBE,0x00},
    {0x8B,0x97,0x44,0x03,0x00,0x00},
    {0x8B,0xBF,0x44,0x03,0x00,0x00},
    {0x50,0x8D,0x4C,0x24,0x60}
};
static const size_t mtw_economy_site_lengths[11] =
    {5u,5u,5u,6u,7u,7u,7u,7u,6u,6u,5u};
static const unsigned char mtw_economy_entry_index[11] =
    {0u,1u,2u,3u,4u,5u,5u,5u,6u,7u,8u};

static inline int mtw_economy_payload(uint32_t image_size,
                                      const unsigned char **payload, size_t *length) {
    if (!payload || !length) return 0;
    switch (image_size) {
    case 0xB4C000u: *payload = mtw_economy_hook_b4c000; *length = MTW_ECONOMY_HOOK_B4C000_LENGTH; return 1;
    case 0xB4D000u: *payload = mtw_economy_hook_b4d000; *length = MTW_ECONOMY_HOOK_B4D000_LENGTH; return 1;
    case 0xB4E000u: *payload = mtw_economy_hook_b4e000; *length = MTW_ECONOMY_HOOK_B4E000_LENGTH; return 1;
    case 0xB4F000u: *payload = mtw_economy_hook_b4f000; *length = MTW_ECONOMY_HOOK_B4F000_LENGTH; return 1;
    case 0xB50000u: *payload = mtw_economy_hook_b50000; *length = MTW_ECONOMY_HOOK_B50000_LENGTH; return 1;
    default: return 0;
    }
}

static inline void mtw_economy_hook_bytes(unsigned char *out, size_t length,
                                          uint32_t site, uint32_t destination,
                                          int jump) {
    size_t i;
    int32_t relative = (int32_t)((int64_t)destination - ((int64_t)site + 5));
    out[0] = jump ? 0xE9 : 0xE8;
    mtw_scroll_put32(out + 1, (uint32_t)relative);
    for (i = 5u; i < length; ++i) out[i] = 0x90;
}

static inline int mtw_economy_section_layout(const unsigned char *source,
                                             size_t source_length,
                                             const MtwExeVariant *variant,
                                             uint32_t *count, uint32_t *image_size,
                                             uint32_t *slot) {
    uint32_t expected_count, expected_size, table = 0x178u;
    if (!source || !variant || !count || !image_size || !slot || source_length < 0x1000u ||
        source[0] != 'M' || source[1] != 'Z' || mtw_scroll_u32(source + 0x3Cu) != 0x80u ||
        memcmp(source + 0x80u, "PE\0\0", 4u) ||
        mtw_scroll_u32(source + 0x84u) % 0x10000u != 0x14Cu ||
        mtw_scroll_u32(source + 0x98u) % 0x10000u != 0x10Bu ||
        mtw_scroll_u32(source + 0xB4u) != 0x400000u ||
        mtw_scroll_u32(source + 0xB8u) != 0x1000u ||
        mtw_scroll_u32(source + 0xBCu) != 0x1000u ||
        mtw_scroll_u32(source + 0xD4u) != 0x1000u) return 0;
    expected_count = 6u + 2u * variant->scroll + variant->sprite +
                     variant->ui + variant->economy;
    expected_size = 0xB4C000u + 0x2000u * variant->scroll +
                    0x1000u * variant->sprite + 0x1000u * variant->ui +
                    0x1000u * variant->economy;
    if ((uint32_t)(source[0x86u] | ((uint32_t)source[0x87u] << 8)) != expected_count ||
        mtw_scroll_u32(source + 0xD0u) != expected_size ||
        source_length != 0x4E7000u + 0x2000u * variant->scroll +
                         0x1000u * variant->sprite + 0x1000u * variant->ui +
                         0x1000u * variant->economy ||
        table + (expected_count + (variant->economy ? 0u : 1u)) * 40u > 0x1000u)
        return 0;
    *count = expected_count;
    *image_size = expected_size;
    *slot = table + (expected_count - variant->economy) * 40u;
    return 1;
}

static inline int mtw_economy_add(PatchContext *context,
                                  const unsigned char *source, size_t source_length,
                                  const MtwExeVariant *source_variant,
                                  unsigned char **result, size_t *result_length,
                                  PatchError *error) {
    const MtwExeVariant *wanted;
    const unsigned char *payload;
    unsigned char *target;
    size_t payload_length, i;
    uint32_t count, image_size, slot, offset, entry;
    char digest[65];
    static const unsigned char empty_slot[40] = {0};
    if (!source_variant || source_variant->economy ||
        !mtw_economy_section_layout(source, source_length, source_variant,
                                    &count, &image_size, &slot) ||
        !mtw_economy_payload(image_size, &payload, &payload_length) ||
        memcmp(source + slot, empty_slot, sizeof empty_slot)) {
        patch_error_set(error, "unsupported_executable", "The economy section slot or PE layout changed.", ERROR_INVALID_DATA);
        return 0;
    }
    for (i = 0u; i < 11u; ++i) {
        offset = mtw_economy_sites[i] - 0x400000u;
        if (memcmp(source + offset, mtw_economy_original[i], mtw_economy_site_lengths[i])) {
            patch_error_set(error, "transform_mismatch", "An economy instruction changed.", ERROR_INVALID_DATA);
            return 0;
        }
    }
    wanted = mtw_exe_variant_for_options_economy(
        source_variant->scroll, source_variant->sprite,
        source_variant->tutorial, source_variant->laa,
        source_variant->checksum_style, source_variant->ui, 1u);
    if (!wanted || wanted->file_length != source_length + MTW_ECONOMY_PAGE) {
        patch_error_set(error, "unsupported_executable", "The economy output identity is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, source_length + MTW_ECONOMY_PAGE, 1, (void **)&target, error)) return 0;
    memset(target, 0, source_length + MTW_ECONOMY_PAGE);
    memcpy(target, source, source_length);
    mtw_scroll_put16(target + 0x86u, (uint16_t)(count + 1u));
    mtw_scroll_put32(target + 0x9Cu, mtw_scroll_u32(source + 0x9Cu) + MTW_ECONOMY_PAGE);
    mtw_scroll_put32(target + 0xD0u, image_size + MTW_ECONOMY_PAGE);
    mtw_scroll_section(target + slot, ".mtwbal", (uint32_t)payload_length,
                       image_size, (uint32_t)source_length, MTW_ECONOMY_SECTION_FLAGS);
    memcpy(target + source_length, payload, payload_length);
    for (i = 0u; i < 11u; ++i) {
        offset = mtw_economy_sites[i] - 0x400000u;
        entry = mtw_scroll_u32(payload + 4u * mtw_economy_entry_index[i]);
        mtw_economy_hook_bytes(target + offset, mtw_economy_site_lengths[i],
                               mtw_economy_sites[i], 0x400000u + image_size + entry,
                               i == 10u);
    }
    if (!strcmp(source_variant->checksum_style, "base") ||
        !strcmp(source_variant->checksum_style, "recompute")) {
        mtw_scroll_put32(target + 0xD8u, 0u);
        mtw_scroll_put32(target + 0xD8u,
                         mtw_direct_checksum(target, source_length + MTW_ECONOMY_PAGE));
    } else if (!strcmp(source_variant->checksum_style, "zero"))
        mtw_scroll_put32(target + 0xD8u, 0u);
    if (!patch_sha256_bytes(context, target, source_length + MTW_ECONOMY_PAGE, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        patch_context_free(context, target);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "The economy executable did not match its pinned output.", ERROR_INVALID_DATA);
        patch_context_free(context, target);
        return 0;
    }
    *result = target;
    *result_length = source_length + MTW_ECONOMY_PAGE;
    return 1;
}

static inline int mtw_economy_strip(PatchContext *context,
                                    const unsigned char *source, size_t source_length,
                                    const MtwExeVariant *source_variant,
                                    unsigned char **result, size_t *result_length,
                                    PatchError *error) {
    const MtwExeVariant *wanted;
    const unsigned char *payload;
    unsigned char *target;
    unsigned char expected_hook[7], expected_section[40];
    size_t payload_length, i;
    uint32_t count, image_size, slot, offset, original_size, entry;
    char digest[65];
    if (!source_variant || !source_variant->economy ||
        !mtw_economy_section_layout(source, source_length, source_variant,
                                    &count, &image_size, &slot) ||
        source_length < MTW_ECONOMY_PAGE) {
        patch_error_set(error, "unsupported_executable", "The balanced executable layout is not pinned.", ERROR_INVALID_DATA);
        return 0;
    }
    original_size = image_size - MTW_ECONOMY_PAGE;
    if (!mtw_economy_payload(original_size, &payload, &payload_length)) {
        patch_error_set(error, "unsupported_executable", "The economy payload is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    memset(expected_section, 0, sizeof expected_section);
    mtw_scroll_section(expected_section, ".mtwbal", (uint32_t)payload_length,
                       original_size, (uint32_t)(source_length - MTW_ECONOMY_PAGE),
                       MTW_ECONOMY_SECTION_FLAGS);
    if (memcmp(source + slot, expected_section, sizeof expected_section) ||
        memcmp(source + source_length - MTW_ECONOMY_PAGE, payload, payload_length)) {
        patch_error_set(error, "transform_mismatch", "The economy executable section changed.", ERROR_INVALID_DATA);
        return 0;
    }
    for (i = payload_length; i < MTW_ECONOMY_PAGE; ++i)
        if (source[source_length - MTW_ECONOMY_PAGE + i]) {
            patch_error_set(error, "transform_mismatch", "The economy executable padding changed.", ERROR_INVALID_DATA);
            return 0;
        }
    for (i = 0u; i < 11u; ++i) {
        offset = mtw_economy_sites[i] - 0x400000u;
        entry = mtw_scroll_u32(payload + 4u * mtw_economy_entry_index[i]);
        mtw_economy_hook_bytes(expected_hook, mtw_economy_site_lengths[i],
                               mtw_economy_sites[i], 0x400000u + original_size + entry,
                               i == 10u);
        if (memcmp(source + offset, expected_hook, mtw_economy_site_lengths[i])) {
            patch_error_set(error, "transform_mismatch", "An economy redirection changed.", ERROR_INVALID_DATA);
            return 0;
        }
    }
    wanted = mtw_exe_variant_for_options_economy(
        source_variant->scroll, source_variant->sprite,
        source_variant->tutorial, source_variant->laa,
        source_variant->checksum_style, source_variant->ui, 0u);
    if (!wanted || wanted->file_length + MTW_ECONOMY_PAGE != source_length) {
        patch_error_set(error, "unsupported_executable", "The pre-economy identity is unavailable.", ERROR_INVALID_DATA);
        return 0;
    }
    if (!patch_alloc(context, wanted->file_length, 1, (void **)&target, error)) return 0;
    memcpy(target, source, wanted->file_length);
    mtw_scroll_put16(target + 0x86u, (uint16_t)(count - 1u));
    mtw_scroll_put32(target + 0x9Cu, mtw_scroll_u32(source + 0x9Cu) - MTW_ECONOMY_PAGE);
    mtw_scroll_put32(target + 0xD0u, original_size);
    memset(target + slot, 0, 40u);
    for (i = 0u; i < 11u; ++i) {
        offset = mtw_economy_sites[i] - 0x400000u;
        memcpy(target + offset, mtw_economy_original[i], mtw_economy_site_lengths[i]);
    }
    if (!strcmp(source_variant->checksum_style, "zero"))
        mtw_scroll_put32(target + 0xD8u, 0u);
    else if (!strcmp(source_variant->checksum_style, "recompute")) {
        mtw_scroll_put32(target + 0xD8u, 0u);
        mtw_scroll_put32(target + 0xD8u, mtw_direct_checksum(target, wanted->file_length));
    } else if (!strcmp(source_variant->checksum_style, "retain"))
        mtw_scroll_put32(target + 0xD8u, mtw_scroll_u32(source + 0xD8u));
    else mtw_scroll_put32(target + 0xD8u, wanted->canonical_checksum);
    if (!patch_sha256_bytes(context, target, wanted->file_length, digest)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        patch_context_free(context, target);
        return 0;
    }
    if (strcmp(digest, wanted->sha256)) {
        patch_error_set(error, "transform_mismatch", "Economy removal did not restore a pinned executable.", ERROR_INVALID_DATA);
        patch_context_free(context, target);
        return 0;
    }
    *result = target;
    *result_length = wanted->file_length;
    return 1;
}

#endif
