#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../source/loading_shadow_core.h"

static int failures;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);  \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

int main(void) {
    uint8_t storage[16] = {0};
    uint8_t clean[16];
    uint8_t live[16];
    loading_shadow_state state;
    size_t i;

    memset(&state, 0, sizeof(state));
    state.bytes = storage;
    state.capacity = sizeof(storage);
    for (i = 0; i < sizeof(clean); ++i) clean[i] = (uint8_t)(i + 1u);
    memset(live, 0xA5, sizeof(live));

    CHECK(!loading_shadow_restore_after_lock(&state, live, sizeof(live)));
    CHECK(loading_shadow_capture(&state, clean, sizeof(clean)));
    CHECK(state.valid == 1);
    CHECK(memcmp(storage, clean, sizeof(clean)) == 0);

    CHECK(loading_shadow_restore_after_lock(&state, live, sizeof(live)));
    CHECK(memcmp(live, clean, sizeof(clean)) == 0);

    live[5] = 0xF0;
    live[6] = 0x0D;
    CHECK(loading_shadow_commit_before_unlock(&state, live, sizeof(live)));
    memset(live, 0, sizeof(live));
    CHECK(loading_shadow_restore_after_lock(&state, live, sizeof(live)));
    CHECK(live[5] == 0xF0 && live[6] == 0x0D);

    CHECK(!loading_shadow_capture(&state, clean, sizeof(clean) + 1u));
    CHECK(!loading_shadow_restore_after_lock(&state, live, sizeof(live) - 1u));
    CHECK(!loading_shadow_commit_before_unlock(&state, live, sizeof(live) - 1u));

    if (failures != 0) return 1;
    puts("loading shadow tests: PASS");
    return 0;
}
