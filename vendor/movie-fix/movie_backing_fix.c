#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#include "movie_backing_fix.h"
#include "../../include/mtw_exe_variants.h"
#include "../r185/source/intro_backing_repair_core.h"

#if !defined(_M_IX86)
#error The movie backing fix is valid only for the verified PE32 game.
#endif

#define MTW_GAME_BASE 0x00400000u
#define MTW_GAME_TIMESTAMP 0x567DD42Fu
#define MTW_BACKING_WIDTH_RVA 0x004D01B4u
#define MTW_BACKING_HEIGHT_RVA 0x004D01B8u
#define MTW_BACKING_PITCH_RVA 0x004D0244u
#define MTW_BACKING_BITS_RVA 0x004D0248u
#define MTW_BACKING_OWNER_RVA 0x004DCE48u

typedef void *(__cdecl *mtw_allocator_fn)(uint32_t);
typedef void (__cdecl *mtw_deallocator_fn)(void *);

static INIT_ONCE install_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK backing_lock = SRWLOCK_INIT;
static mtw_intro_backing_tracker backing_tracker;
static mtw_allocator_fn game_allocate;
static mtw_deallocator_fn game_deallocate;
static volatile LONG installed;
static volatile LONG accepted;
static volatile LONG pitch_repairs;
static volatile LONG capacity_growths;
static volatile LONG failed_growths;
static volatile LONG fallback_hits;

static int sha256_file(const WCHAR *path, BYTE result[32], size_t *file_length) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE *object = NULL, *buffer = NULL;
    DWORD object_size = 0, hash_size = 0, returned = 0, read = 0;
    LARGE_INTEGER file_size;
    int okay = 0;
    if (!path || !result) return 0;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileSizeEx(file, &file_size) || file_size.QuadPart < 0 ||
        (ULONGLONG)file_size.QuadPart > (ULONGLONG)SIZE_MAX ||
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    NULL, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size,
                          sizeof object_size, &returned, 0) < 0 ||
        returned != sizeof object_size || !object_size ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_size,
                          sizeof hash_size, &returned, 0) < 0 ||
        returned != sizeof hash_size || hash_size != 32u)
        goto done;
    object = (BYTE *)HeapAlloc(GetProcessHeap(), 0, object_size);
    buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, 65536u);
    if (!object || !buffer ||
        BCryptCreateHash(algorithm, &hash, object, object_size,
                         NULL, 0, 0) < 0) goto done;
    do {
        if (!ReadFile(file, buffer, 65536u, &read, NULL)) goto done;
        if (read && BCryptHashData(hash, buffer, read, 0) < 0) goto done;
    } while (read);
    okay = BCryptFinishHash(hash, result, 32u, 0) >= 0;
    if (okay && file_length) *file_length = (size_t)file_size.QuadPart;
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (object) HeapFree(GetProcessHeap(), 0, object);
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return okay;
}

static int exact_game_executable(void) {
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)MTW_GAME_BASE;
    const IMAGE_NT_HEADERS32 *nt;
    WCHAR path[MAX_PATH];
    BYTE digest[32];
    char digest_hex[65];
    MtwExeVariant known;
    size_t i, file_length;
    static const char hex[] = "0123456789ABCDEF";
    if ((uintptr_t)GetModuleHandleW(NULL) != MTW_GAME_BASE ||
        dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 || dos->e_lfanew >= 0x1000) return 0;
    nt = (const IMAGE_NT_HEADERS32 *)(MTW_GAME_BASE + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.TimeDateStamp != MTW_GAME_TIMESTAMP ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != MTW_GAME_BASE) return 0;
    i = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!i || i >= MAX_PATH || !sha256_file(path, digest, &file_length)) return 0;
    for (i = 0; i < 32u; ++i) {
        digest_hex[i * 2u] = hex[digest[i] >> 4u];
        digest_hex[i * 2u + 1u] = hex[digest[i] & 15u];
    }
    digest_hex[64] = '\0';
    return mtw_exe_variant_from_hash(digest_hex, file_length, &known) &&
           known.image_size == nt->OptionalHeader.SizeOfImage;
}

static mtw_intro_backing_snapshot backing_snapshot(void) {
    mtw_intro_backing_snapshot state;
    state.backing = *(volatile uintptr_t *)(MTW_GAME_BASE + MTW_BACKING_OWNER_RVA);
    state.active = *(volatile uintptr_t *)(MTW_GAME_BASE + MTW_BACKING_BITS_RVA);
    state.width = *(volatile uint32_t *)(MTW_GAME_BASE + MTW_BACKING_WIDTH_RVA);
    state.height = *(volatile uint32_t *)(MTW_GAME_BASE + MTW_BACKING_HEIGHT_RVA);
    state.pitch = *(volatile uint32_t *)(MTW_GAME_BASE + MTW_BACKING_PITCH_RVA);
    return state;
}

static void * __cdecl backing_allocate_hook(uint32_t size) {
    void *pointer = game_allocate ? game_allocate(size) : NULL;
    DWORD error = GetLastError();
    AcquireSRWLockExclusive(&backing_lock);
    mtw_intro_backing_record_allocation(&backing_tracker,
                                        (uintptr_t)pointer, size);
    ReleaseSRWLockExclusive(&backing_lock);
    SetLastError(error);
    return pointer;
}

__declspec(noinline) static void __cdecl backing_after_lock(void) {
    DWORD error = GetLastError();
    mtw_intro_backing_snapshot surface, current;
    mtw_intro_backing_plan plan, check;
    mtw_intro_backing_tracker tracked;
    void *replacement;
    uintptr_t old_pointer = 0;
    int committed = 0;
    if (!game_allocate || !game_deallocate) goto done;
    surface = backing_snapshot();
    AcquireSRWLockExclusive(&backing_lock);
    mtw_intro_backing_reconcile(&backing_tracker, surface.backing);
    if (!mtw_intro_backing_make_plan(&backing_tracker, &surface, &plan)) {
        ReleaseSRWLockExclusive(&backing_lock);
        goto done;
    }
    InterlockedIncrement(&fallback_hits);
    if (!plan.grow) {
        if (plan.fix_pitch) {
            current = backing_snapshot();
            if (current.backing == surface.backing &&
                current.active == surface.active &&
                current.width == surface.width &&
                current.height == surface.height &&
                current.pitch == surface.pitch) {
                *(volatile uint32_t *)(MTW_GAME_BASE + MTW_BACKING_PITCH_RVA) =
                    plan.row_bytes;
                InterlockedIncrement(&pitch_repairs);
            }
        }
        ReleaseSRWLockExclusive(&backing_lock);
        goto done;
    }
    tracked = backing_tracker;
    ReleaseSRWLockExclusive(&backing_lock);
    replacement = game_allocate(plan.required_bytes);
    if (!replacement) {
        InterlockedIncrement(&failed_growths);
        goto done;
    }
    AcquireSRWLockExclusive(&backing_lock);
    current = backing_snapshot();
    if (backing_tracker.pointer == tracked.pointer &&
        backing_tracker.capacity_bytes == tracked.capacity_bytes &&
        current.backing == surface.backing &&
        current.active == surface.active &&
        current.width == surface.width &&
        current.height == surface.height &&
        current.pitch == surface.pitch &&
        mtw_intro_backing_make_plan(&backing_tracker, &current, &check) &&
        check.required_bytes == plan.required_bytes &&
        mtw_intro_backing_commit_growth(&backing_tracker, &current,
                                        (uintptr_t)replacement, &old_pointer)) {
        *(volatile uintptr_t *)(MTW_GAME_BASE + MTW_BACKING_OWNER_RVA) = current.backing;
        *(volatile uintptr_t *)(MTW_GAME_BASE + MTW_BACKING_BITS_RVA) = current.active;
        *(volatile uint32_t *)(MTW_GAME_BASE + MTW_BACKING_PITCH_RVA) = current.pitch;
        committed = 1;
    }
    ReleaseSRWLockExclusive(&backing_lock);
    if (committed) {
        game_deallocate((void *)old_pointer);
        InterlockedIncrement(&capacity_growths);
    } else if ((uintptr_t)replacement != tracked.pointer) {
        game_deallocate(replacement);
    }
done:
    SetLastError(error);
}

__declspec(naked) static void backing_return_stub(void) {
    __asm {
        pushfd
        pushad
        call backing_after_lock
        popad
        popfd
        pop ebx
        add esp, 0x7C
        ret
    }
}

static int patch_site(uintptr_t address, const unsigned char expected[5],
                      unsigned char opcode, const void *target) {
    unsigned char next[5];
    intptr_t displacement = (const BYTE *)target - ((const BYTE *)address + 5);
    DWORD protection, ignored;
    if (displacement < INT32_MIN || displacement > INT32_MAX ||
        memcmp((const void *)address, expected, 5u)) return 0;
    next[0] = opcode;
    memcpy(next + 1, &displacement, 4u);
    if (!VirtualProtect((void *)address, 5u, PAGE_EXECUTE_READWRITE,
                        &protection)) return 0;
    memcpy((void *)address, next, 5u);
    FlushInstructionCache(GetCurrentProcess(), (void *)address, 5u);
    VirtualProtect((void *)address, 5u, protection, &ignored);
    return memcmp((const void *)address, next, 5u) == 0;
}

static void restore_site(uintptr_t address, const unsigned char expected[5]) {
    DWORD protection, ignored;
    if (!VirtualProtect((void *)address, 5u, PAGE_EXECUTE_READWRITE,
                        &protection)) return;
    memcpy((void *)address, expected, 5u);
    FlushInstructionCache(GetCurrentProcess(), (void *)address, 5u);
    VirtualProtect((void *)address, 5u, protection, &ignored);
}

static BOOL CALLBACK install_callback(PINIT_ONCE once, PVOID parameter,
                                     PVOID *context) {
    uintptr_t first = MTW_GAME_BASE + MTW_INTRO_ALLOC_FIRST_RVA;
    uintptr_t second = MTW_GAME_BASE + MTW_INTRO_ALLOC_SECOND_RVA;
    uintptr_t return_first = MTW_GAME_BASE + MTW_INTRO_RETURN_FIRST_RVA;
    uintptr_t return_second = MTW_GAME_BASE + MTW_INTRO_RETURN_SECOND_RVA;
    HMODULE self;
    (void)once; (void)parameter; (void)context;
    if (!exact_game_executable()) return TRUE;
    InterlockedExchange(&accepted, 1);
    if (memcmp((const void *)first, mtw_intro_alloc_first_expected, 5u) ||
        memcmp((const void *)second, mtw_intro_alloc_second_expected, 5u) ||
        memcmp((const void *)return_first, mtw_intro_return_expected, 5u) ||
        memcmp((const void *)return_second, mtw_intro_return_expected, 5u) ||
        memcmp((const void *)(MTW_GAME_BASE + MTW_INTRO_ALLOCATOR_RVA),
               mtw_intro_allocator_expected, sizeof mtw_intro_allocator_expected) ||
        memcmp((const void *)(MTW_GAME_BASE + MTW_INTRO_DEALLOCATOR_RVA),
               mtw_intro_deallocator_expected, sizeof mtw_intro_deallocator_expected) ||
        memcmp((const void *)(MTW_GAME_BASE + MTW_INTRO_FIRST_PITCH_RVA),
               mtw_intro_first_pitch_expected, sizeof mtw_intro_first_pitch_expected) ||
        memcmp((const void *)(MTW_GAME_BASE + MTW_INTRO_SECOND_PITCH_RVA),
               mtw_intro_second_pitch_expected, sizeof mtw_intro_second_pitch_expected))
        return TRUE;
    /* The game may release a dynamically loaded D3D9 module while still
     * running. Pin the module before storing its code addresses in the EXE. */
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(const void *)&mtw_movie_backing_install_once,
                            &self)) return TRUE;
    game_allocate = (mtw_allocator_fn)(MTW_GAME_BASE + MTW_INTRO_ALLOCATOR_RVA);
    game_deallocate = (mtw_deallocator_fn)(MTW_GAME_BASE + MTW_INTRO_DEALLOCATOR_RVA);
    if (!patch_site(first, mtw_intro_alloc_first_expected, 0xE8u,
                    backing_allocate_hook)) return TRUE;
    if (!patch_site(second, mtw_intro_alloc_second_expected, 0xE8u,
                    backing_allocate_hook)) goto undo_first;
    if (!patch_site(return_first, mtw_intro_return_expected, 0xE9u,
                    backing_return_stub)) goto undo_second;
    if (!patch_site(return_second, mtw_intro_return_expected, 0xE9u,
                    backing_return_stub)) goto undo_return_first;
    InterlockedExchange(&installed, 1);
    return TRUE;
undo_return_first:
    restore_site(return_first, mtw_intro_return_expected);
undo_second:
    restore_site(second, mtw_intro_alloc_second_expected);
undo_first:
    restore_site(first, mtw_intro_alloc_first_expected);
    return TRUE;
}

int mtw_movie_backing_install_once(void) {
    InitOnceExecuteOnce(&install_once, install_callback, NULL, NULL);
    return InterlockedCompareExchange(&installed, 0, 0) != 0;
}

DWORD mtw_movie_backing_status(DWORD field) {
    switch (field) {
    case 0: return (DWORD)InterlockedCompareExchange(&installed, 0, 0);
    case 1: return (DWORD)InterlockedCompareExchange(&pitch_repairs, 0, 0);
    case 2: return (DWORD)InterlockedCompareExchange(&capacity_growths, 0, 0);
    case 3: return (DWORD)InterlockedCompareExchange(&failed_growths, 0, 0);
    case 4: return (DWORD)InterlockedCompareExchange(&accepted, 0, 0);
    case 5: return (DWORD)InterlockedCompareExchange(&fallback_hits, 0, 0);
    default: return 0;
    }
}
