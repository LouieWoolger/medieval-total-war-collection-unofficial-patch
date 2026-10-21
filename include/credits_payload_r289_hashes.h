/* Frozen R289 Credits payload identities for receipt upgrades after zoom tooltip wording change. */
#ifndef MTW_CREDITS_PAYLOAD_R289_HASHES_H
#define MTW_CREDITS_PAYLOAD_R289_HASHES_H
#include "credits_payload_hashes.h"
static const MtwCreditsProxyVariant mtw_r289_credits_proxy_variants[] = {
    {1u, 0u, 1u, 0u, "EBF7EDBF9D8B411DC7B12A08065BF8FF9BBB447EF5AF3AD10A8D88605BA43814", 234496u},
    {1u, 1u, 1u, 0u, "980E5C92DF4912A0925906B0BDA5856B60427D480B81D5ABC8146632E8AD1FB8", 249344u},
    {0u, 1u, 0u, 0u, "5FF741DE9CF2F785D58D983EBED8BBCD3EB156ED58233192E6A4297E9780FDF7", 146432u},
    {1u, 0u, 0u, 0u, "5026C0DD242A13877632CE8E62960638916CB4B741E80924AA2EB1FC23903DB2", 232960u},
    {1u, 1u, 0u, 0u, "B2A4FDC8F1D5D6FA09AE4146197EA552CC7C0B6F2313995EAAE01490FC8A1A77", 247808u},
    {0u, 0u, 1u, 0u, "70AEBD7421FF513D9BEC8145C77635EF6F926B1CEC46DADA4D5A400FBD7E35B1", 115712u},
    {0u, 1u, 1u, 0u, "B3DBD18BBE496D74B58D3DCD01191F97C4EA112DF9E57143623B318B7D06B2E4", 147456u},
    {1u, 0u, 1u, 1u, "191982AC2D10845A171308816C25CB4176CD0000A70787AA738420DDF6D86337", 249344u},
    {1u, 1u, 1u, 1u, "5EBF3444093DBDDE3D0416345C36DB0201EB6979E065AB3B3E02BC4E8AF6ED80", 249344u},
    {1u, 0u, 0u, 1u, "3B3B6125719C8F6BF888247563B7972735AB4FD27B18A2795F1B0F068E836ED9", 247808u},
    {1u, 1u, 0u, 1u, "DDFFE62341BB0233A81F196AC9ADBDD0CD15AA9C7FE79B142ADEC959EAB60177", 247808u},
    {0u, 0u, 0u, 1u, "C543AE8DD588B833B3EC6BC0C24548F4EAA906DCD25858CAEFFA1610EF0E621D", 146432u},
    {0u, 1u, 0u, 1u, "DC0064071DD063B816643C1E1E7AF784C8922BF3042964641BB06EBA0554FDD6", 146432u},
    {0u, 0u, 1u, 1u, "EF533B08029B5D5AB687AEBF5791186DB618E844EAEA4A8A8111EB602B4165A3", 147456u},
    {0u, 1u, 1u, 1u, "CF4D0DE4E72AC03170628307C1DC84C325F6F9CA4DDA1C65DA17B735B1A6D25D", 147456u},
    {0u, 0u, 0u, 0u, "DF4DB5B0EA34B4F48425B2C5981508120457285FEE7DF781BD3EFE1C6EDB25F6", 114688u},
};
static inline const MtwCreditsProxyVariant *mtw_r289_credits_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom) {
    size_t i;
    for (i = 0u; i < sizeof(mtw_r289_credits_proxy_variants) / sizeof(mtw_r289_credits_proxy_variants[0]); ++i) {
        const MtwCreditsProxyVariant *v = &mtw_r289_credits_proxy_variants[i];
        if (v->terrain == terrain && v->fast == fast && v->icon == icon && v->zoom == zoom)
            return v;
    }
    return NULL;
}
static inline int mtw_r289_credits_proxy_known_hash(const char *sha256) {
    size_t i;
    if (!sha256) return 0;
    for (i = 0u; i < sizeof(mtw_r289_credits_proxy_variants) / sizeof(mtw_r289_credits_proxy_variants[0]); ++i)
        if (!strcmp(sha256, mtw_r289_credits_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
