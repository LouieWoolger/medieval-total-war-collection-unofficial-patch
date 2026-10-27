/* Frozen R291 Credits identities for dust alpha upgrades. */
#ifndef MTW_CREDITS_PAYLOAD_R291_HASHES_H
#define MTW_CREDITS_PAYLOAD_R291_HASHES_H
#include "credits_payload_hashes.h"
static const MtwCreditsProxyVariant mtw_r291_credits_proxy_variants[] = {
    {1u, 0u, 1u, 0u, "1108456972504FEDD1E5A5ECC6498E16DF5F1CA54C9D7B8A1450CE4B58C73A8F", 237056u},
    {1u, 1u, 1u, 0u, "C661AE8537DF3B8C069E25898A1CFBE6546953B4098E0978FE5B1FE50ACEDCD4", 251392u},
    {0u, 1u, 0u, 0u, "2969095115A0A6529D182AA005BC786AEA03FF53C8A20D3F2A0B8D583DBCBF4F", 146432u},
    {1u, 0u, 0u, 0u, "06D2C012706925E56C30A0D8EFBD114C8667CF88F0F56E5C347AAFB2A307EEB6", 236032u},
    {1u, 1u, 0u, 0u, "81EAC812E307F8A9A479E0319012197E1492C6539184C0BC0EEA50B64A4549D8", 250368u},
    {0u, 0u, 1u, 0u, "70AEBD7421FF513D9BEC8145C77635EF6F926B1CEC46DADA4D5A400FBD7E35B1", 115712u},
    {0u, 1u, 1u, 0u, "B7785F86DB773A4A05CB1AA826366458DF8CA1D48B847A1142C1F9C7B47C54B4", 147456u},
    {1u, 0u, 1u, 1u, "43616022C83A96FF11E5E7A6038DFED8971F14F886EA57399CBA8BBE761BB29F", 251392u},
    {1u, 1u, 1u, 1u, "8619F11188C1D336A222986D39384D3EF7687BC40980D0102FEC641CEA0ECAFF", 251392u},
    {1u, 0u, 0u, 1u, "2293F00EF0CAF07997745A19E96E804ECDF03614AE91FAE6B76C8966DC067941", 250368u},
    {1u, 1u, 0u, 1u, "228934EEF5080474740293E7EE49DD633C523762265BF24452F5DE88FE17266C", 250368u},
    {0u, 0u, 0u, 1u, "51403B722EEE23479760344F8F6D2C8F04116E398313C36106909D4064BC9B5B", 146432u},
    {0u, 1u, 0u, 1u, "627F3B49FDAD67A814B3E5FF465BFE0D094E9E27EE66860E4EBA36671F0BEE70", 146432u},
    {0u, 0u, 1u, 1u, "950172A7BEF89ECBFD5E5E1E66C3260AAAF19F2DC4558E1F582E62B079813143", 147456u},
    {0u, 1u, 1u, 1u, "A2C77DE7FC7DCE024297D07F8850BBBCBCC84FEA6A6269D9FBB552ACE298E499", 147456u},
    {0u, 0u, 0u, 0u, "DF4DB5B0EA34B4F48425B2C5981508120457285FEE7DF781BD3EFE1C6EDB25F6", 114688u},
};
static inline const MtwCreditsProxyVariant *mtw_r291_credits_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom) {
    size_t i;
    for (i = 0u; i < sizeof(mtw_r291_credits_proxy_variants) / sizeof(mtw_r291_credits_proxy_variants[0]); ++i) {
        const MtwCreditsProxyVariant *v = &mtw_r291_credits_proxy_variants[i];
        if (v->terrain == terrain && v->fast == fast && v->icon == icon && v->zoom == zoom)
            return v;
    }
    return NULL;
}
static inline int mtw_r291_credits_proxy_known_hash(const char *sha256) {
    size_t i;
    if (!sha256) return 0;
    for (i = 0u; i < sizeof(mtw_r291_credits_proxy_variants) / sizeof(mtw_r291_credits_proxy_variants[0]); ++i)
        if (!strcmp(sha256, mtw_r291_credits_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
