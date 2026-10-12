#include "../src/campaign_locale_patch.h"
#include <stdio.h>

int main(int argc, char **argv) {
    PatchContext context = {0};
    PatchError error = {0};
    PatchGuard guard = {0};
    wchar_t *path = NULL, *parent = NULL;
    char *original = NULL;
    unsigned char *enabled = NULL, *restored = NULL, *unchanged = NULL;
    unsigned char *previous = NULL, *migrated = NULL, *previous_restored = NULL;
    size_t original_length = 0, enabled_length = 0, restored_length = 0, unchanged_length = 0;
    size_t previous_length, migrated_length = 0, previous_restored_length = 0;
    size_t tip_at, new_tip_length = sizeof(mtw_locale_tip_add) - 1u;
    size_t previous_tip_length = sizeof(mtw_locale_previous_tip_add) - 1u;
    char previous_hash[65];
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
