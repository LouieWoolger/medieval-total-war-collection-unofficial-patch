#ifndef MTW_GAME_DATA_PATCH_H
#define MTW_GAME_DATA_PATCH_H

/* Byte-bounded repairs of supported game data files. The game files are
   read from the user's installation and are never distributed as payloads. */
#include "patch_files.h"

#define MTW_GAME_DATA_HEROES 0
#define MTW_GAME_DATA_NAMES 1
#define MTW_GAME_DATA_REGION_TITLES 2

#define MTW_HEROES_STOCK_SIZE 64281u
#define MTW_HEROES_FIXED_SIZE 64280u
#define MTW_HEROES_STOCK_SHA256 "62B181D0804E48ECB527CAC1DC38D5DBE6CC79A22B9C576530F6F1C842D6E6DF"
#define MTW_HEROES_FIXED_SHA256 "B9BB6F6000BB426736F2E113A0D05331A014C8351AB0D8C1B3FD2FE10705ADDB"
#define MTW_NAMES_STOCK_SIZE 147294u
#define MTW_NAMES_FIXED_SIZE 147295u
#define MTW_NAMES_STOCK_SHA256 "BDFB6F894C4B21C147A523A1FAD84B1A8F3CF8D809E98C69363266D4B17A7A76"
#define MTW_NAMES_CYD_SHA256 "A4CC2AF6ECA25792E5E0FC64055706B4C22763C4D4A51080F774B0A8420E080E"
#define MTW_NAMES_KILIJ_SHA256 "F2847372150BED92E36F87F629A955344D2DEF533D6FD5ED6A1C25EDECF532FA"
#define MTW_NAMES_FIXED_SHA256 "3421E5FE033FF3CD9D710FFA38F465B970060C4095C56B590C22AEDFEF4A6F15"
#define MTW_REGION_TITLES_SIZE 41495u
#define MTW_REGION_TITLES_STOCK_SHA256 "17CC768ABBBE45CFDBFA807A17FC27732A2E0D25251298A55EED869389972517"
#define MTW_REGION_TITLES_FIXED_SHA256 "F75F5DAFE7319715C81ABE87AC9F6D1058944AC505DA53E3C015A7EB353BF1F7"

static inline int mtw_game_data_supported_record(const PatchFileRecord *r, int kind) {
    if (!r || !r->exists) return 0;
    if (kind == MTW_GAME_DATA_HEROES)
        return (r->length == MTW_HEROES_STOCK_SIZE && !strcmp(r->sha256, MTW_HEROES_STOCK_SHA256)) ||
               (r->length == MTW_HEROES_FIXED_SIZE && !strcmp(r->sha256, MTW_HEROES_FIXED_SHA256));
    if (kind == MTW_GAME_DATA_NAMES)
        return (r->length == MTW_NAMES_STOCK_SIZE &&
                (!strcmp(r->sha256, MTW_NAMES_STOCK_SHA256) || !strcmp(r->sha256, MTW_NAMES_CYD_SHA256))) ||
               (r->length == MTW_NAMES_FIXED_SIZE &&
                (!strcmp(r->sha256, MTW_NAMES_KILIJ_SHA256) || !strcmp(r->sha256, MTW_NAMES_FIXED_SHA256)));
    if (kind == MTW_GAME_DATA_REGION_TITLES)
        return r->length == MTW_REGION_TITLES_SIZE &&
               (!strcmp(r->sha256, MTW_REGION_TITLES_STOCK_SHA256) ||
                !strcmp(r->sha256, MTW_REGION_TITLES_FIXED_SHA256));
    return 0;
}

typedef struct {
    const unsigned char *before, *after;
    size_t before_length, after_length, at;
} MtwGameDataEdit;

static inline size_t mtw_game_data_find_once(const unsigned char *bytes, size_t length,
                                             const unsigned char *needle, size_t needle_length) {
    size_t i, found = SIZE_MAX;
    for (i = 0; i + needle_length <= length; ++i) {
        if (!memcmp(bytes + i, needle, needle_length)) {
            if (found != SIZE_MAX) return SIZE_MAX;
            found = i;
        }
    }
    return found;
}

static inline int mtw_game_data_transform(PatchContext *context, const unsigned char *source,
                                          size_t source_length, int kind, unsigned char **result,
                                          size_t *result_length, PatchError *error) {
    static const unsigned char hero_before[] = "SET_FORENAMES::\tFN_SICILIAN\t14\t8";
    static const unsigned char hero_after[] = "SET_FORENAMES::\tFN_SICILIAN\t4\t8";
    static const unsigned char cyd_before[] = "{\"Cydwladwr\" (Compatriot)}";
    static const unsigned char cyd_after[] = "{\"Cydwladwr (Compatriot)\"}";
    static const unsigned char kilij_before[] = "{\"Kilij Arslan}";
    static const unsigned char kilij_after[] = "{\"Kilij Arslan\"}";
    static const unsigned char titles_before[] =
        "@{\" of Hungary\"}\r\n@{\" of Sicily\"}\r\n@{\" of Burgundy\"}\r\n"
        "@{\" of Golden Khanate\"}\r\n@{\" of Novgorod\"}\r\n@{\" of The Papal States\"}";
    static const unsigned char titles_after[] =
        "@{\" of Burgundy\"}\r\n@{\" of Golden Khanate\"}\r\n@{\" of Hungary\"}\r\n"
        "@{\" of Novgorod\"}\r\n@{\" of The Papal States\"}\r\n@{\" of Sicily\"}";
    MtwGameDataEdit edits[2];
    unsigned char *next;
    char input_hash[65], output_hash[65];
    size_t count = 0, i, cursor = 0, written = 0, final_size;
    int cyd_fixed = 0, kilij_fixed = 0;
    const char *expected;
    if (result) *result = NULL;
    if (result_length) *result_length = 0;
    if (!context || !source || !result || !result_length ||
        (kind != MTW_GAME_DATA_HEROES && kind != MTW_GAME_DATA_NAMES &&
         kind != MTW_GAME_DATA_REGION_TITLES) ||
        !patch_sha256_bytes(context, source, source_length, input_hash)) {
        patch_error_set(error, "invalid_argument", "Could not hash supported game data input.", ERROR_INVALID_DATA);
        return 0;
    }
    if (kind == MTW_GAME_DATA_HEROES) {
        if (source_length == MTW_HEROES_FIXED_SIZE && !strcmp(input_hash, MTW_HEROES_FIXED_SHA256)) {
            final_size = source_length;
        } else if (source_length == MTW_HEROES_STOCK_SIZE &&
                   !strcmp(input_hash, MTW_HEROES_STOCK_SHA256)) {
            edits[count++] = (MtwGameDataEdit){hero_before, hero_after,
                                               sizeof hero_before - 1u, sizeof hero_after - 1u, 0};
            final_size = MTW_HEROES_FIXED_SIZE;
        } else {
            patch_error_set(error, "unsupported_data", "The forename table is not an exact supported state. Preserve it and review any game-data mod before installing.", ERROR_INVALID_DATA);
            return 0;
        }
        expected = MTW_HEROES_FIXED_SHA256;
    } else if (kind == MTW_GAME_DATA_NAMES) {
        if (source_length == MTW_NAMES_STOCK_SIZE && !strcmp(input_hash, MTW_NAMES_STOCK_SHA256)) {
            /* Neither entry is corrected. */
        } else if (source_length == MTW_NAMES_STOCK_SIZE && !strcmp(input_hash, MTW_NAMES_CYD_SHA256)) {
            cyd_fixed = 1;
        } else if (source_length == MTW_NAMES_FIXED_SIZE && !strcmp(input_hash, MTW_NAMES_KILIJ_SHA256)) {
            kilij_fixed = 1;
        } else if (source_length == MTW_NAMES_FIXED_SIZE && !strcmp(input_hash, MTW_NAMES_FIXED_SHA256)) {
            cyd_fixed = kilij_fixed = 1;
        } else {
            patch_error_set(error, "unsupported_data", "The English names table is not an exact supported state. Preserve it and review any game-data mod before installing.", ERROR_INVALID_DATA);
            return 0;
        }
        if (!cyd_fixed)
            edits[count++] = (MtwGameDataEdit){cyd_before, cyd_after,
                                               sizeof cyd_before - 1u, sizeof cyd_after - 1u, 0};
        if (!kilij_fixed)
            edits[count++] = (MtwGameDataEdit){kilij_before, kilij_after,
                                               sizeof kilij_before - 1u, sizeof kilij_after - 1u, 0};
        final_size = MTW_NAMES_FIXED_SIZE;
        expected = MTW_NAMES_FIXED_SHA256;
    } else {
        if (source_length != MTW_REGION_TITLES_SIZE) {
            patch_error_set(error, "unsupported_data", "The English region text is not an exact supported state. Preserve it before installing.", ERROR_INVALID_DATA);
            return 0;
        }
        if (!strcmp(input_hash, MTW_REGION_TITLES_STOCK_SHA256))
            edits[count++] = (MtwGameDataEdit){titles_before, titles_after,
                                               sizeof titles_before - 1u, sizeof titles_after - 1u, 0};
        else if (strcmp(input_hash, MTW_REGION_TITLES_FIXED_SHA256)) {
            patch_error_set(error, "unsupported_data", "The English region text is not an exact supported state. Preserve it before installing.", ERROR_INVALID_DATA);
            return 0;
        }
        final_size = MTW_REGION_TITLES_SIZE;
        expected = MTW_REGION_TITLES_FIXED_SHA256;
    }
    for (i = 0; i < count; ++i) {
        edits[i].at = mtw_game_data_find_once(source, source_length, edits[i].before, edits[i].before_length);
        if (edits[i].at == SIZE_MAX) goto mismatch;
    }
    if (count == 2 && edits[1].at < edits[0].at) {
        MtwGameDataEdit temporary = edits[0];
        edits[0] = edits[1]; edits[1] = temporary;
    }
    if (!patch_alloc(context, final_size, 1, (void **)&next, error)) return 0;
    for (i = 0; i < count; ++i) {
        size_t plain;
        if (edits[i].at < cursor || edits[i].at + edits[i].before_length > source_length) goto allocated_mismatch;
        plain = edits[i].at - cursor;
        if (written + plain + edits[i].after_length > final_size) goto allocated_mismatch;
        memcpy(next + written, source + cursor, plain); written += plain;
        memcpy(next + written, edits[i].after, edits[i].after_length); written += edits[i].after_length;
        cursor = edits[i].at + edits[i].before_length;
    }
    if (written + source_length - cursor != final_size) goto allocated_mismatch;
    memcpy(next + written, source + cursor, source_length - cursor);
    if (!patch_sha256_bytes(context, next, final_size, output_hash) || strcmp(output_hash, expected))
        goto allocated_mismatch;
    *result = next; *result_length = final_size;
    return 1;
allocated_mismatch:
    patch_context_free(context, next);
mismatch:
    patch_error_set(error, "transform_mismatch", "Game data anchors changed or the resulting identity was not the pinned correction.", ERROR_INVALID_DATA);
    return 0;
}
#endif
