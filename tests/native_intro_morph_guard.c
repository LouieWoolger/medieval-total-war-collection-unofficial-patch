#include <stdio.h>

#include "../vendor/r185/source/intro_morph_guard_core.h"

static int checks;
static int failures;

static void check(int condition, const char *description) {
    ++checks;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", description);
    }
}

int main(void) {
    mtw_intro_morph_state state = {1, 0, 1, 1, 0, 0, 1, 0};

    /* Captured t35/t43 case: credits morph is reached with a lost surface,
     * fallback owner, and the real game window away. */
    check(mtw_intro_morph_should_defer(&state), "lost surface while away defers");
    check(mtw_intro_morph_should_wait(&state), "away window keeps transition pending");

    state.window_foreground = 1;
    check(!mtw_intro_morph_should_wait(&state), "foreground resumes original morph");
    check(!mtw_intro_morph_should_defer(&state), "foreground does not start deferral");
    state.window_iconic = 1;
    check(mtw_intro_morph_should_wait(&state), "iconic game window is not ready");
    state.window_iconic = 0;
    state.surface_lost = 0;
    state.window_foreground = 0;
    check(!mtw_intro_morph_should_defer(&state), "healthy surface keeps native timing");
    state.surface_lost = 1;
    state.surface_present = 1;
    check(!mtw_intro_morph_should_defer(&state), "live surface keeps native timing");
    state.surface_present = 0;
    state.owner_present = 0;
    check(!mtw_intro_morph_should_defer(&state), "unknown backing keeps native timing");
    state.owner_present = 1;
    state.pump_available = 0;
    check(!mtw_intro_morph_should_defer(&state), "missing pump never traps intro");
    state.pump_available = 1;
    state.shutdown = 1;
    check(!mtw_intro_morph_should_wait(&state), "shutdown exits wait");
    state.shutdown = 0;
    state.window_valid = 0;
    check(!mtw_intro_morph_should_wait(&state), "destroyed window exits wait");
    check(!mtw_intro_morph_should_defer(&state), "destroyed window never defers");

    printf("RESULT passed=%d failed=%d\n", checks - failures, failures);
    return failures != 0;
}
