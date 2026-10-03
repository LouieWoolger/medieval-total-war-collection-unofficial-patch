#ifndef MTW_PATCH_IDENTITY_H
#define MTW_PATCH_IDENTITY_H

/* Generic SHA-256 adapted from Unofficial Shogun Total War Collection Patch
   src/patch_identity.h at 013d5ed285d0eadcbf22803b17a663fa8231c570.
   Original SHA-256 implementation of FIPS 180-4. GPL-3.0; see LICENSE.
   Only generic SHA code is reused; game identity and lifecycle code are excluded.
   The PiSha256/init/update/final signatures and lowercase output are preserved.
   Medieval adapters below provide uppercase output and checked borrowed I/O. */
#include "patch_platform.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} PiSha256;

static inline uint32_t pi_rotr(uint32_t v, unsigned n)
{
    return (v >> n) | (v << (32U - n));
}

static inline void pi_sha256_block(PiSha256 *ctx, const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,
        0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
        0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,
        0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,
        0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
        0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,
        0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,
        0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
        0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    uint32_t w[64], a,b,c,d,e,f,g,h;
    size_t i;
    for (i=0; i<16; ++i) {
        size_t p=i*4;
        w[i]=((uint32_t)block[p]<<24)|((uint32_t)block[p+1]<<16)|
             ((uint32_t)block[p+2]<<8)|block[p+3];
    }
    for (i=16; i<64; ++i) {
        uint32_t s0=pi_rotr(w[i-15],7)^pi_rotr(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=pi_rotr(w[i-2],17)^pi_rotr(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=ctx->h[0];b=ctx->h[1];c=ctx->h[2];d=ctx->h[3];
    e=ctx->h[4];f=ctx->h[5];g=ctx->h[6];h=ctx->h[7];
    for (i=0; i<64; ++i) {
        uint32_t s1=pi_rotr(e,6)^pi_rotr(e,11)^pi_rotr(e,25);
        uint32_t t1=h+s1+((e&f)^((~e)&g))+k[i]+w[i];
        uint32_t s0=pi_rotr(a,2)^pi_rotr(a,13)^pi_rotr(a,22);
        uint32_t t2=s0+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->h[0]+=a;ctx->h[1]+=b;ctx->h[2]+=c;ctx->h[3]+=d;
    ctx->h[4]+=e;ctx->h[5]+=f;ctx->h[6]+=g;ctx->h[7]+=h;
}

static inline void pi_sha256_init(PiSha256 *ctx)
{
    static const uint32_t initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,
        0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memcpy(ctx->h,initial,sizeof(initial));ctx->bytes=0;ctx->used=0;
}

static inline void pi_sha256_update(PiSha256 *ctx, const unsigned char *bytes, size_t len)
{
    ctx->bytes+=(uint64_t)len;
    while (len) {
        size_t take=64-ctx->used;
        if (take>len) take=len;
        memcpy(ctx->block+ctx->used,bytes,take);
        ctx->used+=take;bytes+=take;len-=take;
        if (ctx->used==64) { pi_sha256_block(ctx,ctx->block);ctx->used=0; }
    }
}

static inline void pi_sha256_final(PiSha256 *ctx, char output[65])
{
    static const char hex[]="0123456789abcdef";
    uint64_t bits=ctx->bytes*8U;
    size_t i;
    ctx->block[ctx->used++]=0x80;
    if (ctx->used>56) {
        memset(ctx->block+ctx->used,0,64-ctx->used);
        pi_sha256_block(ctx,ctx->block);ctx->used=0;
    }
    memset(ctx->block+ctx->used,0,56-ctx->used);
    for (i=0; i<8; ++i) ctx->block[63-i]=(unsigned char)(bits>>(i*8));
    pi_sha256_block(ctx,ctx->block);
    for (i=0; i<32; ++i) {
        unsigned char v=(unsigned char)(ctx->h[i/4]>>(24-(i%4)*8));
        output[i*2]=hex[v>>4];output[i*2+1]=hex[v&15];
    }
    output[64]='\0';
}


static inline void patch_sha256_uppercase(char output[65])
{
    size_t i;
    for (i = 0; i < 64; ++i)
        if (output[i] >= 'a' && output[i] <= 'f') output[i] -= 'a' - 'A';
}

static inline int patch_sha256_bytes(PatchContext *context, const void *bytes,
                                     size_t length, char output[65])
{
    PiSha256 sha;
    if (output) output[0] = '\0';
    if (!context) return 0;
    if (!output || (!bytes && length)) {
        patch_error_set(&context->error, "invalid_argument", "Invalid SHA-256 buffer.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    pi_sha256_init(&sha);
    pi_sha256_update(&sha, (const unsigned char *)bytes, length);
    pi_sha256_final(&sha, output);
    patch_sha256_uppercase(output);
    return 1;
}

/* Hash the complete already-pinned file, restoring its original cursor on every
   read path. The caller owns and validates the handle, denies writers while
   hashing and keeps its guard/pin alive. No unchecked path opening is provided.
   A failed read or cursor restore publishes no digest; the first error wins. */
static inline int patch_sha256_handle(PatchContext *context, HANDLE file, char output[65])
{
    unsigned char buffer[32768];
    PiSha256 sha;
    LARGE_INTEGER zero, saved;
    DWORD got;
    int ok = 0;
    if (output) output[0] = '\0';
    if (!context) return 0;
    if (!output || !file || file == INVALID_HANDLE_VALUE) {
        patch_error_set(&context->error, "invalid_argument", "Invalid SHA-256 handle.",
                        ERROR_INVALID_PARAMETER);
        return 0;
    }
    zero.QuadPart = 0;
    if (!SetFilePointerEx(file, zero, &saved, FILE_CURRENT)) {
        DWORD native = GetLastError();
        patch_error_set(&context->error, "io_error", "Could not read the SHA-256 file position.", native);
        return 0;
    }
    if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN)) {
        DWORD native = GetLastError();
        patch_error_set(&context->error, "io_error", "Could not rewind the SHA-256 file.", native);
        goto cleanup;
    }
    pi_sha256_init(&sha);
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) {
            DWORD native = GetLastError();
            patch_error_set(&context->error, "io_error", "Could not read the SHA-256 file.", native);
            goto cleanup;
        }
        if (!got) break;
        if (sha.bytes > UINT64_MAX / 8 - got) {
            patch_error_set(&context->error, "io_error", "SHA-256 input exceeds the length limit.",
                            ERROR_FILE_TOO_LARGE);
            goto cleanup;
        }
        pi_sha256_update(&sha, buffer, got);
    }
    ok = 1;
cleanup:
    if (!SetFilePointerEx(file, saved, NULL, FILE_BEGIN)) {
        DWORD native = GetLastError();
        patch_error_set(&context->error, "io_error", "Could not restore the SHA-256 file position.", native);
        ok = 0;
    }
    if (ok) {
        pi_sha256_final(&sha, output);
        patch_sha256_uppercase(output);
    }
    return ok;
}

#endif
