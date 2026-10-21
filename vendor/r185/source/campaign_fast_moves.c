/* Optional campaign presentation feature for the supported Medieval PE32 build.
 * All absolute hooks require a full on-disk EXE hash, locale hash and exact
 * instruction bytes. The sibling dgVoodoo backend is not required here. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <string.h>
#include "../../../include/mtw_exe_variants.h"
#include "../../dust-fidelity/minhook/include/MinHook.h"
#include "campaign_fast_moves_core.h"

#define MTW_SHOW_MOVES_ADDRESS 0x007CD498u
#define MTW_ADD_OPTION_ADDRESS 0x0062F410u
#define MTW_CLICK_OPTION_ADDRESS 0x0062F960u
#define MTW_DRAW_TEXT_ADDRESS 0x00416630u
#define MTW_MOVE_INCREMENT_ADDRESS 0x006277A9u
#define MTW_MOVE_CONTINUE_ADDRESS 0x00627884u
#define MTW_ZOOM_CAPTURE_ADDRESS 0x005FCB01u
#define MTW_ZOOM_RESTORE_ADDRESS 0x005FCCCCu
#define MTW_ZOOM_TARGET_ADDRESS 0x008D6768u
#define MTW_ZOOM_CAPTURE_CONTINUE 0x005FCB06u
#define MTW_ZOOM_RESTORE_CONTINUE 0x005FCCD6u
#define MTW_LOCALE_HASH "2BC2C971F1AC2C564490A526C6E8B1AE255E2CECBC43B45EC0B6B45A8FDF0F5B"
#define MTW_LOCALE_ZOOM_HASH "F2724F3F76CABB6289C327229988CEADF04D1D820BFBC0E8D7BDE52207FBC3D3"
#define MTW_LOCALE_FAST_ZOOM_HASH "F8A6A90167B5E2DD90B1482E9FB5BFBC5B1A64FA2DB4949A84C859BE46B2E78D"

typedef void (__thiscall *mtw_add_option_fn)(void *, unsigned char *, const char *);
typedef void (__thiscall *mtw_click_option_fn)(void *, int, int);
typedef int (__thiscall *mtw_draw_text_fn)(void *, void *, const char *, int, int,
                                         int, int, int, int, int);

static mtw_add_option_fn mtw_original_add;
static mtw_click_option_fn mtw_original_click;
static mtw_draw_text_fn mtw_original_draw;
static unsigned char mtw_fast_enabled = 1;
static unsigned char mtw_zoom_enabled;
static unsigned char mtw_fast_available;
static unsigned char mtw_zoom_available;
static unsigned char mtw_zoom_armed;
static int32_t mtw_saved_zoom;
static volatile LONG mtw_install_attempted;
static volatile LONG mtw_installed;
static LARGE_INTEGER mtw_qpc_frequency;
static mtw_fast_move_slot mtw_slow_actions[256];
static const uintptr_t mtw_move_continue = MTW_MOVE_CONTINUE_ADDRESS;
static const uintptr_t mtw_zoom_capture_continue = MTW_ZOOM_CAPTURE_CONTINUE;
static const uintptr_t mtw_zoom_restore_continue = MTW_ZOOM_RESTORE_CONTINUE;
static uint64_t mtw_focus_checked_ms;
static int mtw_focused = 1;
static const unsigned char mtw_add_bytes[] = {0x53,0x8B,0xD9,0x8B,0x43,0x6C,0x83,0xF8,0x0C};
static const unsigned char mtw_click_bytes[] = {0x51,0xA1,0x58,0x68,0x8D,0x00,0x53,0x33,0xD2};
static const unsigned char mtw_draw_bytes[] = {0xA0,0x3C,0xCF,0x8D,0x00,0x83,0xEC,0x7C};
static const unsigned char mtw_move_bytes[] = {0x66,0xFF,0x40,0x2E,0xE9,0xD2,0x00,0x00,0x00};
static const unsigned char mtw_zoom_capture_bytes[] = {0xA0,0x98,0xD4,0x7C,0x00};
static const unsigned char mtw_zoom_restore_bytes[] = {0xC7,0x05,0x68,0x67,0x8D,0x00,0x80,0xF3,0xFF,0xFF};
static const wchar_t mtw_preference_key[] =
    L"Software\\LouieWoolger\\Medieval Total War Unofficial Patch";
static const wchar_t mtw_preference_name[] = L"FastComputerMoves";
static const wchar_t mtw_zoom_preference_name[] = L"KeepCampaignZoom";
static const char mtw_fast_key[] = "fast_moves";
static const char mtw_zoom_key[] = "keep_zoom";

static int mtw_hash_file(const wchar_t *path, char hex[65], size_t *file_length) {
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
        file_size.QuadPart < 0 || (ULONGLONG)file_size.QuadPart > (ULONGLONG)SIZE_MAX) goto done;
    do {
        if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL)) goto done;
        if (read && !CryptHashData(hash, buffer, read, 0)) goto done;
    } while (read);
    if (!CryptGetHashParam(hash, HP_HASHVAL, bytes, &size, 0) || size != 32) goto done;
    for (i = 0; i < 32; ++i) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 15];
    }
    hex[64] = 0;
    if (file_length) *file_length = (size_t)file_size.QuadPart;
    okay = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return okay;
}

static int mtw_supported_inputs(void) {
    wchar_t path[MAX_PATH], *filename;
    char hex[65];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    size_t file_length;
    MtwExeVariant supported;
    if ((uintptr_t)GetModuleHandleW(NULL) != 0x00400000u ||
        !length || length >= MAX_PATH || !mtw_hash_file(path, hex, &file_length) ||
        !mtw_exe_variant_from_hash(hex, file_length, &supported)) return 0;
    filename = wcsrchr(path, L'\\');
    if (!filename || (size_t)(filename - path) +
        wcslen(L"\\Loc\\Eng\\CHANGES.TXT") + 1u >= MAX_PATH) return 0;
    if (wcscpy_s(filename, MAX_PATH - (size_t)(filename - path),
                 L"\\Loc\\Eng\\CHANGES.TXT")) return 0;
    if (!mtw_hash_file(path, hex, NULL)) return 0;
    mtw_fast_available = !strcmp(hex, MTW_LOCALE_HASH) ||
                         !strcmp(hex, MTW_LOCALE_FAST_ZOOM_HASH);
    mtw_zoom_available = !strcmp(hex, MTW_LOCALE_ZOOM_HASH) ||
                         !strcmp(hex, MTW_LOCALE_FAST_ZOOM_HASH);
    if (!mtw_fast_available && !mtw_zoom_available) return 0;
    return !memcmp((void *)MTW_ADD_OPTION_ADDRESS, mtw_add_bytes, sizeof(mtw_add_bytes)) &&
           !memcmp((void *)MTW_CLICK_OPTION_ADDRESS, mtw_click_bytes, sizeof(mtw_click_bytes)) &&
           !memcmp((void *)MTW_DRAW_TEXT_ADDRESS, mtw_draw_bytes, sizeof(mtw_draw_bytes)) &&
           (!mtw_fast_available ||
            !memcmp((void *)MTW_MOVE_INCREMENT_ADDRESS, mtw_move_bytes, sizeof(mtw_move_bytes))) &&
           (!mtw_zoom_available ||
            (!memcmp((void *)MTW_ZOOM_CAPTURE_ADDRESS, mtw_zoom_capture_bytes,
                     sizeof(mtw_zoom_capture_bytes)) &&
             !memcmp((void *)MTW_ZOOM_RESTORE_ADDRESS, mtw_zoom_restore_bytes,
                     sizeof(mtw_zoom_restore_bytes))));
}

static unsigned char mtw_load_preference(const wchar_t *name, DWORD initial) {
    HKEY key = NULL;
    DWORD value = initial, type = 0u, size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, mtw_preference_key, 0, KEY_QUERY_VALUE,
                      &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, name, NULL, &type,
                            (BYTE *)&value, &size) != ERROR_SUCCESS ||
            type != REG_DWORD || size != sizeof(value) || value > 1u) value = initial;
        RegCloseKey(key);
    }
    return (unsigned char)value;
}

static int mtw_store_preference(const wchar_t *name, unsigned char value) {
    HKEY key = NULL;
    DWORD saved = value;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, mtw_preference_key, 0,
                                  NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS) return 0;
    result = RegSetValueExW(key, name, 0, REG_DWORD,
                            (const BYTE *)&saved, sizeof(saved));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static void __fastcall mtw_add_option_hook(void *panel, void *unused,
                                           unsigned char *flag, const char *key) {
    (void)unused;
    mtw_original_add(panel, flag, key);
    if (flag == (unsigned char *)MTW_SHOW_MOVES_ADDRESS && key &&
        !strcmp(key, "show_moves")) {
        if (mtw_fast_available)
            mtw_original_add(panel, &mtw_fast_enabled, mtw_fast_key);
        if (mtw_zoom_available)
            mtw_original_add(panel, &mtw_zoom_enabled, mtw_zoom_key);
    }
}

static void __fastcall mtw_click_option_hook(void *panel, void *unused, int x, int y) {
    unsigned char before = mtw_fast_enabled;
    unsigned char before_zoom = mtw_zoom_enabled;
    (void)unused;
    mtw_original_click(panel, x, y);
    if (mtw_fast_available && mtw_fast_enabled != before &&
        (!*(const unsigned char *)MTW_SHOW_MOVES_ADDRESS ||
         !mtw_store_preference(mtw_preference_name, mtw_fast_enabled)))
        mtw_fast_enabled = before;
    if (mtw_zoom_available && mtw_zoom_enabled != before_zoom &&
        !mtw_store_preference(mtw_zoom_preference_name, mtw_zoom_enabled))
        mtw_zoom_enabled = before_zoom;
}

static int __fastcall mtw_draw_text_hook(void *font, void *unused, void *surface,
                                        const char *text, int x, int y, int width,
                                        int height, int align, int display, int extra) {
    uint32_t previous[3];
    uint32_t *const red = (uint32_t *)((char *)font + 0x0c);
    uint32_t *const green = (uint32_t *)((char *)font + 0x10);
    uint32_t *const blue = (uint32_t *)((char *)font + 0x14);
    int result;
    (void)unused;
    if (!mtw_fast_available || *(const unsigned char *)MTW_SHOW_MOVES_ADDRESS || !text ||
        strcmp(text, "Fast computer moves."))
        return mtw_original_draw(font, surface, text, x, y, width, height,
                                 align, display, extra);
    previous[0] = *red;
    previous[1] = *green;
    previous[2] = *blue;
    /* The native software glyph blitter uses four-bit channel levels. 8 maps
     * through its RGB565 tables to neutral middle grey (0x8410). */
    *red = *green = *blue = 8;
    result = mtw_original_draw(font, surface, text, x, y, width, height,
                               align, display, extra);
    *red = previous[0];
    *green = previous[1];
    *blue = previous[2];
    return result;
}

static uint64_t mtw_monotonic_ms(void) {
    LARGE_INTEGER now;
    uint64_t ticks, frequency;
    if (!mtw_qpc_frequency.QuadPart || !QueryPerformanceCounter(&now) ||
        now.QuadPart < 0) return 0;
    ticks = (uint64_t)now.QuadPart;
    frequency = (uint64_t)mtw_qpc_frequency.QuadPart;
    return ticks / frequency * 1000u + ticks % frequency * 1000u / frequency;
}

static void __cdecl mtw_move_increment_hook(void *sprite) {
    unsigned char *object = (unsigned char *)sprite;
    unsigned char *action;
    uint16_t *counter;
    uint64_t now;
    mtw_fast_move_result result;
    DWORD foreground_pid = 0;
    if (!object) return;
    counter = (uint16_t *)(object + 0x2e);
    action = *(unsigned char **)(object + 0x34);
    if (!action || !mtw_qpc_frequency.QuadPart) {
        ++*counter;
        return;
    }
    now = mtw_monotonic_ms();
    if (!now) {
        ++*counter;
        return;
    }
    if (!mtw_focus_checked_ms || now < mtw_focus_checked_ms ||
        now - mtw_focus_checked_ms >= 50u) {
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
        mtw_focused = foreground_pid == GetCurrentProcessId();
        mtw_focus_checked_ms = now;
    }
    result = mtw_fast_move_step(mtw_slow_actions,
        sizeof(mtw_slow_actions) / sizeof(mtw_slow_actions[0]),
        (uintptr_t)object, (uintptr_t)action,
        *(int32_t *)(action + 4), *(int32_t *)(action + 8),
        *(int32_t *)(action + 0x44), *(int32_t *)(action + 0x48),
        *counter, now, mtw_focused,
        *(const unsigned char *)MTW_SHOW_MOVES_ADDRESS, mtw_fast_enabled);
    if (!result.override) {
        ++*counter;
        return;
    }
    *(int32_t *)object = result.x;
    *(int32_t *)(object + 4) = result.y;
    *counter = result.counter;
}

__declspec(naked) static void mtw_move_increment_stub(void) {
    __asm {
        pushfd
        pushad
        push eax
        call mtw_move_increment_hook
        add esp, 4
        popad
        popfd
        jmp dword ptr [mtw_move_continue]
    }
}

static int mtw_patch_move_increment(int *write_attempted) {
    unsigned char jump[sizeof(mtw_move_bytes)] = {0xE9};
    DWORD old_protect, ignored;
    intptr_t delta = (intptr_t)(void *)mtw_move_increment_stub -
                     (intptr_t)(MTW_MOVE_INCREMENT_ADDRESS + 5u);
    int32_t relative = (int32_t)delta;
    if ((intptr_t)relative != delta ||
        memcmp((void *)MTW_MOVE_INCREMENT_ADDRESS, mtw_move_bytes,
               sizeof(mtw_move_bytes))) return 0;
    memcpy(jump + 1, &relative, sizeof(relative));
    memset(jump + 5, 0x90, sizeof(jump) - 5);
    if (!VirtualProtect((void *)MTW_MOVE_INCREMENT_ADDRESS, sizeof(jump),
                        PAGE_EXECUTE_READWRITE, &old_protect)) return 0;
    *write_attempted = 1;
    memcpy((void *)MTW_MOVE_INCREMENT_ADDRESS, jump, sizeof(jump));
    FlushInstructionCache(GetCurrentProcess(), (void *)MTW_MOVE_INCREMENT_ADDRESS,
                          sizeof(jump));
    VirtualProtect((void *)MTW_MOVE_INCREMENT_ADDRESS, sizeof(jump),
                   old_protect, &ignored);
    return !memcmp((void *)MTW_MOVE_INCREMENT_ADDRESS, jump, sizeof(jump));
}

/* The original tour changes the zoom target to -4000 for AI movement and
 * writes -3200 before returning to the player. Capture once at the tour gate;
 * substitute the target at the existing restore site, before its camera call.
 * Other camera motion and all event/AI camera choices remain game-owned. */
__declspec(naked) static void mtw_zoom_capture_stub(void) {
    __asm {
        pushfd
        push ecx
        mov ecx, dword ptr ds:[MTW_ZOOM_TARGET_ADDRESS]
        mov dword ptr [mtw_saved_zoom], ecx
        mov byte ptr [mtw_zoom_armed], 1
        pop ecx
        popfd
        mov al, byte ptr ds:[MTW_SHOW_MOVES_ADDRESS]
        jmp dword ptr [mtw_zoom_capture_continue]
    }
}

__declspec(naked) static void mtw_zoom_restore_stub(void) {
    __asm {
        pushfd
        push eax
        cmp byte ptr [mtw_zoom_armed], 0
        je original_zoom
        cmp byte ptr [mtw_zoom_enabled], 0
        je original_zoom
        mov eax, dword ptr [mtw_saved_zoom]
        mov dword ptr ds:[MTW_ZOOM_TARGET_ADDRESS], eax
        jmp zoom_done
    original_zoom:
        mov dword ptr ds:[MTW_ZOOM_TARGET_ADDRESS], 0FFFFF380h
    zoom_done:
        mov byte ptr [mtw_zoom_armed], 0
        pop eax
        popfd
        jmp dword ptr [mtw_zoom_restore_continue]
    }
}

static int mtw_restore_code(uintptr_t address, const unsigned char *bytes, size_t length) {
    DWORD old_protect, ignored;
    if (!VirtualProtect((void *)address, length, PAGE_EXECUTE_READWRITE,
                        &old_protect)) return 0;
    memcpy((void *)address, bytes, length);
    FlushInstructionCache(GetCurrentProcess(), (void *)address, length);
    VirtualProtect((void *)address, length, old_protect, &ignored);
    return !memcmp((void *)address, bytes, length);
}

static int mtw_patch_jump(uintptr_t address, const unsigned char *expected,
                          size_t length, void *stub, int *write_attempted) {
    unsigned char jump[sizeof(mtw_zoom_restore_bytes)] = {0xE9};
    intptr_t delta = (intptr_t)stub - (intptr_t)(address + 5u);
    int32_t relative = (int32_t)delta;
    if (length < 5u || length > sizeof(jump) || (intptr_t)relative != delta ||
        memcmp((void *)address, expected, length)) return 0;
    memcpy(jump + 1, &relative, sizeof(relative));
    memset(jump + 5, 0x90, length - 5u);
    *write_attempted = 1;
    return mtw_restore_code(address, jump, length);
}

static int mtw_patch_zoom(int *capture_attempted, int *restore_attempted) {
    if (!mtw_patch_jump(MTW_ZOOM_CAPTURE_ADDRESS, mtw_zoom_capture_bytes,
                        sizeof(mtw_zoom_capture_bytes), mtw_zoom_capture_stub,
                        capture_attempted)) return 0;
    if (!mtw_patch_jump(MTW_ZOOM_RESTORE_ADDRESS, mtw_zoom_restore_bytes,
                        sizeof(mtw_zoom_restore_bytes), mtw_zoom_restore_stub,
                        restore_attempted)) return 0;
    return 1;
}

int mtw_campaign_fast_moves_install(void) {
    MH_STATUS status;
    int add_created = 0, click_created = 0, draw_created = 0;
    int move_write_attempted = 0, zoom_capture_attempted = 0, zoom_restore_attempted = 0;
    if (InterlockedCompareExchange(&mtw_install_attempted, 1, 0)) return mtw_installed != 0;
    if (!mtw_supported_inputs()) return 0;
    if (mtw_fast_available && (!QueryPerformanceFrequency(&mtw_qpc_frequency) ||
        mtw_qpc_frequency.QuadPart <= 0)) return 0;
    if (mtw_fast_available)
        mtw_fast_enabled = mtw_load_preference(mtw_preference_name, 1u);
    if (mtw_zoom_available)
        mtw_zoom_enabled = mtw_load_preference(mtw_zoom_preference_name, 0u);
    status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return 0;
    if (MH_CreateHook((void *)MTW_ADD_OPTION_ADDRESS, mtw_add_option_hook,
                      (void **)&mtw_original_add) != MH_OK) goto failed;
    add_created = 1;
    if (MH_CreateHook((void *)MTW_CLICK_OPTION_ADDRESS, mtw_click_option_hook,
                      (void **)&mtw_original_click) != MH_OK) goto failed;
    click_created = 1;
    if (MH_CreateHook((void *)MTW_DRAW_TEXT_ADDRESS, mtw_draw_text_hook,
                      (void **)&mtw_original_draw) != MH_OK) goto failed;
    draw_created = 1;
    if (MH_EnableHook((void *)MTW_ADD_OPTION_ADDRESS) != MH_OK ||
        MH_EnableHook((void *)MTW_CLICK_OPTION_ADDRESS) != MH_OK ||
        MH_EnableHook((void *)MTW_DRAW_TEXT_ADDRESS) != MH_OK) goto failed;
    if (mtw_zoom_available) {
        if (!mtw_patch_zoom(&zoom_capture_attempted, &zoom_restore_attempted)) goto failed;
    }
    if (mtw_fast_available && !mtw_patch_move_increment(&move_write_attempted)) goto failed;
    InterlockedExchange(&mtw_installed, 1);
    return 1;
failed:
    if (move_write_attempted)
        mtw_restore_code(MTW_MOVE_INCREMENT_ADDRESS, mtw_move_bytes,
                         sizeof(mtw_move_bytes));
    if (zoom_restore_attempted)
        mtw_restore_code(MTW_ZOOM_RESTORE_ADDRESS, mtw_zoom_restore_bytes,
                         sizeof(mtw_zoom_restore_bytes));
    if (zoom_capture_attempted)
        mtw_restore_code(MTW_ZOOM_CAPTURE_ADDRESS, mtw_zoom_capture_bytes,
                         sizeof(mtw_zoom_capture_bytes));
    if (draw_created) {
        MH_DisableHook((void *)MTW_DRAW_TEXT_ADDRESS);
        MH_RemoveHook((void *)MTW_DRAW_TEXT_ADDRESS);
    }
    if (click_created) {
        MH_DisableHook((void *)MTW_CLICK_OPTION_ADDRESS);
        MH_RemoveHook((void *)MTW_CLICK_OPTION_ADDRESS);
    }
    if (add_created) {
        MH_DisableHook((void *)MTW_ADD_OPTION_ADDRESS);
        MH_RemoveHook((void *)MTW_ADD_OPTION_ADDRESS);
    }
    return 0;
}
