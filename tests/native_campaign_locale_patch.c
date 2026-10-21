#include "../src/campaign_locale_patch.h"
#include <stdio.h>

static int previous_zoom_text(PatchContext *context, const unsigned char *current,
                              size_t current_length, unsigned char **result,
                              size_t *result_length, PatchError *error) {
    size_t new_label = sizeof(mtw_locale_zoom_label_add) - 1u;
    size_t new_tip = sizeof(mtw_locale_zoom_tip_add) - 1u;
    size_t old_label = sizeof(mtw_locale_previous_zoom_label_add) - 1u;
    size_t old_tip = sizeof(mtw_locale_previous_zoom_tip_add) - 1u;
    size_t label_at = mtw_locale_find_once(current, current_length,
                                           mtw_locale_zoom_label_add, new_label);
    size_t tip_at = mtw_locale_find_once(current, current_length,
                                         mtw_locale_zoom_tip_add, new_tip);
    size_t size, cursor = 0;
    unsigned char *previous;
    if (label_at == SIZE_MAX || tip_at == SIZE_MAX ||
        label_at + new_label > tip_at || tip_at + new_tip > current_length) return 0;
    size = current_length - new_label - new_tip + old_label + old_tip;
    if (!patch_alloc(context, size, 1, (void **)&previous, error)) return 0;
    memcpy(previous + cursor, current, label_at); cursor += label_at;
    memcpy(previous + cursor, mtw_locale_previous_zoom_label_add, old_label); cursor += old_label;
    memcpy(previous + cursor, current + label_at + new_label,
           tip_at - label_at - new_label); cursor += tip_at - label_at - new_label;
    memcpy(previous + cursor, mtw_locale_previous_zoom_tip_add, old_tip); cursor += old_tip;
    memcpy(previous + cursor, current + tip_at + new_tip,
           current_length - tip_at - new_tip); cursor += current_length - tip_at - new_tip;
    if (cursor != size) return 0;
    *result = previous;
    *result_length = size;
    return 1;
}

static int r289_zoom_text(PatchContext *context, const unsigned char *current,
                          size_t current_length, unsigned char **result,
                          size_t *result_length, PatchError *error) {
    size_t new_tip = sizeof(mtw_locale_zoom_tip_add) - 1u;
    size_t old_tip = sizeof(mtw_locale_r289_zoom_tip_add) - 1u;
    size_t tip_at = mtw_locale_find_once(current, current_length,
                                         mtw_locale_zoom_tip_add, new_tip);
    size_t size;
    unsigned char *previous;
    if (tip_at == SIZE_MAX || tip_at + new_tip > current_length) return 0;
    size = current_length - new_tip + old_tip;
    if (!patch_alloc(context, size, 1, (void **)&previous, error)) return 0;
    memcpy(previous, current, tip_at);
    memcpy(previous + tip_at, mtw_locale_r289_zoom_tip_add, old_tip);
    memcpy(previous + tip_at + old_tip, current + tip_at + new_tip,
           current_length - tip_at - new_tip);
    *result = previous;
    *result_length = size;
    return 1;
}

int main(int argc, char **argv) {
    PatchContext context = {0};
    PatchError error = {0};
    PatchGuard guard = {0};
    wchar_t *path = NULL, *parent = NULL;
    char *original = NULL;
    unsigned char *enabled = NULL, *restored = NULL, *unchanged = NULL;
    unsigned char *previous = NULL, *migrated = NULL, *previous_restored = NULL;
    unsigned char *zoom = NULL, *fast_zoom = NULL, *from_fast = NULL;
    unsigned char *from_both = NULL, *zoom_again = NULL;
    unsigned char *old_zoom = NULL, *old_fast_zoom = NULL, *migrated_zoom = NULL;
    unsigned char *migrated_fast_zoom = NULL;
    unsigned char *r289_zoom = NULL, *r289_fast_zoom = NULL;
    unsigned char *migrated_r289_zoom = NULL, *migrated_r289_fast_zoom = NULL;
    size_t original_length = 0, enabled_length = 0, restored_length = 0, unchanged_length = 0;
    size_t previous_length, migrated_length = 0, previous_restored_length = 0;
    size_t zoom_length = 0, fast_zoom_length = 0, from_fast_length = 0;
    size_t from_both_length = 0, zoom_again_length = 0;
    size_t old_zoom_length = 0, old_fast_zoom_length = 0;
    size_t migrated_zoom_length = 0, migrated_fast_zoom_length = 0;
    size_t r289_zoom_length = 0, r289_fast_zoom_length = 0;
    size_t migrated_r289_zoom_length = 0, migrated_r289_fast_zoom_length = 0;
    size_t tip_at, new_tip_length = sizeof(mtw_locale_tip_add) - 1u;
    size_t previous_tip_length = sizeof(mtw_locale_previous_tip_add) - 1u;
    char previous_hash[65], old_zoom_hash[65], old_fast_zoom_hash[65];
    char r289_zoom_hash[65], r289_fast_zoom_hash[65];
    int ok = 0;
    if (argc != 2 || !patch_utf8_to_wide(&context, argv[1], strlen(argv[1]), &path, &error) ||
        !patch_parent_path(&context, path, &parent, &error) ||
        !patch_guard_open(&guard, parent, &error) ||
        !patch_file_read(&guard, path, MTW_CAMPAIGN_LOCALE_STOCK_SIZE,
                         &context, &original, &original_length, &error)) goto done;
    if (!mtw_campaign_locale_transform(&context, (const unsigned char *)original,
                                       original_length, 1, &enabled, &enabled_length, &error) ||
        !mtw_campaign_locale_transform(&context, enabled, enabled_length,
                                       0, &restored, &restored_length, &error) ||
        restored_length != original_length || memcmp(restored, original, original_length) ||
        !mtw_campaign_locale_transform(&context, enabled, enabled_length,
                                       1, &unchanged, &unchanged_length, &error) ||
        unchanged_length != enabled_length || memcmp(unchanged, enabled, enabled_length)) goto done;
    tip_at = mtw_locale_find_once(enabled, enabled_length, mtw_locale_tip_add, new_tip_length);
    if (tip_at == SIZE_MAX) goto done;
    previous_length = enabled_length - new_tip_length + previous_tip_length;
    if (previous_length != MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SIZE ||
        !patch_alloc(&context, previous_length, 1, (void **)&previous, &error)) goto done;
    memcpy(previous, enabled, tip_at);
    memcpy(previous + tip_at, mtw_locale_previous_tip_add, previous_tip_length);
    memcpy(previous + tip_at + previous_tip_length,
           enabled + tip_at + new_tip_length, enabled_length - tip_at - new_tip_length);
    if (!patch_sha256_bytes(&context, previous, previous_length, previous_hash) ||
        strcmp(previous_hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SHA256) ||
        !mtw_campaign_locale_transform(&context, previous, previous_length,
                                       1, &migrated, &migrated_length, &error) ||
        migrated_length != enabled_length || memcmp(migrated, enabled, enabled_length) ||
        !mtw_campaign_locale_transform(&context, previous, previous_length,
                                       0, &previous_restored, &previous_restored_length, &error) ||
        previous_restored_length != original_length ||
        memcmp(previous_restored, original, original_length)) goto done;
    if (!mtw_campaign_locale_transform_options(&context, (const unsigned char *)original,
                                                original_length, 0, 1, &zoom, &zoom_length, &error) ||
        zoom_length != MTW_CAMPAIGN_LOCALE_ZOOM_SIZE ||
        !mtw_campaign_locale_transform_options(&context, zoom, zoom_length,
                                                1, 1, &fast_zoom, &fast_zoom_length, &error) ||
        fast_zoom_length != MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SIZE ||
        !mtw_campaign_locale_transform_options(&context, enabled, enabled_length,
                                                1, 1, &from_fast, &from_fast_length, &error) ||
        from_fast_length != fast_zoom_length || memcmp(from_fast, fast_zoom, fast_zoom_length) ||
        mtw_locale_find_once(zoom, zoom_length, mtw_locale_zoom_tip_add,
                             sizeof(mtw_locale_zoom_tip_add) - 1u) == SIZE_MAX ||
        mtw_locale_find_once(zoom, zoom_length, mtw_locale_r289_zoom_tip_add,
                             sizeof(mtw_locale_r289_zoom_tip_add) - 1u) != SIZE_MAX ||
        !mtw_campaign_locale_transform_options(&context, fast_zoom, fast_zoom_length,
                                                0, 0, &from_both, &from_both_length, &error) ||
        from_both_length != original_length || memcmp(from_both, original, original_length) ||
        !mtw_campaign_locale_transform_options(&context, fast_zoom, fast_zoom_length,
                                                0, 1, &zoom_again, &zoom_again_length, &error) ||
        zoom_again_length != zoom_length || memcmp(zoom_again, zoom, zoom_length) ||
        !previous_zoom_text(&context, zoom, zoom_length, &old_zoom, &old_zoom_length, &error) ||
        old_zoom_length != MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SIZE ||
        !patch_sha256_bytes(&context, old_zoom, old_zoom_length, old_zoom_hash) ||
        strcmp(old_zoom_hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SHA256) ||
        !previous_zoom_text(&context, fast_zoom, fast_zoom_length,
                            &old_fast_zoom, &old_fast_zoom_length, &error) ||
        old_fast_zoom_length != MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SIZE ||
        !patch_sha256_bytes(&context, old_fast_zoom, old_fast_zoom_length, old_fast_zoom_hash) ||
        strcmp(old_fast_zoom_hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SHA256) ||
        !mtw_campaign_locale_transform_options(&context, old_zoom, old_zoom_length,
                                                0, 1, &migrated_zoom, &migrated_zoom_length, &error) ||
        migrated_zoom_length != zoom_length || memcmp(migrated_zoom, zoom, zoom_length) ||
        !mtw_campaign_locale_transform_options(&context, old_fast_zoom, old_fast_zoom_length,
                                                1, 1, &migrated_fast_zoom,
                                                &migrated_fast_zoom_length, &error) ||
        migrated_fast_zoom_length != fast_zoom_length ||
        memcmp(migrated_fast_zoom, fast_zoom, fast_zoom_length) ||
        !mtw_campaign_locale_transform_options(&context, old_fast_zoom, old_fast_zoom_length,
                                                0, 0, &from_both, &from_both_length, &error) ||
        from_both_length != original_length || memcmp(from_both, original, original_length)) goto done;
    if (!r289_zoom_text(&context, zoom, zoom_length,
                        &r289_zoom, &r289_zoom_length, &error) ||
        r289_zoom_length != MTW_CAMPAIGN_LOCALE_R289_ZOOM_SIZE ||
        !patch_sha256_bytes(&context, r289_zoom, r289_zoom_length, r289_zoom_hash) ||
        strcmp(r289_zoom_hash, MTW_CAMPAIGN_LOCALE_R289_ZOOM_SHA256) ||
        !mtw_campaign_locale_zoom_identity(r289_zoom_hash, r289_zoom_length, 0) ||
        !r289_zoom_text(&context, fast_zoom, fast_zoom_length,
                        &r289_fast_zoom, &r289_fast_zoom_length, &error) ||
        r289_fast_zoom_length != MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SIZE ||
        !patch_sha256_bytes(&context, r289_fast_zoom, r289_fast_zoom_length,
                            r289_fast_zoom_hash) ||
        strcmp(r289_fast_zoom_hash, MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SHA256) ||
        !mtw_campaign_locale_zoom_identity(r289_fast_zoom_hash, r289_fast_zoom_length, 1) ||
        !mtw_campaign_locale_transform_options(&context, r289_zoom, r289_zoom_length,
                                                0, 1, &migrated_r289_zoom,
                                                &migrated_r289_zoom_length, &error) ||
        migrated_r289_zoom_length != zoom_length ||
        memcmp(migrated_r289_zoom, zoom, zoom_length) ||
        !mtw_campaign_locale_transform_options(&context, r289_fast_zoom, r289_fast_zoom_length,
                                                1, 1, &migrated_r289_fast_zoom,
                                                &migrated_r289_fast_zoom_length, &error) ||
        migrated_r289_fast_zoom_length != fast_zoom_length ||
        memcmp(migrated_r289_fast_zoom, fast_zoom, fast_zoom_length) ||
        !mtw_campaign_locale_transform_options(&context, r289_fast_zoom, r289_fast_zoom_length,
                                                0, 0, &from_both, &from_both_length, &error) ||
        from_both_length != original_length || memcmp(from_both, original, original_length)) goto done;
    enabled[100] ^= 1u;
    if (mtw_campaign_locale_transform(&context, enabled, enabled_length, 0,
                                      &unchanged, &unchanged_length, &error)) goto done;
    ok = 1;
done:
    printf("campaign locale transform: %s%s%s\n", ok ? "PASS" : "FAIL",
           !ok && error.message ? " - " : "", !ok && error.message ? error.message : "");
    patch_guard_close(&guard);
    patch_context_close(&context);
    return ok ? 0 : 1;
}
