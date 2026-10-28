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
    {0u, 1u, 0u, 0u, "6F0B0E3F130FB7E88A594B202801045C9DEF9B01BCE347CDB9858BDA34A0703A", 163328u},
    {1u, 0u, 0u, 0u, "81B22A3726585B76DAB97BF6AC1D8297EFEDCA1DAA1B5F9667FD758D19C7C050", 257024u},
    {1u, 1u, 0u, 0u, "615EFAC0DD331B400552203067E22D04DC27B356348B80EDBCB8175D4C54F5BE", 275968u},
    {0u, 0u, 1u, 0u, "69DE7D67A5FAD5E954F4C330C4F295FA7534B36B0BF0E42C655887445ED50610", 128000u},
    {0u, 1u, 1u, 0u, "CFCE1A4F4D80F2DF5FBE953446E628E662DE7A0F8C94B750D42F93A1B5B866F8", 164352u},
    {1u, 0u, 1u, 1u, "1C4C5126C02B25D7285FF4417202F1E114ECB4C38500E025895A239AA2E39498", 277504u},
    {1u, 1u, 1u, 1u, "4AADBB0676B3FC4D9E3CFC10831D154CB3950D819B27AD95689164101160BEBC", 277504u},
    {1u, 0u, 0u, 1u, "A7BA82923B1983A485EE743F7F15C897E86D6F6A0D49AD26FEC9736026CF1C2E", 275968u},
    {1u, 1u, 0u, 1u, "1C6E4BA7BEDEEBE153F0FFE87BC8C69D4E3D00E25086FE9EF72698BD38B455D7", 275968u},
    {0u, 0u, 0u, 1u, "0C546BFB6A6DC135BC97FE3FF113F242A64CC09AEE26A6C3E1724753343E2A9C", 162816u},
    {0u, 1u, 0u, 1u, "D249E95E4E96ABEEE13E5064945382270F9108AEA984EF8E8728E24C93794E67", 162816u},
    {0u, 0u, 1u, 1u, "731F50A6D835FE64106E711D0A4A2113203FFB69D6CD3B6CA208EA803384C180", 164352u},
    {0u, 1u, 1u, 1u, "CDEBB7AFB60F0E4E7D373C8D282FD4F0B13960D85B625E178312044AC90AA34C", 164352u},
    {0u, 0u, 0u, 0u, "079953CB29128055ACCE302F750F42C991BB369D8B709AAF04252FFD5BB8831A", 126976u},
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
