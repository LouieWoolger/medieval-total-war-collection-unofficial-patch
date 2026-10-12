#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dust_cadence.h"
#ifdef MTW_DUST_CHILD_DELTA
#include "particle_normalization.h"
#endif

extern IMAGE_DOS_HEADER __ImageBase;

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

#define MTW_EXPECTED_SHA256_HEX \
    "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"
#define MTW_EXPECTED_IMAGE_BASE 0x00400000u
#define MTW_EXPECTED_TIMESTAMP 0x567DD42Fu
#define MTW_CALLSITE_RVA 0x001B96AFu
#define MTW_PARTICLE_UPDATE_RVA 0x001B8000u
#define MTW_LOOP_ELAPSED_RVA 0x0055EAECu
#define MTW_PRODUCER_CALLSITE_RVA 0x001B9A24u
#define MTW_PRODUCER_UPDATE_RVA 0x001B9640u
#define MTW_BATTLE_TICK_RVA 0x00742768u
#define MTW_MOVEMENT_ELIGIBILITY_CALLSITE_RVA 0x000680EEu
#define MTW_ELIGIBILITY_RVA 0x001B9830u

#define PARTICLE_SIZE_X_OFFSET 0x44u
#define PARTICLE_SIZE_Y_OFFSET 0x48u
#define PARTICLE_TYPE_OFFSET 0x4Cu
#define PARTICLE_POSITION_X_OFFSET 0x74u
#define PARTICLE_POSITION_Y_OFFSET 0x78u
#define PARTICLE_POSITION_Z_OFFSET 0x7Cu

/* Calibrated against matched native size-by-age traces; override stays bounded. */
#define MTW_DEFAULT_NATIVE_STEP_MS (1000.0f / 60.0f)
#define MTW_MIN_NATIVE_STEP_MS 10.0f
#define MTW_MAX_NATIVE_STEP_MS 100.0f
#define MTW_MAX_NORMALIZED_ELAPSED_MS 250u
#ifndef MTW_RELEASE_DENSE_HZ
#define MTW_RELEASE_DENSE_HZ 15u
#endif
#ifndef MTW_RELEASE_VIRTUAL_MANAGER_FPS
#define MTW_RELEASE_VIRTUAL_MANAGER_FPS 19u
#endif
#define MTW_RELEASE_CADENCE_STEP_MS (1000.0f / (float)MTW_RELEASE_DENSE_HZ)
#define MTW_RELEASE_SPARSE_STEP_MS 28.0f
#define MTW_DENSE_ENTER_COST 13000u
#define MTW_DENSE_EXIT_COST 11000u
#define MTW_STATUS_MAGIC 0x4457544Du /* MTWD */
#ifdef MTW_DUST_CHILD_DELTA
#define MTW_STATUS_VERSION 2u
#else
#define MTW_STATUS_VERSION 1u
#endif

#define MTW_CANARY_DISABLED 0
#define MTW_CANARY_ZERO_SIZE 1
#define MTW_CANARY_CADENCE_30 2

#if defined(__GNUC__) || defined(__clang__)
#define MTW_THISCALL __attribute__((thiscall))
#else
#define MTW_THISCALL __thiscall
#endif

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Fn)(UINT);
#ifndef MTW_DUST_CHILD_DELTA
typedef int(MTW_THISCALL *ProducerUpdateFn)(void *producer);
#endif
#ifdef MTW_DUST_CHILD_DELTA
typedef int(MTW_THISCALL *ParticleUpdateFn)(void *particle);
#endif

typedef struct MtwDustRuntimeStatus {
    DWORD magic;
    DWORD version;
    DWORD process_id;
    DWORD executable_accepted;
    DWORD producer_hook_installed;
    DWORD eligibility_hook_installed;
    DWORD install_complete;
    DWORD cadence_step_microseconds;
    DWORD virtual_manager_fps;
    volatile LONG producer_hook_hits;
    volatile LONG producer_type0_hits;
    volatile LONG producer_type1_hits;
    volatile LONG producer_updates_executed;
    volatile LONG producer_updates_skipped;
    volatile LONG cadence_frames_allowed;
    volatile LONG cadence_frames_skipped;
    volatile LONG eligibility_hook_hits;
    volatile LONG eligibility_overrides;
    volatile LONG elapsed_min_ms;
    volatile LONG elapsed_max_ms;
    volatile LONG last_battle_tick;
    volatile LONG dense_mode;
    volatile LONG dense_entries;
    volatile LONG dense_exits;
} MtwDustRuntimeStatus;

typedef struct MtwSupportedExe {
    DWORD image_size;
    char sha256[65];
} MtwSupportedExe;

/* Exact installer output identities: stock, scroll, sprite, both. */
static const MtwSupportedExe supported_exes[] = {
    {0x00B4C000u, "23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5"},
    {0x00B4E000u, "50829CD084355D81EC94D6F4489D1F60E2EF7FA92983D0EAD07D43832DEEF15B"},
    {0x00B4D000u, "72A42C3635ED808E87CF85BAF24D39601376E4B0143B109B0A63771541B662D0"},
    {0x00B4F000u, "982921CEFDED31C298F249742B8A001BB1A823F77FB3965E01892A34ED12E627"},
};

#ifndef MTW_DUST_CHILD_DELTA
static const BYTE expected_producer_callsite_bytes[5] = {
    0xE8u, 0x17u, 0xFCu, 0xFFu, 0xFFu,
};
static const BYTE expected_producer_fallthrough_bytes[4] = {
    0x85u, 0xC0u, 0x7Du, 0x64u,
};
static const BYTE expected_producer_update_bytes[7] = {
    0x83u, 0xECu, 0x0Cu, 0x53u, 0x55u, 0x56u, 0x57u,
};
#endif
static INIT_ONCE hook_install_once = INIT_ONCE_STATIC_INIT;
static HMODULE real_d3d9_module;
#ifndef MTW_DUST_CHILD_DELTA
static ProducerUpdateFn original_producer_update;
#endif
#ifdef MTW_DUST_CHILD_DELTA
static ParticleUpdateFn original_particle_update;
#endif
static volatile DWORD *loop_elapsed_ms;
#ifndef MTW_DUST_CHILD_DELTA
static volatile DWORD *battle_tick;
static MtwDustCadence dust_cadence;
#endif
static HANDLE status_mapping;
static MtwDustRuntimeStatus *runtime_status;
static volatile LONG producer_hook_installed;
static volatile LONG graphics_ready;
static volatile LONG reference_step_microseconds = 20000;
static int backend_verified;
static const BYTE expected_backend_sha256[32] = {
    0x6Au, 0x0Cu, 0xA2u, 0x14u, 0x78u, 0x4Bu, 0xE0u, 0x4Bu,
    0x7Cu, 0x8Bu, 0x54u, 0x71u, 0x05u, 0xAAu, 0x9Du, 0x79u,
    0xACu, 0xF4u, 0xDCu, 0x26u, 0xC0u, 0xB6u, 0xF8u, 0x70u,
    0x2Bu, 0x43u, 0x7Du, 0xDCu, 0xA5u, 0x40u, 0x58u, 0xB2u,
};

#ifdef MTW_DUST_CHILD_DELTA
static const BYTE expected_particle_callsite_bytes[5] = {
    0xE8u, 0x4Cu, 0xE9u, 0xFFu, 0xFFu,
};
static const BYTE expected_particle_fallthrough_bytes[4] = {
    0x85u, 0xC0u, 0x7Du, 0x64u,
};
static const BYTE expected_particle_update_bytes[7] = {
    0x83u, 0xECu, 0x08u, 0x56u, 0x57u, 0x8Bu, 0xF1u,
};
#endif

void mtw_dust_simulation_set_graphics_ready(int ready) {
    InterlockedExchange(&graphics_ready, ready ? 1 : 0);
    if (runtime_status) runtime_status->install_complete =
        producer_hook_installed && graphics_ready;
}

int mtw_dust_simulation_hook_installed(void) {
    return producer_hook_installed != 0 && backend_verified != 0;
}

void mtw_dust_simulation_set_reference_microseconds(DWORD step) {
    if (step >= 10000u && step <= 100000u)
        InterlockedExchange(&reference_step_microseconds, (LONG)step);
}

static void status_update_min(volatile LONG *target, LONG value) {
    LONG observed;
    do {
        observed = *target;
        if (value >= observed) return;
    } while (InterlockedCompareExchange(target, value, observed) != observed);
}

static void status_update_max(volatile LONG *target, LONG value) {
    LONG observed;
    do {
        observed = *target;
        if (value <= observed) return;
    } while (InterlockedCompareExchange(target, value, observed) != observed);
}

static void initialize_runtime_status(void) {
    WCHAR name[64];

    wsprintfW(name, L"Local\\MTW_DUST_STATUS_%lu", GetCurrentProcessId());
    status_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
                                        PAGE_READWRITE, 0u,
                                        sizeof(MtwDustRuntimeStatus), name);
    if (!status_mapping) return;
    runtime_status = (MtwDustRuntimeStatus *)MapViewOfFile(
        status_mapping, FILE_MAP_ALL_ACCESS, 0u, 0u,
        sizeof(MtwDustRuntimeStatus));
    if (!runtime_status) return;
    ZeroMemory(runtime_status, sizeof(*runtime_status));
    runtime_status->magic = MTW_STATUS_MAGIC;
    runtime_status->version = MTW_STATUS_VERSION;
    runtime_status->process_id = GetCurrentProcessId();
    runtime_status->elapsed_min_ms = LONG_MAX;
}

static DWORD read_particle_dword(const BYTE *particle, SIZE_T offset) {
    DWORD value;
    CopyMemory(&value, particle + offset, sizeof(value));
    return value;
}

#ifdef MTW_DUST_CHILD_DELTA
static float read_particle_float(const BYTE *particle, SIZE_T offset) {
    float value;
    CopyMemory(&value, particle + offset, sizeof(value));
    return value;
}

static void write_particle_float(BYTE *particle, SIZE_T offset, float value) {
    CopyMemory(particle + offset, &value, sizeof(value));
}
#endif

static int hash_file_sha256(const WCHAR *path, BYTE output[32]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE *hash_object = NULL;
    BYTE *buffer = NULL;
    DWORD object_size = 0u;
    DWORD hash_size = 0u;
    DWORD result_size = 0u;
    DWORD bytes_read = 0u;
    NTSTATUS status;
    int success = 0;

    if (!path || !output) {
        return 0;
    }
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0u);
    if (!NT_SUCCESS(status)) {
        goto cleanup;
    }
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               (PUCHAR)&object_size, sizeof(object_size),
                               &result_size, 0u);
    if (!NT_SUCCESS(status) || result_size != sizeof(object_size) || object_size == 0u) {
        goto cleanup;
    }
    status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                               (PUCHAR)&hash_size, sizeof(hash_size),
                               &result_size, 0u);
    if (!NT_SUCCESS(status) || result_size != sizeof(hash_size) || hash_size != 32u) {
        goto cleanup;
    }
    hash_object = (BYTE *)HeapAlloc(GetProcessHeap(), 0u, object_size);
    buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0u, 64u * 1024u);
    if (!hash_object || !buffer) {
        goto cleanup;
    }
    status = BCryptCreateHash(algorithm, &hash, hash_object, object_size,
                              NULL, 0u, 0u);
    if (!NT_SUCCESS(status)) {
        goto cleanup;
    }
    for (;;) {
        if (!ReadFile(file, buffer, 64u * 1024u, &bytes_read, NULL)) {
            goto cleanup;
        }
        if (bytes_read == 0u) {
            break;
        }
        status = BCryptHashData(hash, buffer, bytes_read, 0u);
        if (!NT_SUCCESS(status)) {
            goto cleanup;
        }
    }
    status = BCryptFinishHash(hash, output, 32u, 0u);
    success = NT_SUCCESS(status);

cleanup:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0u);
    }
    if (buffer) {
        HeapFree(GetProcessHeap(), 0u, buffer);
    }
    if (hash_object) {
        HeapFree(GetProcessHeap(), 0u, hash_object);
    }
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    return success;
}

static int main_executable_matches(HMODULE module) {
    const IMAGE_DOS_HEADER *dos_header;
    const IMAGE_NT_HEADERS32 *nt_headers;
    WCHAR module_path[MAX_PATH];
    BYTE actual_hash[32];
    DWORD path_length;
    SIZE_T known_index;

    if (!module || (ULONG_PTR)module != (ULONG_PTR)MTW_EXPECTED_IMAGE_BASE) {
        return 0;
    }
    dos_header = (const IMAGE_DOS_HEADER *)module;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE ||
        dos_header->e_lfanew <= 0 || dos_header->e_lfanew >= 0x1000) {
        return 0;
    }
    nt_headers = (const IMAGE_NT_HEADERS32 *)((const BYTE *)module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE ||
        nt_headers->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt_headers->FileHeader.TimeDateStamp != MTW_EXPECTED_TIMESTAMP ||
        nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt_headers->OptionalHeader.ImageBase != MTW_EXPECTED_IMAGE_BASE) {
        return 0;
    }
    path_length = GetModuleFileNameW(module, module_path, MAX_PATH);
    if (path_length == 0u || path_length >= MAX_PATH) {
        return 0;
    }
    if (!hash_file_sha256(module_path, actual_hash)) {
        return 0;
    }
    for (known_index = 0u;
         known_index < sizeof(supported_exes) / sizeof(supported_exes[0]);
         ++known_index) {
        const MtwSupportedExe *known = &supported_exes[known_index];
        SIZE_T byte_index;
        int equal = 1;
        if (nt_headers->OptionalHeader.SizeOfImage != known->image_size)
            continue;
        for (byte_index = 0u; byte_index < sizeof(actual_hash); ++byte_index) {
            static const char hex[] = "0123456789ABCDEF";
            if (known->sha256[byte_index * 2u] !=
                    hex[actual_hash[byte_index] >> 4u] ||
                known->sha256[byte_index * 2u + 1u] !=
                    hex[actual_hash[byte_index] & 0x0Fu]) {
                equal = 0;
                break;
            }
        }
        if (equal) return 1;
    }
    return 0;
}

#ifndef MTW_DUST_CHILD_DELTA
static int MTW_THISCALL producer_update_cadence_hook(void *producer_pointer) {
    BYTE *producer = (BYTE *)producer_pointer;
    DWORD producer_type;
    DWORD frame_id;
    DWORD elapsed_ms;
    int is_new_frame;
    int allow;

    if (!original_producer_update || !producer || !battle_tick ||
        !loop_elapsed_ms || !graphics_ready) {
        return original_producer_update ? original_producer_update(producer) : 0;
    }
    producer_type = read_particle_dword(producer, 0x40u);
    if (producer_type != 0u && producer_type != 1u) {
        return original_producer_update(producer);
    }
    if (runtime_status) {
        InterlockedIncrement(&runtime_status->producer_hook_hits);
        InterlockedIncrement(producer_type == 0u
            ? &runtime_status->producer_type0_hits
            : &runtime_status->producer_type1_hits);
    }
    frame_id = *battle_tick;
    elapsed_ms = *loop_elapsed_ms;
    dust_cadence.step_ms = (float)reference_step_microseconds / 1000.0f;
    is_new_frame = !dust_cadence.has_frame || dust_cadence.last_frame_id != frame_id;
    allow = mtw_dust_cadence_begin_frame(&dust_cadence, frame_id, elapsed_ms);
    if (runtime_status && is_new_frame) {
        runtime_status->cadence_step_microseconds =
            (DWORD)(dust_cadence.step_ms * 1000.0f + 0.5f);
        status_update_min(&runtime_status->elapsed_min_ms, (LONG)elapsed_ms);
        status_update_max(&runtime_status->elapsed_max_ms, (LONG)elapsed_ms);
        InterlockedExchange(&runtime_status->last_battle_tick, (LONG)frame_id);
        InterlockedIncrement(allow
            ? &runtime_status->cadence_frames_allowed
            : &runtime_status->cadence_frames_skipped);
    }
    if (!allow) {
        if (runtime_status) {
            InterlockedIncrement(&runtime_status->producer_updates_skipped);
        }
        return 0;
    }
    if (runtime_status) {
        InterlockedIncrement(&runtime_status->producer_updates_executed);
    }
    return original_producer_update(producer);
}
#endif

#ifdef MTW_DUST_CHILD_DELTA
static int MTW_THISCALL particle_update_delta_hook(void *particle_pointer) {
    static const SIZE_T offsets[5] = {
        PARTICLE_POSITION_X_OFFSET, PARTICLE_POSITION_Y_OFFSET,
        PARTICLE_POSITION_Z_OFFSET, PARTICLE_SIZE_X_OFFSET,
        PARTICLE_SIZE_Y_OFFSET,
    };
    BYTE *particle = (BYTE *)particle_pointer;
    DWORD type = 0u;
    DWORD elapsed = 0u;
    float before[5];
    int index;
    int result;

    if (particle && graphics_ready && loop_elapsed_ms) {
        type = read_particle_dword(particle, PARTICLE_TYPE_OFFSET);
        if (type <= 1u) {
            for (index = 0; index < 5; ++index)
                before[index] = read_particle_float(particle, offsets[index]);
            elapsed = *loop_elapsed_ms;
        }
    }
    result = original_particle_update(particle);
    if (!particle || !graphics_ready || type > 1u || result < 0 ||
        elapsed == 0u || elapsed > MTW_MAX_NORMALIZED_ELAPSED_MS ||
        read_particle_dword(particle, PARTICLE_TYPE_OFFSET) != type) {
        return result;
    }
    for (index = 0; index < 5; ++index) {
        const float after = read_particle_float(particle, offsets[index]);
        write_particle_float(particle, offsets[index],
            mtw_normalize_frame_delta(before[index], after, elapsed,
                (float)reference_step_microseconds / 1000.0f));
    }
    if (runtime_status) {
        InterlockedIncrement(&runtime_status->producer_hook_hits);
        InterlockedIncrement(type == 0u
            ? &runtime_status->producer_type0_hits
            : &runtime_status->producer_type1_hits);
        InterlockedIncrement(&runtime_status->producer_updates_executed);
        runtime_status->cadence_step_microseconds =
            (DWORD)reference_step_microseconds;
        status_update_min(&runtime_status->elapsed_min_ms, (LONG)elapsed);
        status_update_max(&runtime_status->elapsed_max_ms, (LONG)elapsed);
    }
    return result;
}
#endif

static int write_guarded_patch(BYTE *address,
                               const BYTE *expected,
                               const BYTE *replacement,
                               SIZE_T size) {
    DWORD old_protection;
    DWORD ignored_protection;
    int flush_succeeded;

    if (!address || !expected || !replacement || size == 0u) {
        return 0;
    }
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        return 0;
    }
    if (memcmp(address, expected, size) != 0) {
        VirtualProtect(address, size, old_protection, &ignored_protection);
        return 0;
    }

    CopyMemory(address, replacement, size);
    flush_succeeded = FlushInstructionCache(GetCurrentProcess(), address, size);
    if (!flush_succeeded ||
        !VirtualProtect(address, size, old_protection, &ignored_protection)) {
        CopyMemory(address, expected, size);
        FlushInstructionCache(GetCurrentProcess(), address, size);
        VirtualProtect(address, size, old_protection, &ignored_protection);
        return 0;
    }
    return 1;
}

#ifndef MTW_DUST_CHILD_DELTA
static int install_producer_cadence_hook(void) {
    HMODULE main_module = GetModuleHandleW(NULL);
    BYTE *image_base = (BYTE *)main_module;
    BYTE *callsite;
    BYTE *original_entry;
    BYTE patch[5];
    DWORD displacement;

    if (!main_executable_matches(main_module)) {
        return 0;
    }
    if (runtime_status) runtime_status->executable_accepted = 1u;
    callsite = image_base + MTW_PRODUCER_CALLSITE_RVA;
    original_entry = image_base + MTW_PRODUCER_UPDATE_RVA;
    if (memcmp(callsite, expected_producer_callsite_bytes,
               sizeof(expected_producer_callsite_bytes)) != 0 ||
        memcmp(callsite + sizeof(expected_producer_callsite_bytes),
               expected_producer_fallthrough_bytes,
               sizeof(expected_producer_fallthrough_bytes)) != 0 ||
        memcmp(original_entry, expected_producer_update_bytes,
               sizeof(expected_producer_update_bytes)) != 0) {
        return 0;
    }

    original_producer_update = (ProducerUpdateFn)(void *)original_entry;
    loop_elapsed_ms = (volatile DWORD *)(void *)(image_base + MTW_LOOP_ELAPSED_RVA);
    battle_tick = (volatile DWORD *)(void *)(image_base + MTW_BATTLE_TICK_RVA);
    dust_cadence = mtw_dust_cadence_initial(
        (float)reference_step_microseconds / 1000.0f);

    patch[0] = 0xE8u;
    displacement = (DWORD)((ULONG_PTR)(void *)producer_update_cadence_hook -
                           ((ULONG_PTR)callsite + sizeof(patch)));
    CopyMemory(patch + 1u, &displacement, sizeof(displacement));
    if (!write_guarded_patch(callsite, expected_producer_callsite_bytes,
                             patch, sizeof(patch))) {
        original_producer_update = NULL;
        loop_elapsed_ms = NULL;
        battle_tick = NULL;
        return 0;
    }
    InterlockedExchange(&producer_hook_installed, 1);
    if (runtime_status) {
        runtime_status->producer_hook_installed = 1u;
        runtime_status->cadence_step_microseconds =
            (DWORD)(dust_cadence.step_ms * 1000.0f + 0.5f);
    }
    return 1;
}
#endif

#ifdef MTW_DUST_CHILD_DELTA
static int install_particle_delta_hook(void) {
    HMODULE main_module = GetModuleHandleW(NULL);
    BYTE *image_base = (BYTE *)main_module;
    BYTE *callsite;
    BYTE *original_entry;
    BYTE patch[5];
    DWORD displacement;

    if (!main_executable_matches(main_module)) return 0;
    if (runtime_status) runtime_status->executable_accepted = 1u;
    callsite = image_base + MTW_CALLSITE_RVA;
    original_entry = image_base + MTW_PARTICLE_UPDATE_RVA;
    if (memcmp(callsite, expected_particle_callsite_bytes,
               sizeof(expected_particle_callsite_bytes)) != 0 ||
        memcmp(callsite + sizeof(expected_particle_callsite_bytes),
               expected_particle_fallthrough_bytes,
               sizeof(expected_particle_fallthrough_bytes)) != 0 ||
        memcmp(original_entry, expected_particle_update_bytes,
               sizeof(expected_particle_update_bytes)) != 0) return 0;
    original_particle_update = (ParticleUpdateFn)(void *)original_entry;
    loop_elapsed_ms = (volatile DWORD *)(void *)(image_base + MTW_LOOP_ELAPSED_RVA);
    patch[0] = 0xE8u;
    displacement = (DWORD)((ULONG_PTR)(void *)particle_update_delta_hook -
                           ((ULONG_PTR)callsite + sizeof(patch)));
    CopyMemory(patch + 1u, &displacement, sizeof(displacement));
    if (!write_guarded_patch(callsite, expected_particle_callsite_bytes,
                             patch, sizeof(patch))) {
        original_particle_update = NULL;
        loop_elapsed_ms = NULL;
        return 0;
    }
    InterlockedExchange(&producer_hook_installed, 1);
    if (runtime_status) runtime_status->producer_hook_installed = 1u;
    return 1;
}
#endif

static BOOL CALLBACK install_hook_once_callback(PINIT_ONCE once,
                                                PVOID parameter,
                                                PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    initialize_runtime_status();
#ifdef MTW_DUST_CHILD_DELTA
    install_particle_delta_hook();
#else
    install_producer_cadence_hook();
#endif
    if (runtime_status) runtime_status->install_complete =
        producer_hook_installed && graphics_ready;
    return TRUE;
}

static Direct3DCreate9Fn resolve_real_direct3dcreate9(void) {
    WCHAR module_path[MAX_PATH];
    const WCHAR sibling_name[] = L"dgVoodoo_D3D9.dll";
    WCHAR *filename;
    DWORD length;
    SIZE_T remaining;

    if (!real_d3d9_module) {
        length = GetModuleFileNameW((HMODULE)&__ImageBase, module_path, MAX_PATH);
        if (length == 0u || length >= MAX_PATH) {
            return NULL;
        }
        filename = module_path + length;
        while (filename > module_path && filename[-1] != L'\\' && filename[-1] != L'/') {
            --filename;
        }
        remaining = MAX_PATH - (SIZE_T)(filename - module_path);
        if ((SIZE_T)lstrlenW(sibling_name) + 1u > remaining) {
            return NULL;
        }
        lstrcpyW(filename, sibling_name);
        real_d3d9_module = LoadLibraryW(module_path);
        if (real_d3d9_module) {
            BYTE actual_hash[32];
            backend_verified = hash_file_sha256(module_path, actual_hash) &&
                memcmp(actual_hash, expected_backend_sha256,
                       sizeof(actual_hash)) == 0;
        }
    }
    if (!real_d3d9_module) {
        return NULL;
    }
    return (Direct3DCreate9Fn)(void *)GetProcAddress(real_d3d9_module,
                                                     "Direct3DCreate9");
}

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version) {
    Direct3DCreate9Fn real_direct3dcreate9;

    real_direct3dcreate9 = resolve_real_direct3dcreate9();
    if (!real_direct3dcreate9) {
        return NULL;
    }
    if (backend_verified)
        InitOnceExecuteOnce(&hook_install_once, install_hook_once_callback, NULL, NULL);
    return real_direct3dcreate9(sdk_version);
}
