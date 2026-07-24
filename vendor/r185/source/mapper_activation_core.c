#include "mapper_activation_core.h"

#define MTW_RESET_WIDTH 640u
#define MTW_RESET_HEIGHT 480u
#define MTW_RESET_PITCH 2560u
#define MTW_RESET_BYTES_PER_UNIT 4u

#define MTW_FRONTEND_WIDTH 800u
#define MTW_FRONTEND_HEIGHT 600u
#define MTW_FRONTEND_PITCH 1600u
#define MTW_FRONTEND_BYTES_PER_UNIT 2u

#define MTW_LOADING_BYTES_PER_PIXEL 2u
#define MTW_LOADING_MAX_DIMENSION 32768u
#define MTW_LOADING_MAX_BYTES 0x20000000u

int mtw_mapper_mode_after_surface(int current_mode,
                                  uint16_t width,
                                  uint16_t height,
                                  uint32_t pitch,
                                  uint8_t bytes_per_unit) {
    if ((width == MTW_RESET_WIDTH && height == MTW_RESET_HEIGHT &&
         pitch == MTW_RESET_PITCH &&
         bytes_per_unit == MTW_RESET_BYTES_PER_UNIT) ||
        (width == MTW_FRONTEND_WIDTH && height == MTW_FRONTEND_HEIGHT &&
         pitch == MTW_FRONTEND_PITCH &&
         bytes_per_unit == MTW_FRONTEND_BYTES_PER_UNIT)) {
        return 1;
    }
    return current_mode != 0;
}

int mtw_mapper_mode_after_loading(int current_mode,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t pitch) {
    uint32_t row_bytes;

    if (width == 0u || height == 0u ||
        width > MTW_LOADING_MAX_DIMENSION ||
        height > MTW_LOADING_MAX_DIMENSION ||
        width > UINT32_MAX / MTW_LOADING_BYTES_PER_PIXEL) {
        return current_mode != 0;
    }
    row_bytes = width * MTW_LOADING_BYTES_PER_PIXEL;
    if (pitch >= row_bytes && pitch != 0u &&
        height <= MTW_LOADING_MAX_BYTES / pitch) {
        return 0;
    }
    return current_mode != 0;
}
