/* Frozen R290 Credits identities for deployment staging upgrades. */
#ifndef MTW_CREDITS_PAYLOAD_R290_HASHES_H
#define MTW_CREDITS_PAYLOAD_R290_HASHES_H
#include "credits_payload_hashes.h"
static const MtwCreditsProxyVariant mtw_r290_credits_proxy_variants[] = {
    {1u, 0u, 1u, 0u, "EBF7EDBF9D8B411DC7B12A08065BF8FF9BBB447EF5AF3AD10A8D88605BA43814", 234496u},
    {1u, 1u, 1u, 0u, "54A6EB996B44099A50A57B2365500A03A323F4A606BE08101C86775F388F62BD", 249344u},
    {0u, 1u, 0u, 0u, "2969095115A0A6529D182AA005BC786AEA03FF53C8A20D3F2A0B8D583DBCBF4F", 146432u},
    {1u, 0u, 0u, 0u, "5026C0DD242A13877632CE8E62960638916CB4B741E80924AA2EB1FC23903DB2", 232960u},
    {1u, 1u, 0u, 0u, "451D69ADBBD0B2D46391BCE5999C24E1C380A530C2FA760AD8223FBF441E5EEC", 247808u},
    {0u, 0u, 1u, 0u, "70AEBD7421FF513D9BEC8145C77635EF6F926B1CEC46DADA4D5A400FBD7E35B1", 115712u},
    {0u, 1u, 1u, 0u, "B7785F86DB773A4A05CB1AA826366458DF8CA1D48B847A1142C1F9C7B47C54B4", 147456u},
    {1u, 0u, 1u, 1u, "E6E51F4F201FCEE9AA55C27A47D4F3B5B766C40F42D7D560D41ADBBBBB1FF971", 249344u},
    {1u, 1u, 1u, 1u, "18F0F7070A35E21D6F20AE135A888F9B3A59A3F23439FE298080722F92F7AE39", 249344u},
    {1u, 0u, 0u, 1u, "226C1650B5C41C5ACEA48B768A0511B1B282B4D52F4D7FE0CF3EA476AFFB0155", 247808u},
    {1u, 1u, 0u, 1u, "4F12F8C30A96DAED45F24D984FFDA2DEBEC8B0E2B3C5DEEB5BEDE5463244B234", 247808u},
    {0u, 0u, 0u, 1u, "51403B722EEE23479760344F8F6D2C8F04116E398313C36106909D4064BC9B5B", 146432u},
    {0u, 1u, 0u, 1u, "627F3B49FDAD67A814B3E5FF465BFE0D094E9E27EE66860E4EBA36671F0BEE70", 146432u},
    {0u, 0u, 1u, 1u, "950172A7BEF89ECBFD5E5E1E66C3260AAAF19F2DC4558E1F582E62B079813143", 147456u},
    {0u, 1u, 1u, 1u, "A2C77DE7FC7DCE024297D07F8850BBBCBCC84FEA6A6269D9FBB552ACE298E499", 147456u},
    {0u, 0u, 0u, 0u, "DF4DB5B0EA34B4F48425B2C5981508120457285FEE7DF781BD3EFE1C6EDB25F6", 114688u},
};
static inline const MtwCreditsProxyVariant *mtw_r290_credits_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom) {
    size_t i;
    for (i = 0u; i < sizeof(mtw_r290_credits_proxy_variants) / sizeof(mtw_r290_credits_proxy_variants[0]); ++i) {
        const MtwCreditsProxyVariant *v = &mtw_r290_credits_proxy_variants[i];
        if (v->terrain == terrain && v->fast == fast && v->icon == icon && v->zoom == zoom)
            return v;
    }
    return NULL;
}
static inline int mtw_r290_credits_proxy_known_hash(const char *sha256) {
    size_t i;
    if (!sha256) return 0;
    for (i = 0u; i < sizeof(mtw_r290_credits_proxy_variants) / sizeof(mtw_r290_credits_proxy_variants[0]); ++i)
        if (!strcmp(sha256, mtw_r290_credits_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
