#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dust_draw_identity.h"

static int passed;
static int failed;

static void check(int condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL %s\n", name); }
}

int main(void) {
    const uintptr_t base = (uintptr_t)0x00400000u;
    const uintptr_t site = base + MTW_DUST_DRAW_RETURN_RVA;
    /* B3FDC7 is the return of the actual vtable DrawIndexedPrimitiveUP call.
     * 1B9F0D is an outer particle-manager call visible in stack walks. */
    const uintptr_t mixed_site = site;
    uint8_t batch[5u * 5u * 32u] = {0};
    const uint32_t black = 0x1F000000u;
    const uint32_t white = 0x5FFFFFFFu;
    const uint32_t brown = 0x2A94771Fu;
    check(mtw_dust_color_phase(0x2D94771Fu, site, base) == MTW_DUST_BROWN_PHASE,
          "observed brown phase");
    check(mtw_dust_color_phase(0xFF94771Fu, site, base) == MTW_DUST_BROWN_PHASE,
          "brown alpha variation");
    check(mtw_dust_color_phase(0x7F202020u, site, base) == MTW_DUST_GRAY_PHASE,
          "observed gray phase");
    check(mtw_dust_color_phase(0x00202020u, site, base) == MTW_DUST_GRAY_PHASE,
          "gray alpha variation");
    check(mtw_dust_color_phase(0x7F202020u, site - 1u, base) == MTW_DUST_NONE,
          "same-format gray impostor at neighboring callsite");
    check(mtw_dust_color_phase(0x7F202020u, site, 0u) == MTW_DUST_NONE,
          "gray requires supported game image");
    check(mtw_dust_color_phase(0x7F202020u, site, base + 0x1000u) == MTW_DUST_NONE,
          "gray rejects different image offset");
    check(mtw_dust_color_phase(0x7F202021u, site, base) == MTW_DUST_NONE,
          "adjacent gray color rejected");
    check(mtw_dust_color_phase(0xFF94771Fu, site - 1u, base) == MTW_DUST_BROWN_PHASE,
          "established brown eligibility preserved");
    check(mtw_dust_counts_as_reference_work(MTW_DUST_BROWN_PHASE),
          "calibrated brown work retained");
    check(!mtw_dust_counts_as_reference_work(MTW_DUST_GRAY_PHASE),
          "new gray render phase leaves native-step model unchanged");
    check(!mtw_dust_counts_as_reference_work(MTW_DUST_NONE),
          "unknown draw leaves native-step model unchanged");
    /* Actual red trace: one DrawIndexedPrimitiveUP contained leading black
     * billboards followed by hundreds of brown ones from the same game site. */
    memcpy(batch + 16u, &black, sizeof(black));
    memcpy(batch + 5u * 32u + 16u, &brown, sizeof(brown));
    check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, base) ==
          MTW_DUST_MIXED_PHASE, "black-first mixed dust batch is repaired");
    check(mtw_dust_counts_as_reference_work(MTW_DUST_MIXED_PHASE),
          "mixed brown batch still contributes reference work");
    memcpy(batch + 16u, &white, sizeof(white));
    check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, base) ==
          MTW_DUST_MIXED_PHASE, "white-first mixed dust batch is repaired");
    {
        const uint32_t gray = 0x7F202020u;
        memcpy(batch + 16u, &gray, sizeof(gray));
        check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, base) ==
              MTW_DUST_MIXED_PHASE, "gray-first mixed dust contributes reference work");
        memcpy(batch + 5u * 32u + 16u, &black, sizeof(black));
        check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, base) ==
              MTW_DUST_GRAY_PHASE, "pure gray phase retains existing bridge behavior");
        memcpy(batch + 5u * 32u + 16u, &brown, sizeof(brown));
        memcpy(batch + 16u, &white, sizeof(white));
    }
    check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site + 1u, base) ==
          MTW_DUST_NONE, "mixed batch rejects neighboring game callsite");
    check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, 0u) ==
          MTW_DUST_NONE, "mixed batch requires supported game image");
    check(mtw_dust_batch_phase(batch, 24u, 32u, mixed_site, base) ==
          MTW_DUST_NONE, "mixed batch rejects incomplete billboard");
    check(mtw_dust_batch_phase(batch, 25u, 28u, mixed_site, base) ==
          MTW_DUST_NONE, "mixed batch rejects other vertex layout");
    memcpy(batch + 5u * 32u + 16u, &black, sizeof(black));
    check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, base) ==
          MTW_DUST_NONE, "pure smoke and white batch stays unselected");
    memcpy(batch + 16u, &brown, sizeof(brown));
    check(mtw_dust_batch_phase(batch, 25u, 32u, site, base) ==
          MTW_DUST_BROWN_PHASE, "existing brown caller remains selected");
    memcpy(batch + 16u, &black, sizeof(black));
    /* Corner colors cannot impersonate billboard-center diffuse colors. */
    memcpy(batch + 5u * 32u + 32u + 16u, &brown, sizeof(brown));
    check(mtw_dust_batch_phase(batch, 25u, 32u, mixed_site, base) ==
          MTW_DUST_NONE, "corner-only brown does not select smoke batch");
    printf("RESULT passed=%d failed=%d\n", passed, failed);
    return failed ? 1 : 0;
}
