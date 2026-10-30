/* Credits image sequencing repair, independent of terrain and backend fixes.
 * The recurring caller's pre-morph copy shows the next picture before its
 * old-to-new blend first writes. Keep the old picture until the blend starts;
 * if morph construction fails, reproduce the original full copy instead. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/mtw_exe_variants.h"
#include "credits_flash_fix.h"

#ifndef MTW_XP_RUNTIME
#define MTW_XP_RUNTIME 0
#endif
#if MTW_XP_RUNTIME && !defined(CALG_SHA_256)
#define CALG_SHA_256 0x0000800cu
#endif

#if !defined(_M_IX86)
#error The credits hook applies only to the verified PE32 game.
#endif

#define MTW_GAME_BASE 0x00400000u
#define MTW_CREDITS_PREMORPH_COPY_RVA 0x00022908u
#define MTW_CREDITS_TIMER_INIT_CALL_RVA 0x00022959u
#define MTW_CREDITS_FULL_COPY_RVA 0x00003110u
#define MTW_CREDITS_TIMER_INIT_RVA 0x00018480u

#if MTW_XP_RUNTIME
static volatile LONG credits_once_state;
#else
static INIT_ONCE credits_once = INIT_ONCE_STATIC_INIT;
#endif
static int credits_installed;
static void *credits_original_full_copy;
static void *credits_original_timer_init;

/* ESI is the recurring credits state and EDI is the next image. ECX holds
 * the morph object produced immediately before the timer initialization.
 * The failure branch keeps the game's original static-image behavior. */
__declspec(naked) static void credits_timer_init_hook_stub(void) {
    __asm {
        test ecx, ecx
        jne credits_start_timer
        pushad
        mov ecx, dword ptr [esi + 0x34]
        push edi
        call dword ptr [credits_original_full_copy]
        popad
    credits_start_timer:
        jmp dword ptr [credits_original_timer_init]
    }
}

static int bytes_match(uintptr_t address,
                       const unsigned char *expected,
                       size_t size) {
    return address != 0u && expected != NULL &&
           memcmp((const void *)address, expected, size) == 0;
}

static int hash_game_file(const wchar_t *path, char hex[65],
                          size_t *file_length) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE bytes[32], buffer[16384];
    DWORD size = sizeof(bytes), read = 0;
    LARGE_INTEGER file_size;
    size_t i;
    static const char digits[] = "0123456789ABCDEF";
    int okay = 0;
    if (!path || !CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES,
                                       CRYPT_VERIFYCONTEXT)) goto done;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart < 0 ||
        (ULONGLONG)file_size.QuadPart > (ULONGLONG)SIZE_MAX) goto done;
    do {
        if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL)) goto done;
        if (read && !CryptHashData(hash, buffer, read, 0)) goto done;
    } while (read);
    if (!CryptGetHashParam(hash, HP_HASHVAL, bytes, &size, 0) || size != 32u)
        goto done;
    for (i = 0u; i < 32u; ++i) {
        hex[i * 2u] = digits[bytes[i] >> 4];
        hex[i * 2u + 1u] = digits[bytes[i] & 15u];
    }
    hex[64] = 0;
    *file_length = (size_t)file_size.QuadPart;
    okay = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return okay;
}

static int supported_image(uintptr_t base) {
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS32 *nt;
    wchar_t path[MAX_PATH];
    char hex[65];
    size_t length;
    MtwExeVariant variant;
    DWORD path_length;
    if (base != MTW_GAME_BASE) return 0;
    path_length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!path_length || path_length >= MAX_PATH ||
        !hash_game_file(path, hex, &length) ||
        !mtw_exe_variant_from_hash(hex, length, &variant)) return 0;
    dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew < (LONG)sizeof(*dos) || dos->e_lfanew > 4096) {
        return 0;
    }
    nt = (const IMAGE_NT_HEADERS32 *)(base + (uintptr_t)dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->OptionalHeader.SizeOfImage == variant.image_size &&
           nt->OptionalHeader.SizeOfImage > MTW_CREDITS_TIMER_INIT_CALL_RVA + 5u;
}

/* attempted distinguishes a prewrite conflict from a failed postwrite check.
 * Only a write made here may be rolled back. */
static int write_checked(uintptr_t site,
                         const unsigned char *expected,
                         const unsigned char *replacement,
                         size_t size,
                         int *attempted) {
    DWORD previous;
    DWORD ignored;
    BOOL restored;
    *attempted = 0;
    if (!bytes_match(site, expected, size) ||
        !VirtualProtect((void *)site, size, PAGE_EXECUTE_READWRITE,
                        &previous)) {
        return 0;
    }
    *attempted = 1;
    memcpy((void *)site, replacement, size);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, size);
    restored = VirtualProtect((void *)site, size, previous, &ignored);
    return restored && bytes_match(site, replacement, size);
}

static int restore_owned(uintptr_t site,
                         const unsigned char *original,
                         const unsigned char *own_patch,
                         size_t size) {
    DWORD previous;
    DWORD ignored;
    BOOL restored;
    if (bytes_match(site, original, size)) return 1;
    if (!bytes_match(site, own_patch, size) ||
        !VirtualProtect((void *)site, size, PAGE_EXECUTE_READWRITE,
                        &previous)) {
        return 0;
    }
    memcpy((void *)site, original, size);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, size);
    restored = VirtualProtect((void *)site, size, previous, &ignored);
    return restored && bytes_match(site, original, size);
}

static int install_credits_flash_repair(void) {
    static const unsigned char copy_expected[6] = {
        0x57u, 0xE8u, 0x02u, 0x08u, 0xFEu, 0xFFu
    };
    static const unsigned char copy_skipped[6] = {
        0x90u, 0x90u, 0x90u, 0x90u, 0x90u, 0x90u
    };
    static const unsigned char timer_expected[5] = {
        0xE8u, 0x22u, 0x5Bu, 0xFFu, 0xFFu
    };
    uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    uintptr_t copy_site;
    uintptr_t timer_site;
    intptr_t displacement;
    int32_t relative;
    unsigned char timer_patch[5];
    int timer_attempted = 0;
    int copy_attempted = 0;

    if (!supported_image(base)) return 0;
    copy_site = base + MTW_CREDITS_PREMORPH_COPY_RVA;
    timer_site = base + MTW_CREDITS_TIMER_INIT_CALL_RVA;
    if (!bytes_match(copy_site, copy_expected, sizeof(copy_expected)) ||
        !bytes_match(timer_site, timer_expected, sizeof(timer_expected))) {
        return 0;
    }
    displacement = (const unsigned char *)credits_timer_init_hook_stub -
                   ((const unsigned char *)timer_site + sizeof(timer_patch));
    if (displacement < INT32_MIN || displacement > INT32_MAX) return 0;
    relative = (int32_t)displacement;
    timer_patch[0] = 0xE8u;
    memcpy(&timer_patch[1], &relative, sizeof(relative));
    credits_original_full_copy = (void *)(base + MTW_CREDITS_FULL_COPY_RVA);
    credits_original_timer_init = (void *)(base + MTW_CREDITS_TIMER_INIT_RVA);

    /* The fallback must be in place before the recurring copy is omitted. */
    if (!write_checked(timer_site, timer_expected, timer_patch,
                       sizeof(timer_patch), &timer_attempted)) {
        if (timer_attempted &&
            !restore_owned(timer_site, timer_expected, timer_patch,
                           sizeof(timer_patch))) {
            OutputDebugStringA("credits timer rollback failed\n");
        }
        return 0;
    }
    if (!write_checked(copy_site, copy_expected, copy_skipped,
                       sizeof(copy_skipped), &copy_attempted)) {
        int copy_restored = !copy_attempted ||
            restore_owned(copy_site, copy_expected, copy_skipped,
                          sizeof(copy_skipped));
        int timer_restored = restore_owned(timer_site, timer_expected,
                                          timer_patch, sizeof(timer_patch));
        if (!copy_restored || !timer_restored) {
            OutputDebugStringA("credits active-copy rollback failed\n");
        }
        return 0;
    }
    return 1;
}

#if MTW_XP_RUNTIME
int mtw_credits_flash_install_once(void) {
    LONG state = InterlockedCompareExchange(&credits_once_state, 1, 0);
    if (state == 0) {
        credits_installed = install_credits_flash_repair();
        InterlockedExchange(&credits_once_state, 2);
    } else {
        /* Preserve the synchronous once contract for concurrent factories. */
        while (InterlockedCompareExchange(&credits_once_state, 2, 2) != 2)
            Sleep(0);
    }
    return credits_installed;
}
#else
static BOOL CALLBACK credits_install_once(PINIT_ONCE once, PVOID parameter,
                                          PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    credits_installed = install_credits_flash_repair();
    return TRUE;
}

int mtw_credits_flash_install_once(void) {
    if (!InitOnceExecuteOnce(&credits_once, credits_install_once, NULL, NULL)) {
        return 0;
    }
    return credits_installed;
}
#endif
