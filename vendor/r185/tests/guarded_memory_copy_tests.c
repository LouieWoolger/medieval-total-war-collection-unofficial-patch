#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/guarded_memory_copy.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int region_has_protection(const void *address, DWORD mask) {
    MEMORY_BASIC_INFORMATION info;
    memset(&info, 0, sizeof(info));
    return VirtualQuery(address, &info, sizeof(info)) == sizeof(info) &&
           (info.Protect & mask) == mask;
}

static void fill_pattern(unsigned char *bytes, size_t length) {
    size_t index;
    for (index = 0u; index < length; ++index) {
        bytes[index] = (unsigned char)((index * 37u + 11u) & 0xFFu);
    }
}

int main(void) {
    SYSTEM_INFO system_info;
    guarded_memory_copy_stats stats;
    unsigned char *source;
    unsigned char *destination;
    unsigned char *expected;
    DWORD old_protect;
    DWORD ignored;
    size_t page_size;
    size_t allocation_size;

    GetSystemInfo(&system_info);
    page_size = (size_t)system_info.dwPageSize;
    allocation_size = page_size * 3u;
    source = (unsigned char *)VirtualAlloc(
        NULL, allocation_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    destination = (unsigned char *)VirtualAlloc(
        NULL, allocation_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    expected = (unsigned char *)malloc(allocation_size);
    CHECK(source != NULL && destination != NULL && expected != NULL);

    fill_pattern(source, allocation_size);
    memcpy(expected, source, allocation_size);
    memset(destination, 0, allocation_size);
    CHECK(VirtualProtect(source, allocation_size,
                         PAGE_READWRITE | PAGE_GUARD, &old_protect));
    memset(&stats, 0, sizeof(stats));
    CHECK(guarded_memory_copy(destination, source, allocation_size, &stats));
    CHECK(memcmp(destination, expected, allocation_size) == 0);
    CHECK(stats.copied_bytes == allocation_size);
    CHECK(stats.guard_regions >= 1u);
    CHECK(stats.restored_regions == stats.guard_regions);
    CHECK(region_has_protection(source, PAGE_READWRITE | PAGE_GUARD));

    CHECK(VirtualProtect(source, allocation_size, PAGE_READWRITE, &ignored));
    fill_pattern(source, allocation_size);
    memcpy(expected, source, allocation_size);
    memset(destination, 0xCC, allocation_size);
    CHECK(VirtualProtect(source, allocation_size,
                         PAGE_READWRITE | PAGE_GUARD, &ignored));
    memset(&stats, 0, sizeof(stats));
    CHECK(guarded_memory_copy(destination + page_size - 16u,
                              source + page_size - 16u, 64u, &stats));
    CHECK(memcmp(destination + page_size - 16u,
                 expected + page_size - 16u, 64u) == 0);
    CHECK(stats.copied_bytes == 64u);
    CHECK(region_has_protection(source + page_size - 16u,
                                PAGE_READWRITE | PAGE_GUARD));

    CHECK(VirtualProtect(source, allocation_size, PAGE_READWRITE, &ignored));
    CHECK(VirtualProtect(source, page_size,
                         PAGE_READWRITE | PAGE_GUARD, &ignored));
    CHECK(VirtualProtect(source + page_size, page_size,
                         PAGE_NOACCESS, &ignored));
    memset(&stats, 0, sizeof(stats));
    CHECK(!guarded_memory_copy(destination, source, page_size * 2u, &stats));
    CHECK(region_has_protection(source, PAGE_READWRITE | PAGE_GUARD));
    CHECK(region_has_protection(source + page_size, PAGE_NOACCESS));

    CHECK(VirtualProtect(source, allocation_size, PAGE_READWRITE, &ignored));
    VirtualFree(source, 0u, MEM_RELEASE);
    VirtualFree(destination, 0u, MEM_RELEASE);
    free(expected);
    puts("guarded memory copy tests passed");
    return 0;
}
