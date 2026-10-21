/* Frozen previous Credits proxy identities for upgrades after the campaign locale wording change. */
#ifndef MTW_CREDITS_PAYLOAD_R288_HASHES_H
#define MTW_CREDITS_PAYLOAD_R288_HASHES_H
#include "credits_payload_hashes.h"
static const MtwCreditsProxyVariant mtw_r288_credits_proxy_variants[] = {
    {1u, 0u, 1u, 0u, "EBF7EDBF9D8B411DC7B12A08065BF8FF9BBB447EF5AF3AD10A8D88605BA43814", 234496u},
    {1u, 1u, 1u, 0u, "34FFF18E0A9FE099D4EBA53D4E7E8B4FC0945E1F5138B3C22139E5FC5C1CE924", 249344u},
    {0u, 1u, 0u, 0u, "FDDD032415FB1027D801E760A7357E559CA99C4D087E72E52F410ACF240D8ADF", 146432u},
    {1u, 0u, 0u, 0u, "5026C0DD242A13877632CE8E62960638916CB4B741E80924AA2EB1FC23903DB2", 232960u},
    {1u, 1u, 0u, 0u, "459F8210B7A7819A5ACA420F7F7FE54E6FF6438F0AE1EA761F485EBA06032C99", 247808u},
    {0u, 0u, 1u, 0u, "70AEBD7421FF513D9BEC8145C77635EF6F926B1CEC46DADA4D5A400FBD7E35B1", 115712u},
    {0u, 1u, 1u, 0u, "8A748497CB7795C37FDE042A5007E6A3F76B7C4DF85AA1A580A4ECE890A804DE", 147456u},
    {1u, 0u, 1u, 1u, "B5A5E24D2324F1EC8576C68BF5C418BB4541EE141FC7AE2B18197C4F9CF68CCC", 249344u},
    {1u, 1u, 1u, 1u, "2385F9840109C0CF1D1934F4CEBBEBD5C85A5C446138A577597939A2A294DB79", 249344u},
    {1u, 0u, 0u, 1u, "CA7361007E763B1DE9CCB2CCBB427B7D4301FFBD3832AB8E6F3995877D42D8AD", 247808u},
    {1u, 1u, 0u, 1u, "CC04423CA33A79EE70F9B5E033E9A7D88BCFD425FE6917948E85313C2EF0248E", 247808u},
    {0u, 0u, 0u, 1u, "A32DB766C8781726728E82509546B993EC1995A72DF3B4E6C5C29E684880FCAC", 146432u},
    {0u, 1u, 0u, 1u, "9B36058D5148C383A949A489A172F0AAB926D4115CADA54BA0E225D0418D2057", 146432u},
    {0u, 0u, 1u, 1u, "A3775D42847C4630ACE5A09CFAFBB69928592A11133362E106C96D0E6B6CBC26", 147456u},
    {0u, 1u, 1u, 1u, "436B76A146BA475BC1C408EF2F74067D575D6A9BA389A666171D611DC1FAA414", 147456u},
    {0u, 0u, 0u, 0u, "DF4DB5B0EA34B4F48425B2C5981508120457285FEE7DF781BD3EFE1C6EDB25F6", 114688u},
};
static inline const MtwCreditsProxyVariant *mtw_r288_credits_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom) {
    size_t i;
    for (i = 0u; i < sizeof(mtw_r288_credits_proxy_variants) /
                      sizeof(mtw_r288_credits_proxy_variants[0]); ++i) {
        const MtwCreditsProxyVariant *v = &mtw_r288_credits_proxy_variants[i];
        if (v->terrain == terrain && v->fast == fast && v->icon == icon && v->zoom == zoom)
            return v;
    }
    return NULL;
}
static inline int mtw_r288_credits_proxy_known_hash(const char *sha256) {
    size_t i;
    if (!sha256) return 0;
    for (i = 0u; i < sizeof(mtw_r288_credits_proxy_variants) /
                      sizeof(mtw_r288_credits_proxy_variants[0]); ++i)
        if (!strcmp(sha256, mtw_r288_credits_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
