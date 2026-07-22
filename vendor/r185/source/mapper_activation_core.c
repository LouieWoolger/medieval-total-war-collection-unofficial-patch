#include "mapper_activation_core.h"

#define MTW_RESET_WIDTH 640u
#define MTW_RESET_HEIGHT 480u
#define MTW_RESET_PITCH 2560u
#define MTW_RESET_BYTES_PER_UNIT 4u

#define MTW_FRONTEND_WIDTH 800u
#define MTW_FRONTEND_HEIGHT 600u
#define MTW_FRONTEND_PITCH 1600u
#define MTW_FRONTEND_BYTES_PER_UNIT 2u

#define MTW_LOADING_WIDTH 2560u
#define MTW_LOADING_HEIGHT 1440u
#define MTW_LOADING_PITCH 5120u

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
    if (width == MTW_LOADING_WIDTH && height == MTW_LOADING_HEIGHT &&
        pitch == MTW_LOADING_PITCH) {
        return 0;
    }
    return current_mode != 0;
}
