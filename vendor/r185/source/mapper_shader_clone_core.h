#ifndef MTW_MAPPER_SHADER_CLONE_CORE_H
#define MTW_MAPPER_SHADER_CLONE_CORE_H

#include <stddef.h>

#define MTW_MAPPER_CALLSITE_SIZE 6u

int mtw_mapper_callsite_supported(const unsigned char *bytes, size_t size);
const unsigned char *mtw_mapper_pixel_shader_bytecode(size_t *size);
int mtw_mapper_pixel_shader_matches(const void *bytes, size_t size);
int mtw_mapper_problem_pixel_shader_matches(const void *bytes, size_t size);

#endif
