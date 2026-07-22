#ifndef MTW_FRONTEND_EPOCH_CORE_H
#define MTW_FRONTEND_EPOCH_CORE_H

#include <stdint.h>

int frontend_epoch_should_begin(uint32_t message,
                                uintptr_t wparam,
                                uint32_t live_frontend_generations);

#endif
