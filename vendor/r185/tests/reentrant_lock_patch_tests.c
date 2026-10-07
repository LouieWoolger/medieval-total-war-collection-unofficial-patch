#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/reentrant_lock_patch_core.h"

static int failures;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static mtw_reentrant_callsite_bundle exact_bundle(void) {
    static const uint8_t outer_acquire[] = {0xE8, 0xA8, 0xFC, 0xF6, 0xFF};
    static const uint8_t outer_release[] = {0xE8, 0x69, 0xF8, 0xF6, 0xFF};
    static const uint8_t inner_acquire[] = {0xE8, 0x07, 0xED, 0xFF, 0xFF};
    static const uint8_t inner_release_ok[] = {0xE8, 0x35, 0xED, 0xFF, 0xFF};
    static const uint8_t inner_release_fail[] = {0xE8, 0x25, 0xED, 0xFF, 0xFF};
    mtw_reentrant_callsite_bundle bundle = {
        outer_acquire, sizeof(outer_acquire),
        outer_release, sizeof(outer_release),
        inner_acquire, sizeof(inner_acquire),
        inner_release_ok, sizeof(inner_release_ok),
        inner_release_fail, sizeof(inner_release_fail)
    };
    return bundle;
}

int main(void) {
    mtw_reentrant_callsite_bundle bundle = exact_bundle();
    uint8_t drift[MTW_REENTRANT_CALL_SIZE];

    expect_true(mtw_validate_reentrant_callsites(&bundle) == 1,
                "all five exact owner-preserving callsites must be accepted");

    memcpy(drift, bundle.outer_acquire, sizeof(drift));
    drift[0] = 0x90;
    bundle.outer_acquire = drift;
    expect_true(mtw_validate_reentrant_callsites(&bundle) == 0,
                "outer acquire opcode drift must fail closed");

    bundle = exact_bundle();
    memcpy(drift, bundle.inner_release_fail, sizeof(drift));
    drift[4] ^= 1u;
    bundle.inner_release_fail = drift;
    expect_true(mtw_validate_reentrant_callsites(&bundle) == 0,
                "nested exceptional release target drift must fail closed");

    bundle = exact_bundle();
    bundle.inner_acquire_size = MTW_REENTRANT_CALL_SIZE - 1u;
    expect_true(mtw_validate_reentrant_callsites(&bundle) == 0,
                "short callsite must fail closed");
    expect_true(mtw_validate_reentrant_callsites(NULL) == 0,
                "null bundle must fail closed");

    expect_true(mtw_should_divert_outer_owned_guard_fault(42u, 42u, 1u) == 1,
                "exact same-thread outer fast owner must divert guard tracking");
    expect_true(mtw_should_divert_outer_owned_guard_fault(0u, 0u, 1u) == 0,
                "zero owner must fail closed");
    expect_true(mtw_should_divert_outer_owned_guard_fault(42u, 41u, 1u) == 0,
                "different thread must keep stock guard tracking");
    expect_true(mtw_should_divert_outer_owned_guard_fault(42u, 42u, 0u) == 0,
                "inactive fast-owner flag must keep stock guard tracking");
    expect_true(mtw_should_divert_outer_owned_guard_fault(42u, 42u, 2u) == 0,
                "unexpected fast-owner state must fail closed");

    if (failures != 0) return 1;
    puts("outer-owned guard diversion tests passed");
    return 0;
}
