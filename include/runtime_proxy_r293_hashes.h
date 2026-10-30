/* Released R293 feature identities retained by tools/refresh-feature-runtime.py. */
#ifndef MTW_RUNTIME_PROXY_R293_HASHES_H
#define MTW_RUNTIME_PROXY_R293_HASHES_H
#include <stddef.h>
#include <string.h>
typedef struct MtwR293FeatureProxyVariant {
    unsigned char fast, icon, zoom, credits;
    const char *sha256;
    size_t file_length;
} MtwR293FeatureProxyVariant;
static const MtwR293FeatureProxyVariant mtw_r293_feature_proxy_variants[] = {
    {1u, 0u, 0u, 0u, "5438D9ECD918630FB128869062823B5991BEA97CFB686FD477F22F24FC47C67A", 161280u},
    {0u, 1u, 0u, 0u, "FB2E4409D477888154EED7D9351EC18F4AB3A606487746E0E8ECFA0A5F89F8BB", 126464u},
    {1u, 1u, 0u, 0u, "7404A941F981005771B72BCE7D5A125A9D28B9A9CC3F4991F73BB4CCAACB0721", 162816u},
    {0u, 0u, 1u, 0u, "E6FCFEB3CA81CE405495D5BDED13240BD0B12857369D1CC952CC0781A77BA120", 161280u},
    {1u, 0u, 1u, 0u, "27B8D0AA29363CD13A13AA44F2AAE7E552396034821AAF8F028ED200882F37BF", 161280u},
    {0u, 1u, 1u, 0u, "78C16B5F9635C1A3F3FE7DCEF4B0E63AE238AC000AE3407F167902AF7889C52A", 162816u},
    {1u, 1u, 1u, 0u, "C934473FDB52D39F796E83CA5A070B2B488315684768A8A95B7BF33CDB66EF86", 162816u},
    {0u, 0u, 0u, 1u, "079953CB29128055ACCE302F750F42C991BB369D8B709AAF04252FFD5BB8831A", 126976u},
    {1u, 0u, 0u, 1u, "6F0B0E3F130FB7E88A594B202801045C9DEF9B01BCE347CDB9858BDA34A0703A", 163328u},
    {0u, 1u, 0u, 1u, "69DE7D67A5FAD5E954F4C330C4F295FA7534B36B0BF0E42C655887445ED50610", 128000u},
    {1u, 1u, 0u, 1u, "CFCE1A4F4D80F2DF5FBE953446E628E662DE7A0F8C94B750D42F93A1B5B866F8", 164352u},
    {0u, 0u, 1u, 1u, "0C546BFB6A6DC135BC97FE3FF113F242A64CC09AEE26A6C3E1724753343E2A9C", 162816u},
    {1u, 0u, 1u, 1u, "D249E95E4E96ABEEE13E5064945382270F9108AEA984EF8E8728E24C93794E67", 162816u},
    {0u, 1u, 1u, 1u, "731F50A6D835FE64106E711D0A4A2113203FFB69D6CD3B6CA208EA803384C180", 164352u},
    {1u, 1u, 1u, 1u, "CDEBB7AFB60F0E4E7D373C8D282FD4F0B13960D85B625E178312044AC90AA34C", 164352u},
};
static inline const MtwR293FeatureProxyVariant *mtw_r293_feature_proxy_for_options(
    unsigned int terrain, unsigned int fast, unsigned int icon, unsigned int zoom, unsigned int credits) {
    size_t i;
    if (terrain) return NULL;
    for (i = 0u; i < sizeof(mtw_r293_feature_proxy_variants) / sizeof(mtw_r293_feature_proxy_variants[0]); ++i) {
        const MtwR293FeatureProxyVariant *v = &mtw_r293_feature_proxy_variants[i];
        if (v->fast == fast && v->icon == icon && v->zoom == zoom && v->credits == credits) return v;
    }
    return NULL;
}
static inline int mtw_r293_feature_proxy_known_hash(const char *hash) {
    size_t i;
    if (!hash) return 0;
    for (i = 0u; i < sizeof(mtw_r293_feature_proxy_variants) / sizeof(mtw_r293_feature_proxy_variants[0]); ++i)
        if (!strcmp(hash, mtw_r293_feature_proxy_variants[i].sha256)) return 1;
    return 0;
}
#endif
