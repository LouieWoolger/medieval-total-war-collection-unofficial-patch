#include "frontend_epoch_core.h"

#define MTW_WM_KEYDOWN 0x0100u
#define MTW_WM_LBUTTONDOWN 0x0201u
#define MTW_WM_LBUTTONDBLCLK 0x0203u
#define MTW_VK_RETURN 0x0Du
#define MTW_VK_ESCAPE 0x1Bu

int frontend_epoch_should_begin(uint32_t message,
                                uintptr_t wparam,
                                uint32_t live_frontend_generations) {
    if (live_frontend_generations == 0u) return 0;
    if (message == MTW_WM_LBUTTONDOWN ||
        message == MTW_WM_LBUTTONDBLCLK) {
        return 1;
    }
    return message == MTW_WM_KEYDOWN &&
           (wparam == MTW_VK_ESCAPE || wparam == MTW_VK_RETURN);
}
