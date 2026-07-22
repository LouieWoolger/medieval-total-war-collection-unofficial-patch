#ifndef MTW_MAPPER_ACTIVATION_CORE_H
#define MTW_MAPPER_ACTIVATION_CORE_H

#include <stdint.h>

int mtw_mapper_mode_after_surface(int current_mode,
                                  uint16_t width,
                                  uint16_t height,
                                  uint32_t pitch,
                                  uint8_t bytes_per_unit);

int mtw_mapper_mode_after_loading(int current_mode,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t pitch);

#endif
