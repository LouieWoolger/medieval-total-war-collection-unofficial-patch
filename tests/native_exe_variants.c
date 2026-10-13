#include "../include/mtw_exe_variants.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    MtwExeVariant variant;
    char *end = NULL;
    unsigned long length;
    if (argc != 3) return 1;
    length = strtoul(argv[2], &end, 10);
    if (!end || *end || !mtw_exe_variant_from_hash(argv[1], (size_t)length, &variant)) return 2;
    printf("%u %u %s\n", variant.scroll, variant.sprite, variant.checksum_style);
    return 0;
}
