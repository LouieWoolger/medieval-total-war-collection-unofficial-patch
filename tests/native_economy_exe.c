/* Developer-only exact-state economy transform harness. No game EXE embedded. */
#include "../src/direct_exe_patch.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *input, *output;
    long size;
    unsigned char *bytes, *result;
    size_t length;
    int i;
    PatchContext context = {0};
    PatchError error = {0};
    if (argc != 8) return 2;
    for (i = 2; i <= 6; ++i)
        if ((argv[i][0] != '0' && argv[i][0] != '1') || argv[i][1]) return 2;
    input = fopen(argv[1], "rb");
    if (!input) return 3;
    if (fseek(input, 0, SEEK_END) || (size = ftell(input)) < 0 || fseek(input, 0, SEEK_SET)) return 4;
    bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, input) != (size_t)size) return 5;
    fclose(input);
    if (!mtw_direct_exe_transform_options(&context, bytes, (size_t)size,
                                           argv[2][0] - '0', argv[3][0] - '0',
                                           argv[4][0] - '0', argv[5][0] - '0',
                                           argv[6][0] - '0',
                                           &result, &length, &error)) {
        fprintf(stderr, "%s: %s\n", error.code ? error.code : "unknown",
                error.message ? error.message : "");
        return 6;
    }
    output = fopen(argv[7], "wb");
    if (!output || fwrite(result, 1, length, output) != length || fclose(output)) return 7;
    patch_context_close(&context);
    free(bytes);
    return 0;
}
