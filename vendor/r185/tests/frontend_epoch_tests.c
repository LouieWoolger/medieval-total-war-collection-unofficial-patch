#include <stdint.h>
#include <stdio.h>

#include "../source/frontend_epoch_core.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression);    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

int main(void) {
    /* Frontend activation is a content boundary before the game handler. */
    CHECK(frontend_epoch_should_begin(0x0201u, 0u, 1u));
    CHECK(frontend_epoch_should_begin(0x0203u, 0u, 1u));
    CHECK(frontend_epoch_should_begin(0x0100u, 0x1Bu, 1u));
    CHECK(frontend_epoch_should_begin(0x0100u, 0x0Du, 1u));

    /* Focus messages must not terminate the restoration epoch they trigger. */
    CHECK(!frontend_epoch_should_begin(0x0006u, 1u, 1u));
    CHECK(!frontend_epoch_should_begin(0x0007u, 0u, 1u));
    CHECK(!frontend_epoch_should_begin(0x0008u, 0u, 1u));
    CHECK(!frontend_epoch_should_begin(0x0112u, 0xF100u, 1u));

    /* Ordinary motion, redraw, key-up, and unrelated keys are not boundaries. */
    CHECK(!frontend_epoch_should_begin(0x0200u, 0u, 1u));
    CHECK(!frontend_epoch_should_begin(0x0101u, 0x1Bu, 1u));
    CHECK(!frontend_epoch_should_begin(0x0100u, 0x20u, 1u));
    CHECK(!frontend_epoch_should_begin(0x000Fu, 0u, 1u));

    /* No tracked 800x600 frontend lifetime means battle input is untouched. */
    CHECK(!frontend_epoch_should_begin(0x0201u, 0u, 0u));
    CHECK(!frontend_epoch_should_begin(0x0100u, 0x1Bu, 0u));

    if (failures != 0) {
        fprintf(stderr, "%d frontend epoch test(s) failed\n", failures);
        return 1;
    }
    puts("frontend input epoch tests passed");
    return 0;
}
