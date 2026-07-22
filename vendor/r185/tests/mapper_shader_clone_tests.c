#include <stdint.h>
#include <stdio.h>

#include "mapper_shader_clone_core.h"

static uint32_t fnv1a32(const unsigned char *bytes, size_t length) {
    uint32_t value = 2166136261u;
    size_t index;
    for (index = 0u; index < length; ++index) {
        value ^= bytes[index];
        value *= 16777619u;
    }
    return value;
}

int main(int argc, char **argv) {
    static const unsigned char supported_callsite[MTW_MAPPER_CALLSITE_SIZE] = {
        0x8B, 0x08, 0x50, 0xFF, 0x51, 0x34
    };
    unsigned char corrupt_callsite[MTW_MAPPER_CALLSITE_SIZE] = {
        0x8B, 0x08, 0x50, 0xFF, 0x51, 0x35
    };
    const unsigned char *bytecode;
    size_t bytecode_size = 0u;
    unsigned char problem_shader[4212u];
    FILE *problem_file;

    if (!mtw_mapper_callsite_supported(supported_callsite,
                                       sizeof(supported_callsite))) return 1;
    if (mtw_mapper_callsite_supported(corrupt_callsite,
                                      sizeof(corrupt_callsite))) return 2;
    if (mtw_mapper_callsite_supported(supported_callsite,
                                      sizeof(supported_callsite) - 1u)) return 3;
    if (mtw_mapper_callsite_supported(NULL, sizeof(supported_callsite))) return 4;

    bytecode = mtw_mapper_pixel_shader_bytecode(&bytecode_size);
    if (bytecode == NULL || bytecode_size != 764u) return 5;
    if (bytecode[0] != 'D' || bytecode[1] != 'X' ||
        bytecode[2] != 'B' || bytecode[3] != 'C') return 6;
    if (fnv1a32(bytecode, bytecode_size) != 0x21300CB1u) return 7;
    if (!mtw_mapper_pixel_shader_matches(bytecode, bytecode_size)) return 8;
    if (mtw_mapper_pixel_shader_matches(bytecode, bytecode_size - 1u)) return 9;
    if (mtw_mapper_pixel_shader_matches(NULL, bytecode_size)) return 10;
    {
        unsigned char corrupt[764u];
        size_t index;
        for (index = 0u; index < bytecode_size; ++index) {
            corrupt[index] = bytecode[index];
        }
        corrupt[bytecode_size - 1u] ^= 1u;
        if (mtw_mapper_pixel_shader_matches(corrupt, bytecode_size)) return 11;
    }

    if (argc != 2 || argv == NULL || argv[1] == NULL) return 12;
    if (fopen_s(&problem_file, argv[1], "rb") != 0 ||
        problem_file == NULL) return 13;
    if (fread(problem_shader, 1u, sizeof(problem_shader), problem_file) !=
        sizeof(problem_shader)) {
        fclose(problem_file);
        return 14;
    }
    if (fgetc(problem_file) != EOF) {
        fclose(problem_file);
        return 15;
    }
    fclose(problem_file);
    if (!mtw_mapper_problem_pixel_shader_matches(
            problem_shader, sizeof(problem_shader))) return 16;
    if (mtw_mapper_problem_pixel_shader_matches(
            problem_shader, sizeof(problem_shader) - 1u)) return 17;
    if (mtw_mapper_problem_pixel_shader_matches(
            NULL, sizeof(problem_shader))) return 18;
    problem_shader[1024u] ^= 1u;
    if (mtw_mapper_problem_pixel_shader_matches(
            problem_shader, sizeof(problem_shader))) return 19;

    puts("targeted mapper shader identity and replacement tests passed");
    return 0;
}
