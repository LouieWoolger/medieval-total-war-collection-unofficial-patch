/* Frozen R287 Credits payload identities for validating upgrade receipts. */
#ifndef MTW_CREDITS_PAYLOAD_PREVIOUS_HASHES_H
#define MTW_CREDITS_PAYLOAD_PREVIOUS_HASHES_H
#include "credits_payload_hashes.h"
static const MtwCreditsProxyVariant mtw_previous_credits_proxy_variants[] = {
    {1u, 0u, 1u, 0u, "2420481F7BAE6054DFF82FA7F4C30F7EBA4387C89F989AD59419569E8D3FF54A", 218112u},
    {1u, 1u, 1u, 0u, "64FA6C466F6678A72AC2796F0B7AAE8774544884A6DDBE860501A462564F7ACB", 228864u},
    {0u, 1u, 0u, 0u, "50EAD0C8839A261FC23552B5702203D9660CB902C4E940A25EF69F3029978BF2", 130048u},
    {1u, 0u, 0u, 0u, "5A4FC1AF3E9FC12DBA66913ED06BC1EF79CC547300907C9B7132163D3AC8FD9A", 216576u},
    {1u, 1u, 0u, 0u, "4926B8A12A69A93DFFDECFDBC9DAB53AB08A3E1860C18B985DD24D9FDD46C5A3", 227328u},
    {0u, 0u, 1u, 0u, "04359F74AF2BB6079CA3C80BE6050F763F1AC4077BB52585A2374D964D445054", 103424u},
    {0u, 1u, 1u, 0u, "094321B858456A52D6BAB4D6C0053115A4AFC8672536D36284DBBA67CB1E1AB9", 131072u},
    {1u, 0u, 1u, 1u, "6C65BB7CB3796AFDFEB452A78B87DDB22C7406259C18AC2CF71A1E9A9F0884BB", 228864u},
    {1u, 1u, 1u, 1u, "6381976FE451AC15722280EF8C6F19AC83797B27C351D947B3B4F1B69D7EBBD3", 228864u},
    {1u, 0u, 0u, 1u, "93AD14A6E78ABA3A338EDECE326D3D85C261D7C9F74A3BF190281255F20EFD69", 226816u},
    {1u, 1u, 0u, 1u, "65330A00E0D7AE8787C739B474A3C69E5E7FE07E1CD6220A15D20B6BED03712C", 226816u},
    {0u, 0u, 0u, 1u, "17AB23E04C82BB6FCEEB65BB41CDCFEB75E06EFF4DF1C7CA6BF909E521BEFF52", 130048u},
    {0u, 1u, 0u, 1u, "7EE697B3A488FD53E4D1B7CD5D2391F56C973B66BB81F303D6C00D7136198815", 130048u},
    {0u, 0u, 1u, 1u, "D1E5E85AD9CA66B0B3009DE2CE0FEE35F818C5AA358F99B38CE3CBEDD1C757A6", 131072u},
    {0u, 1u, 1u, 1u, "87A4E59C4D08676B3137EF41AB3B4E47D61C87EF7990C7E632E31E17BF2C5638", 131072u},
    {0u, 0u, 0u, 0u, "C858F63D741D1071C2BE56ACE758D2D609E52A147CE38CF94FF392AA9350452A", 102400u},
};
static inline const MtwCreditsProxyVariant *mtw_previous_credits_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom) {
    size_t i;
    for (i = 0u; i < sizeof(mtw_previous_credits_proxy_variants) /
                      sizeof(mtw_previous_credits_proxy_variants[0]); ++i) {
        const MtwCreditsProxyVariant *v = &mtw_previous_credits_proxy_variants[i];
        if (v->terrain == terrain && v->fast == fast && v->icon == icon && v->zoom == zoom)
            return v;
    }
    return NULL;
}
static inline int mtw_previous_credits_proxy_known_hash(const char *sha256) {
    size_t i;
    if (!sha256) return 0;
    for (i = 0u; i < sizeof(mtw_previous_credits_proxy_variants) /
                      sizeof(mtw_previous_credits_proxy_variants[0]); ++i)
        if (!strcmp(sha256, mtw_previous_credits_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
