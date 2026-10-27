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
    {1u, 0u, 1u, 0u, "C8E440A5295D1E06C6A6ABD4E97C0244B235D7E103A2180F90F96048EAAA48A5", 242688u},
    {1u, 1u, 1u, 0u, "56A3AF8FEBF59146A04A10B184BA0388534B6A26B5701CA40CEA3ED645C48AB5", 257024u},
    {0u, 1u, 0u, 0u, "2969095115A0A6529D182AA005BC786AEA03FF53C8A20D3F2A0B8D583DBCBF4F", 146432u},
    {1u, 0u, 0u, 0u, "4DB6E53E2511DDAEB169810D4CCA1D74CDB35617E71D81A4A550EB5306EEAD59", 240640u},
    {1u, 1u, 0u, 0u, "80EFAF6D370DBFC7A33627C8F99281EE4C331E3DDD00814F8D800749063B16F8", 254976u},
    {0u, 0u, 1u, 0u, "70AEBD7421FF513D9BEC8145C77635EF6F926B1CEC46DADA4D5A400FBD7E35B1", 115712u},
    {0u, 1u, 1u, 0u, "B7785F86DB773A4A05CB1AA826366458DF8CA1D48B847A1142C1F9C7B47C54B4", 147456u},
    {1u, 0u, 1u, 1u, "C0EDD21FF1044306ACA9751C9375A26B44894F1D578C3D555AD789FBF699614A", 257024u},
    {1u, 1u, 1u, 1u, "D97F234A35250C2CF683C610CF14458203EC8A147A15FC0B3E9B67B854A1253B", 257024u},
    {1u, 0u, 0u, 1u, "E2A59EA0AC57D3DF4810570EBA1373C3C344BAF1A6A095B970854F4A502E4FA5", 254976u},
    {1u, 1u, 0u, 1u, "AB7231B61FE49B0764046645D65EDC2EC9AEF320BEA4FA0AF2FD5147EF60BE81", 254976u},
    {0u, 0u, 0u, 1u, "51403B722EEE23479760344F8F6D2C8F04116E398313C36106909D4064BC9B5B", 146432u},
    {0u, 1u, 0u, 1u, "627F3B49FDAD67A814B3E5FF465BFE0D094E9E27EE66860E4EBA36671F0BEE70", 146432u},
    {0u, 0u, 1u, 1u, "950172A7BEF89ECBFD5E5E1E66C3260AAAF19F2DC4558E1F582E62B079813143", 147456u},
    {0u, 1u, 1u, 1u, "A2C77DE7FC7DCE024297D07F8850BBBCBCC84FEA6A6269D9FBB552ACE298E499", 147456u},
    {0u, 0u, 0u, 0u, "DF4DB5B0EA34B4F48425B2C5981508120457285FEE7DF781BD3EFE1C6EDB25F6", 114688u},
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
