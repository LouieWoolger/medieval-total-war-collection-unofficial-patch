#ifndef MTW_SCROLL_EXE_PATCH_H
#define MTW_SCROLL_EXE_PATCH_H

/* Reversible, identity-gated PE32 camera patch. No game image is distributed.
   The 591-byte code payload is generated from vendor/scroll_exe/scroll_hook.asm. */
#include "patch_identity.h"
#include "../vendor/scroll_exe/scroll_hook.inc"

#define MTW_SCROLL_STOCK_SHA256 "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
#define MTW_SCROLL_STOCK_SIZE 0x4E7000u
#define MTW_SCROLL_PATCHED_SIZE 0x4E9000u

static inline uint32_t mtw_scroll_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void mtw_scroll_put16(unsigned char *p, uint16_t n) {
    p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8);
}
static inline void mtw_scroll_put32(unsigned char *p, uint32_t n) {
    p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8);
    p[2] = (unsigned char)(n >> 16); p[3] = (unsigned char)(n >> 24);
}
static inline void mtw_scroll_section(unsigned char *p, const char *name,
                                       uint32_t virtual_size, uint32_t rva,
                                       uint32_t raw, uint32_t flags) {
    memset(p, 0, 40);
    memcpy(p, name, strlen(name));
    mtw_scroll_put32(p + 8, virtual_size);
    mtw_scroll_put32(p + 12, rva);
    mtw_scroll_put32(p + 16, 0x1000);
    mtw_scroll_put32(p + 20, raw);
    mtw_scroll_put32(p + 36, flags);
}

/* desired=1 patches stock; desired=0 restores stock. Already-correct exact
   identities are copied unchanged. Caller owns the returned PatchContext. */
static inline int mtw_scroll_transform(PatchContext *context,
                                       const unsigned char *source, size_t source_length,
                                       int desired, unsigned char **result,
                                       size_t *result_length, PatchError *error) {
    char source_hash[65], output_hash[65];
    unsigned char *target = NULL;
    size_t length;
    int stock, patched;
    static const unsigned char prologue[6] = {0x55,0x8B,0xEC,0x83,0xEC,0x08};
    static const unsigned char jump[6] = {0xE9,0x2B,0x42,0x92,0x00,0x90};
    if (result) *result = NULL;
    if (result_length) *result_length = 0;
    if (!context || !source || !result || !result_length || (desired != 0 && desired != 1)) {
        patch_error_set(error, "invalid_argument", "Invalid scroll EXE transform arguments.", ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!patch_sha256_bytes(context, source, source_length, source_hash)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    stock = source_length == MTW_SCROLL_STOCK_SIZE && !strcmp(source_hash, MTW_SCROLL_STOCK_SHA256);
    patched = source_length == MTW_SCROLL_PATCHED_SIZE && !strcmp(source_hash, MTW_SCROLL_PATCHED_SHA256);
    if (!stock && !patched) {
        patch_error_set(error, "unsupported_executable",
                        "Medieval_TW.exe is neither the supported stock EXE nor the exact direct-scroll patch.",
                        ERROR_INVALID_DATA);
        return 0;
    }
    length = desired ? MTW_SCROLL_PATCHED_SIZE : MTW_SCROLL_STOCK_SIZE;
    if (!patch_alloc(context, length, 1, (void **)&target, error)) return 0;
    memset(target, 0, length);
    memcpy(target, source, source_length < length ? source_length : length);
    if (desired && stock) {
        mtw_scroll_put16(target + 0x86, 8);
        mtw_scroll_put32(target + 0x9C, mtw_scroll_u32(target + 0x9C) + 0x1000);
        mtw_scroll_put32(target + 0xA0, mtw_scroll_u32(target + 0xA0) + 0x1000);
        mtw_scroll_put32(target + 0xD0, 0xB4E000);
        mtw_scroll_put32(target + 0xD8, 0x4EE44E);
        mtw_scroll_section(target + 0x268, ".mtwpan", MTW_SCROLL_HOOK_LENGTH,
                           0xB4C000, 0x4E7000, 0x60000020);
        mtw_scroll_section(target + 0x290, ".mtwdat", 0x1000,
                           0xB4D000, 0x4E8000, 0xC0000040);
        memcpy(target + 0x227DD0, jump, sizeof jump);
        memcpy(target + 0x4E7000, mtw_scroll_hook_bytes, MTW_SCROLL_HOOK_LENGTH);
    } else if (!desired && patched) {
        mtw_scroll_put16(target + 0x86, 6);
        mtw_scroll_put32(target + 0x9C, mtw_scroll_u32(target + 0x9C) - 0x1000);
        mtw_scroll_put32(target + 0xA0, mtw_scroll_u32(target + 0xA0) - 0x1000);
        mtw_scroll_put32(target + 0xD0, 0xB4C000);
        mtw_scroll_put32(target + 0xD8, 0x4F65E7);
        memset(target + 0x268, 0, 80);
        memcpy(target + 0x227DD0, prologue, sizeof prologue);
    }
    if (!patch_sha256_bytes(context, target, length, output_hash)) {
        patch_error_set(error, context->error.code, context->error.message, context->error.win32);
        return 0;
    }
    if (strcmp(output_hash, desired ? MTW_SCROLL_PATCHED_SHA256 : MTW_SCROLL_STOCK_SHA256)) {
        patch_error_set(error, "transform_mismatch",
                        "The direct-scroll EXE transform did not match its pinned output hash.",
                        ERROR_INVALID_DATA);
        return 0;
    }
    *result = target;
    *result_length = length;
    return 1;
}

#endif
