/* Generated from the exact source-built Credits payload manifest. */
#ifndef MTW_CREDITS_PAYLOAD_HASHES_H
#define MTW_CREDITS_PAYLOAD_HASHES_H
#include <stddef.h>
#include <string.h>
typedef struct MtwCreditsProxyVariant {
    unsigned char terrain, fast, icon, zoom;
    const char *sha256;
    size_t file_length;
} MtwCreditsProxyVariant;
static const MtwCreditsProxyVariant mtw_credits_proxy_variants[] = {
    {1u, 0u, 1u, 0u, "6BBC90F043761CE3F9FC0489376DFC13EBAE11C528680334309E0E9A585E1CF1", 259072u},
    {1u, 1u, 1u, 0u, "D5728A19EB0479A3872422F71C548789E42627D60E7AA59CEA0B0CA3CE1CD1AA", 277504u},
    {0u, 1u, 0u, 0u, "E6F98F1B90A67F0D8F32BA2EE7F7D639E12ACA1A351A5FE18C86A9B1F49EF4B3", 175104u},
    {1u, 0u, 0u, 0u, "81B22A3726585B76DAB97BF6AC1D8297EFEDCA1DAA1B5F9667FD758D19C7C050", 257024u},
    {1u, 1u, 0u, 0u, "615EFAC0DD331B400552203067E22D04DC27B356348B80EDBCB8175D4C54F5BE", 275968u},
    {0u, 0u, 1u, 0u, "CAAA6C0E929EDBFE26F96E0F22AB475086B203F07CBF0E89C5B67C1E0E9D9C89", 117248u},
    {0u, 1u, 1u, 0u, "07E7A41782C83C2F8CA6C86A62C9D4E346D7D4B86E3D21796147EEB735930349", 177152u},
    {1u, 0u, 1u, 1u, "1C4C5126C02B25D7285FF4417202F1E114ECB4C38500E025895A239AA2E39498", 277504u},
    {1u, 1u, 1u, 1u, "4AADBB0676B3FC4D9E3CFC10831D154CB3950D819B27AD95689164101160BEBC", 277504u},
    {1u, 0u, 0u, 1u, "A7BA82923B1983A485EE743F7F15C897E86D6F6A0D49AD26FEC9736026CF1C2E", 275968u},
    {1u, 1u, 0u, 1u, "1C6E4BA7BEDEEBE153F0FFE87BC8C69D4E3D00E25086FE9EF72698BD38B455D7", 275968u},
    {0u, 0u, 0u, 1u, "87F4E6BC5DDAE12BECDA18C6596B1572F0FFF9921B65BB6B97DA5D09CBF5ABF8", 175104u},
    {0u, 1u, 0u, 1u, "CCB322FF214E2ED8E25BAE5B168456A762E304B255F0D60AD78E10DA1E9660C5", 175104u},
    {0u, 0u, 1u, 1u, "F6E09AE5DAFAAAC0599591D370EA1FED60ED0260B61964DB0C97BC4E4E497DDB", 177152u},
    {0u, 1u, 1u, 1u, "A73231E727FE38F69BCF1C084814E3879F65001F964BB22F12B8679E73909AC5", 177152u},
    {0u, 0u, 0u, 0u, "B9553A1580F85C269C77DB33D32A1250EA20844D7447871E561108B62FEBCA74", 115712u},
};
static inline const MtwCreditsProxyVariant *mtw_credits_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom) {
    size_t i;
    for (i = 0u; i < sizeof(mtw_credits_proxy_variants) / sizeof(mtw_credits_proxy_variants[0]); ++i) {
        const MtwCreditsProxyVariant *v = &mtw_credits_proxy_variants[i];
        if (v->terrain == terrain && v->fast == fast && v->icon == icon && v->zoom == zoom)
            return v;
    }
    return NULL;
}
static inline int mtw_credits_proxy_known_hash(const char *sha256) {
    size_t i;
    if (!sha256) return 0;
    for (i = 0u; i < sizeof(mtw_credits_proxy_variants) / sizeof(mtw_credits_proxy_variants[0]); ++i)
        if (!strcmp(sha256, mtw_credits_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
