#ifndef MTW_CAMPAIGN_LOCALE_PATCH_H
#define MTW_CAMPAIGN_LOCALE_PATCH_H

/* The campaign option panel resolves labels and tooltips by keys in this
   exact English game asset. Ship only a bounded transform, never the asset. */
#include "patch_files.h"

#define MTW_CAMPAIGN_LOCALE_STOCK_SIZE 279934u
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SIZE 280133u
#define MTW_CAMPAIGN_LOCALE_FAST_SIZE 280162u
#define MTW_CAMPAIGN_LOCALE_STOCK_SHA256 "A4ED1815E5BBE23DC0B4279AFB9398E1FABC6A9A1F2A014DB96F5F25E2B5B3DC"
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SHA256 "6415A171371504CECCDDFD66F7CD78F31DBA3497DFED8607CA7AD146B93C6B56"
#define MTW_CAMPAIGN_LOCALE_FAST_SHA256 "2BC2C971F1AC2C564490A526C6E8B1AE255E2CECBC43B45EC0B6B45A8FDF0F5B"

static const unsigned char mtw_locale_label_anchor[] =
    "[\"show_moves\"]\r\n{\"Show computer moves.\"}\r\n\r\n";
static const unsigned char mtw_locale_tip_anchor[] =
    "@[\"show_moves_tt\"]\r\n"
    "@{\"If this option is selected, once your turn is completed\"}\r\n"
    "@{\"the game will take a tour of the overall map\"}\r\n"
    "@{\"showing the moves other factions have made this turn.\"}\r\n"
    "@{\"\"}\r\n\r\n";
static const unsigned char mtw_locale_label_add[] =
    "[\"fast_moves\"]\r\n{\"Fast computer moves.\"}\r\n\r\n";
static const unsigned char mtw_locale_tip_add[] =
    "@[\"fast_moves_tt\"]\r\n"
    "@{\"When selected, AI turns play at their usual speed on the campaign map.\"}\r\n"
    "@{\"Turn it off to slow them down so you can follow where the AI has moved.\"}\r\n"
    "@{\"\"}\r\n\r\n";
static const unsigned char mtw_locale_previous_tip_add[] =
    "@[\"fast_moves_tt\"]\r\n"
    "@{\"When selected, visible computer moves use their usual speed.\"}\r\n"
    "@{\"Turn it off to watch those moves at a readable pace.\"}\r\n"
    "@{\"\"}\r\n\r\n";

static inline size_t mtw_locale_find_once(const unsigned char *data, size_t length,
                                          const unsigned char *needle, size_t needle_length) {
    size_t i, found = SIZE_MAX;
    for (i = 0; i + needle_length <= length; ++i)
        if (!memcmp(data + i, needle, needle_length)) {
            if (found != SIZE_MAX) return SIZE_MAX;
            found = i;
        }
    return found;
}

static inline int mtw_campaign_locale_transform(PatchContext *context,
                                                 const unsigned char *source, size_t source_length,
                                                 int enable, unsigned char **result,
                                                 size_t *result_length, PatchError *error) {
    char hash[65], next_hash[65];
    size_t label_at, tip_at, label_anchor_size = sizeof(mtw_locale_label_anchor) - 1u;
    size_t tip_anchor_size = sizeof(mtw_locale_tip_anchor) - 1u;
    size_t label_add_size = sizeof(mtw_locale_label_add) - 1u;
    size_t tip_add_size = sizeof(mtw_locale_tip_add) - 1u;
    size_t cursor = 0, out_cursor = 0, next_length, source_tip_add_size;
    const unsigned char *source_tip_add;
    unsigned char *next;
    int source_fast, source_previous_fast, source_stock;
    if (result) *result = NULL;
    if (!context || !source || !result || !result_length ||
        !patch_sha256_bytes(context, source, source_length, hash)) {
        patch_error_set(error, "invalid_argument", "Could not hash campaign localization input.", ERROR_INVALID_DATA);
        return 0;
    }
    source_fast = source_length == MTW_CAMPAIGN_LOCALE_FAST_SIZE &&
                  !strcmp(hash, MTW_CAMPAIGN_LOCALE_FAST_SHA256);
    source_previous_fast = source_length == MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SIZE &&
                           !strcmp(hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SHA256);
    source_stock = source_length == MTW_CAMPAIGN_LOCALE_STOCK_SIZE &&
                   !strcmp(hash, MTW_CAMPAIGN_LOCALE_STOCK_SHA256);
    if (!source_fast && !source_previous_fast && !source_stock) {
        patch_error_set(error, "unsupported_locale", "The English campaign text has an unsupported identity.", ERROR_INVALID_DATA);
        return 0;
    }
    if ((enable && source_fast) || (!enable && source_stock)) {
        if (!patch_alloc(context, source_length, 1, (void **)&next, error)) return 0;
        memcpy(next, source, source_length);
        *result = next; *result_length = source_length;
        return 1;
    }
    label_at = mtw_locale_find_once(source, source_length, mtw_locale_label_anchor, label_anchor_size);
    tip_at = mtw_locale_find_once(source, source_length, mtw_locale_tip_anchor, tip_anchor_size);
    if (label_at == SIZE_MAX || tip_at == SIZE_MAX || label_at + label_anchor_size >= tip_at) {
        patch_error_set(error, "transform_mismatch", "Campaign text anchors changed or are ambiguous.", ERROR_INVALID_DATA);
        return 0;
    }
    next_length = enable ? MTW_CAMPAIGN_LOCALE_FAST_SIZE : MTW_CAMPAIGN_LOCALE_STOCK_SIZE;
    source_tip_add = source_previous_fast ? mtw_locale_previous_tip_add : mtw_locale_tip_add;
    source_tip_add_size = source_previous_fast ? sizeof(mtw_locale_previous_tip_add) - 1u : tip_add_size;
    if (!patch_alloc(context, next_length, 1, (void **)&next, error)) return 0;
#define MTW_COPY(n) do { size_t copy_length = (n); \
    if (cursor + copy_length > source_length || out_cursor + copy_length > next_length) goto mismatch; \
    memcpy(next + out_cursor, source + cursor, copy_length); \
    cursor += copy_length; out_cursor += copy_length; } while (0)
    MTW_COPY(label_at + label_anchor_size);
    if (enable) {
        if (source_previous_fast) {
            if (memcmp(source + cursor, mtw_locale_label_add, label_add_size)) goto mismatch;
            MTW_COPY(label_add_size);
        } else {
            memcpy(next + out_cursor, mtw_locale_label_add, label_add_size);
            out_cursor += label_add_size;
        }
    } else {
        if (memcmp(source + cursor, mtw_locale_label_add, label_add_size)) goto mismatch;
        cursor += label_add_size;
    }
    MTW_COPY(tip_at + tip_anchor_size - cursor);
    if (!source_stock) {
        if (memcmp(source + cursor, source_tip_add, source_tip_add_size)) goto mismatch;
        cursor += source_tip_add_size;
    }
    if (enable) {
        memcpy(next + out_cursor, mtw_locale_tip_add, tip_add_size);
        out_cursor += tip_add_size;
    }
    MTW_COPY(source_length - cursor);
#undef MTW_COPY
    if (out_cursor != next_length || !patch_sha256_bytes(context, next, next_length, next_hash) ||
        strcmp(next_hash, enable ? MTW_CAMPAIGN_LOCALE_FAST_SHA256 : MTW_CAMPAIGN_LOCALE_STOCK_SHA256)) goto mismatch;
    *result = next; *result_length = next_length;
    return 1;
mismatch:
    patch_context_free(context, next);
    patch_error_set(error, "transform_mismatch", "Campaign text transform did not reproduce its pinned output.", ERROR_INVALID_DATA);
    return 0;
}
#endif
