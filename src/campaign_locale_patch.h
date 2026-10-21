#ifndef MTW_CAMPAIGN_LOCALE_PATCH_H
#define MTW_CAMPAIGN_LOCALE_PATCH_H

/* The campaign option panel resolves labels and tooltips by keys in this
   exact English game asset. Ship only a bounded transform, never the asset. */
#include "patch_files.h"

#define MTW_CAMPAIGN_LOCALE_STOCK_SIZE 279934u
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SIZE 280133u
#define MTW_CAMPAIGN_LOCALE_FAST_SIZE 280162u
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SIZE 280149u
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SIZE 280377u
#define MTW_CAMPAIGN_LOCALE_R289_ZOOM_SIZE 280165u
#define MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SIZE 280393u
#define MTW_CAMPAIGN_LOCALE_ZOOM_SIZE 280157u
#define MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SIZE 280385u
#define MTW_CAMPAIGN_LOCALE_STOCK_SHA256 "A4ED1815E5BBE23DC0B4279AFB9398E1FABC6A9A1F2A014DB96F5F25E2B5B3DC"
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_SHA256 "6415A171371504CECCDDFD66F7CD78F31DBA3497DFED8607CA7AD146B93C6B56"
#define MTW_CAMPAIGN_LOCALE_FAST_SHA256 "2BC2C971F1AC2C564490A526C6E8B1AE255E2CECBC43B45EC0B6B45A8FDF0F5B"
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SHA256 "C65900DF6EEAE03CA8E515D79037AB789D8C262C95FE07AF69A66B6A0F00CEAB"
#define MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SHA256 "944AC79B88C97A59FEA1CF4F27B20E4011BC53A6F1A41DEB9411C44ED3CAF502"
#define MTW_CAMPAIGN_LOCALE_R289_ZOOM_SHA256 "6DC40BA54E4CED551FEFE6F76146813A7CAA351938860F952D48B8F6D0EB663A"
#define MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SHA256 "88ED2FCAAB75884954FD3730B9D3EA274956334AB26A5C1C10C643C830633BA7"
#define MTW_CAMPAIGN_LOCALE_ZOOM_SHA256 "F2724F3F76CABB6289C327229988CEADF04D1D820BFBC0E8D7BDE52207FBC3D3"
#define MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SHA256 "F8A6A90167B5E2DD90B1482E9FB5BFBC5B1A64FA2DB4949A84C859BE46B2E78D"

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
static const unsigned char mtw_locale_zoom_label_add[] =
    "[\"keep_zoom\"]\r\n{\"Persistent Campaign Zoom\"}\r\n\r\n";
static const unsigned char mtw_locale_zoom_tip_add[] =
    "@[\"keep_zoom_tt\"]\r\n"
    "@{\"When selected, the map returns to your zoom level after each End Year.\"}\r\n"
    "@{\"Turn it off to let the map zoom reset after the computer's turn.\"}\r\n"
    "@{\"\"}\r\n\r\n";
static const unsigned char mtw_locale_r289_zoom_tip_add[] =
    "@[\"keep_zoom_tt\"]\r\n"
    "@{\"When selected, the map returns to your current zoom level after each End Year.\"}\r\n"
    "@{\"Turn it off to let the map zoom reset after the computer's turn.\"}\r\n"
    "@{\"\"}\r\n\r\n";
static const unsigned char mtw_locale_previous_zoom_label_add[] =
    "[\"keep_zoom\"]\r\n{\"Keep campaign zoom.\"}\r\n\r\n";
static const unsigned char mtw_locale_previous_zoom_tip_add[] =
    "@[\"keep_zoom_tt\"]\r\n"
    "@{\"When selected, the campaign map returns to your chosen zoom after each turn.\"}\r\n"
    "@{\"Turn it off to use the usual zoom after computer moves.\"}\r\n"
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

static inline int mtw_campaign_locale_zoom_identity(const char *hash, uint64_t length, int fast) {
    return fast ?
        ((length == MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SIZE &&
          !strcmp(hash, MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SHA256)) ||
         (length == MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SIZE &&
          !strcmp(hash, MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SHA256)) ||
         (length == MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SIZE &&
          !strcmp(hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SHA256))) :
        ((length == MTW_CAMPAIGN_LOCALE_ZOOM_SIZE &&
          !strcmp(hash, MTW_CAMPAIGN_LOCALE_ZOOM_SHA256)) ||
         (length == MTW_CAMPAIGN_LOCALE_R289_ZOOM_SIZE &&
          !strcmp(hash, MTW_CAMPAIGN_LOCALE_R289_ZOOM_SHA256)) ||
         (length == MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SIZE &&
          !strcmp(hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SHA256)));
}

static inline int mtw_campaign_locale_transform_options(PatchContext *context,
                                                         const unsigned char *source, size_t source_length,
                                                         int fast_enabled, int zoom_enabled,
                                                         unsigned char **result,
                                                         size_t *result_length, PatchError *error) {
    char hash[65], next_hash[65];
    size_t label_at, tip_at, label_anchor_size = sizeof(mtw_locale_label_anchor) - 1u;
    size_t tip_anchor_size = sizeof(mtw_locale_tip_anchor) - 1u;
    size_t label_add_size = sizeof(mtw_locale_label_add) - 1u;
    size_t tip_add_size = sizeof(mtw_locale_tip_add) - 1u;
    size_t zoom_label_size = sizeof(mtw_locale_zoom_label_add) - 1u;
    size_t zoom_tip_size = sizeof(mtw_locale_zoom_tip_add) - 1u;
    size_t cursor = 0, out_cursor = 0, next_length, source_tip_add_size;
    size_t source_zoom_label_size, source_zoom_tip_size;
    const unsigned char *source_tip_add, *source_zoom_label, *source_zoom_tip;
    const char *expected_hash;
    unsigned char *next;
    int source_fast, source_zoom, source_r289_zoom, source_previous_zoom;
    int source_previous_fast, source_stock;
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
    source_zoom = (source_length == MTW_CAMPAIGN_LOCALE_ZOOM_SIZE &&
                   !strcmp(hash, MTW_CAMPAIGN_LOCALE_ZOOM_SHA256)) ||
                  (source_length == MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SIZE &&
                   !strcmp(hash, MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SHA256));
    source_r289_zoom = (source_length == MTW_CAMPAIGN_LOCALE_R289_ZOOM_SIZE &&
                        !strcmp(hash, MTW_CAMPAIGN_LOCALE_R289_ZOOM_SHA256)) ||
                       (source_length == MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SIZE &&
                        !strcmp(hash, MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SHA256));
    source_previous_zoom = (source_length == MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SIZE &&
                            !strcmp(hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_ZOOM_SHA256)) ||
                           (source_length == MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SIZE &&
                            !strcmp(hash, MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SHA256));
    if ((source_length == MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SIZE && source_zoom) ||
        (source_length == MTW_CAMPAIGN_LOCALE_R289_FAST_ZOOM_SIZE && source_r289_zoom) ||
        (source_length == MTW_CAMPAIGN_LOCALE_PREVIOUS_FAST_ZOOM_SIZE && source_previous_zoom))
        source_fast = 1;
    if (!source_fast && !source_zoom && !source_r289_zoom &&
        !source_previous_zoom && !source_previous_fast && !source_stock) {
        patch_error_set(error, "unsupported_locale", "The English campaign text has an unsupported identity.", ERROR_INVALID_DATA);
        return 0;
    }
    if ((fast_enabled != 0 && fast_enabled != 1) ||
        (zoom_enabled != 0 && zoom_enabled != 1)) {
        patch_error_set(error, "invalid_argument", "Campaign text selections must be Boolean.", ERROR_INVALID_DATA);
        return 0;
    }
    expected_hash = fast_enabled ?
        (zoom_enabled ? MTW_CAMPAIGN_LOCALE_FAST_ZOOM_SHA256 : MTW_CAMPAIGN_LOCALE_FAST_SHA256) :
        (zoom_enabled ? MTW_CAMPAIGN_LOCALE_ZOOM_SHA256 : MTW_CAMPAIGN_LOCALE_STOCK_SHA256);
    next_length = MTW_CAMPAIGN_LOCALE_STOCK_SIZE +
        (fast_enabled ? label_add_size + tip_add_size : 0u) +
        (zoom_enabled ? zoom_label_size + zoom_tip_size : 0u);
    if (!strcmp(hash, expected_hash)) {
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
    source_tip_add = source_previous_fast ? mtw_locale_previous_tip_add : mtw_locale_tip_add;
    source_tip_add_size = source_previous_fast ? sizeof(mtw_locale_previous_tip_add) - 1u : tip_add_size;
    source_zoom_label = source_previous_zoom ? mtw_locale_previous_zoom_label_add : mtw_locale_zoom_label_add;
    source_zoom_tip = source_previous_zoom ? mtw_locale_previous_zoom_tip_add :
                      (source_r289_zoom ? mtw_locale_r289_zoom_tip_add : mtw_locale_zoom_tip_add);
    source_zoom_label_size = source_previous_zoom ? sizeof(mtw_locale_previous_zoom_label_add) - 1u : zoom_label_size;
    source_zoom_tip_size = source_previous_zoom ? sizeof(mtw_locale_previous_zoom_tip_add) - 1u :
                           (source_r289_zoom ? sizeof(mtw_locale_r289_zoom_tip_add) - 1u : zoom_tip_size);
    if (!patch_alloc(context, next_length, 1, (void **)&next, error)) return 0;
#define MTW_COPY(n) do { size_t copy_length = (n); \
    if (cursor + copy_length > source_length || out_cursor + copy_length > next_length) goto mismatch; \
    memcpy(next + out_cursor, source + cursor, copy_length); \
    cursor += copy_length; out_cursor += copy_length; } while (0)
    MTW_COPY(label_at + label_anchor_size);
    if (source_fast || source_previous_fast) {
        if (memcmp(source + cursor, mtw_locale_label_add, label_add_size)) goto mismatch;
        cursor += label_add_size;
    }
    if (source_zoom || source_r289_zoom || source_previous_zoom) {
        if (memcmp(source + cursor, source_zoom_label, source_zoom_label_size)) goto mismatch;
        cursor += source_zoom_label_size;
    }
    if (fast_enabled) {
        memcpy(next + out_cursor, mtw_locale_label_add, label_add_size);
        out_cursor += label_add_size;
    }
    if (zoom_enabled) {
        memcpy(next + out_cursor, mtw_locale_zoom_label_add, zoom_label_size);
        out_cursor += zoom_label_size;
    }
    MTW_COPY(tip_at + tip_anchor_size - cursor);
    if (source_fast || source_previous_fast) {
        if (memcmp(source + cursor, source_tip_add, source_tip_add_size)) goto mismatch;
        cursor += source_tip_add_size;
    }
    if (source_zoom || source_r289_zoom || source_previous_zoom) {
        if (memcmp(source + cursor, source_zoom_tip, source_zoom_tip_size)) goto mismatch;
        cursor += source_zoom_tip_size;
    }
    if (fast_enabled) {
        memcpy(next + out_cursor, mtw_locale_tip_add, tip_add_size);
        out_cursor += tip_add_size;
    }
    if (zoom_enabled) {
        memcpy(next + out_cursor, mtw_locale_zoom_tip_add, zoom_tip_size);
        out_cursor += zoom_tip_size;
    }
    MTW_COPY(source_length - cursor);
#undef MTW_COPY
    if (out_cursor != next_length || !patch_sha256_bytes(context, next, next_length, next_hash) ||
        strcmp(next_hash, expected_hash)) goto mismatch;
    *result = next; *result_length = next_length;
    return 1;
mismatch:
    patch_context_free(context, next);
    patch_error_set(error, "transform_mismatch", "Campaign text transform did not reproduce its pinned output.", ERROR_INVALID_DATA);
    return 0;
}
static inline int mtw_campaign_locale_transform(PatchContext *context,
                                                 const unsigned char *source, size_t source_length,
                                                 int enable, unsigned char **result,
                                                 size_t *result_length, PatchError *error) {
    return mtw_campaign_locale_transform_options(context, source, source_length,
                                                  enable, 0, result, result_length, error);
}
#endif
