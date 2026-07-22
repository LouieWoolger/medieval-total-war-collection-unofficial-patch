#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "guarded_memory_copy.h"

static int readable_committed_region(const MEMORY_BASIC_INFORMATION *info) {
    DWORD access;
    if (info == NULL || info->State != MEM_COMMIT) return 0;
    access = info->Protect & 0xFFu;
    return access != 0u && access != PAGE_NOACCESS;
}

int guarded_memory_copy(void *destination,
                        const void *source,
                        size_t length,
                        guarded_memory_copy_stats *stats) {
    guarded_memory_copy_stats local;
    unsigned char *destination_cursor = (unsigned char *)destination;
    const unsigned char *source_cursor = (const unsigned char *)source;
    size_t remaining = length;

    memset(&local, 0, sizeof(local));
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (destination == NULL || source == NULL || length == 0u) return 0;

    while (remaining != 0u) {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t region_end;
        size_t chunk;
        SIZE_T transferred = 0u;
        DWORD old_protect = 0u;
        DWORD ignored = 0u;
        int guard_removed = 0;
        int copied;
        int restored = 1;

        memset(&info, 0, sizeof(info));
        if (VirtualQuery(source_cursor, &info, sizeof(info)) != sizeof(info) ||
            !readable_committed_region(&info)) {
            if (stats != NULL) *stats = local;
            return 0;
        }
        ++local.queried_regions;
        region_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (region_end <= (uintptr_t)source_cursor) {
            if (stats != NULL) *stats = local;
            return 0;
        }
        chunk = (size_t)(region_end - (uintptr_t)source_cursor);
        if (chunk > remaining) chunk = remaining;

        if ((info.Protect & PAGE_GUARD) != 0u) {
            DWORD unguarded = info.Protect & ~PAGE_GUARD;
            if (!VirtualProtect((void *)source_cursor, chunk, unguarded,
                                &old_protect)) {
                if (stats != NULL) *stats = local;
                return 0;
            }
            guard_removed = 1;
            ++local.guard_regions;
        }

        copied = ReadProcessMemory(GetCurrentProcess(), source_cursor,
                                   destination_cursor, chunk,
                                   &transferred) && transferred == chunk;
        if (guard_removed) {
            restored = VirtualProtect((void *)source_cursor, chunk,
                                      old_protect, &ignored) != FALSE;
            if (restored) ++local.restored_regions;
        }
        if (!copied || !restored) {
            if (stats != NULL) *stats = local;
            return 0;
        }

        local.copied_bytes += chunk;
        source_cursor += chunk;
        destination_cursor += chunk;
        remaining -= chunk;
    }

    if (stats != NULL) *stats = local;
    return 1;
}
