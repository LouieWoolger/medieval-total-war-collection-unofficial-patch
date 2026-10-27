#include "../src/game_data_patch.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    PatchContext context = {0};
    PatchError error = {0};
    FILE *input = NULL, *output = NULL;
    unsigned char *source = NULL, *result = NULL;
    long count;
    size_t result_length = 0;
    int kind, ok = 0;
    if (argc != 4) return 2;
    kind = !strcmp(argv[1], "heroes") ? MTW_GAME_DATA_HEROES :
           !strcmp(argv[1], "names") ? MTW_GAME_DATA_NAMES :
           !strcmp(argv[1], "region-titles") ? MTW_GAME_DATA_REGION_TITLES : -1;
    if (kind < 0 || !(input = fopen(argv[2], "rb")) || fseek(input, 0, SEEK_END) ||
        (count = ftell(input)) < 0 || count > 200000 || fseek(input, 0, SEEK_SET)) goto done;
    source = (unsigned char *)malloc((size_t)count);
    if (!source || fread(source, 1, (size_t)count, input) != (size_t)count) goto done;
    if (!mtw_game_data_transform(&context, source, (size_t)count, kind,
                                 &result, &result_length, &error)) goto done;
    output = fopen(argv[3], "wb");
    if (!output || fwrite(result, 1, result_length, output) != result_length || fflush(output)) goto done;
    ok = 1;
done:
    if (!ok) fprintf(stderr, "%s: %s\n", error.code ? error.code : "harness_error",
                     error.message ? error.message : "input or output failure");
    if (output) fclose(output);
    if (input) fclose(input);
    free(source);
    patch_context_close(&context);
    return ok ? 0 : 1;
}
