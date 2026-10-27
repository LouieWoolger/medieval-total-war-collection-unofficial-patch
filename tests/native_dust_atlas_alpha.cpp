#include "../vendor/dust-fidelity/dust_atlas_alpha.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static constexpr unsigned kWidth = 256;
static constexpr unsigned kHeight = 256;
static constexpr unsigned kDustSide = 128;

static void check_fixture(std::vector<uint16_t> healthy,
                          uint64_t expected_hash, unsigned black_count) {
    const std::vector<uint16_t> original = healthy;
    const unsigned pitch = kWidth * sizeof(uint16_t);
    assert(mtw_repair_dust_atlas_alpha(healthy.data(), pitch,
                                       expected_hash, black_count) == 0);
    assert(healthy == original);

    std::vector<uint16_t> damaged = original;
    for (unsigned y = 0; y < kDustSide; ++y)
        for (unsigned x = 0; x < kDustSide; ++x)
            damaged[y * kWidth + x] |= 0x8000u;
    const unsigned repaired = mtw_repair_dust_atlas_alpha(
        damaged.data(), pitch, expected_hash, black_count);
    assert(repaired == black_count);
    assert(damaged == original);

    damaged = original;
    for (unsigned y = 0; y < kDustSide; ++y)
        for (unsigned x = 0; x < kDustSide; ++x)
            damaged[y * kWidth + x] |= 0x8000u;
    assert(mtw_repair_dust_atlas_alpha(damaged.data(), pitch,
                                       expected_hash ^ 1u, black_count) == 0);
    assert(damaged != original);
}

int main(int argc, char **argv) {
    std::vector<uint16_t> synthetic(kWidth * kHeight, 0x8421u);
    for (unsigned i = 0; i < 6847; ++i)
        synthetic[(i / kDustSide) * kWidth + (i % kDustSide)] = 0;
    check_fixture(synthetic, 0x67C7458CADB5D792ull, 6847);

    for (int index = 1; index < argc; ++index) {
        FILE *file = nullptr;
        assert(fopen_s(&file, argv[index], "rb") == 0);
        assert(file);
        std::vector<uint16_t> captured(kWidth * kHeight);
        assert(std::fread(captured.data(), sizeof(uint16_t), captured.size(), file)
               == captured.size());
        assert(std::fgetc(file) == EOF);
        std::fclose(file);
        check_fixture(captured, 0x389100512363FEBDull, 6847);

        // The natural fault makes the complete 256x256 sheet opaque. Repair
        // every clear alpha bit, including the other particle tiles.
        const std::vector<uint16_t> original = captured;
        assert(!mtw_dust_atlas_alpha_sentinels_bad(
            captured.data(), kWidth * sizeof(uint16_t)));
        for (uint16_t &pixel : captured) pixel |= 0x8000u;
        assert(mtw_dust_atlas_alpha_sentinels_bad(
            captured.data(), kWidth * sizeof(uint16_t)));
        assert(mtw_repair_dust_atlas_alpha(captured.data(),
                                           kWidth * sizeof(uint16_t),
                                           0x389100512363FEBDull, 6847) == 7310);
        assert(captured == original);
        assert(!mtw_dust_atlas_alpha_sentinels_bad(
            captured.data(), kWidth * sizeof(uint16_t)));

        // A later loss outside the dust tile must also restore its exact
        // transparency, including variants with changed RGB art there.
        captured[65533] |= 0x8000u;
        assert(mtw_dust_atlas_alpha_sentinels_bad(
            captured.data(), kWidth * sizeof(uint16_t)));
        assert(mtw_repair_dust_atlas_alpha(captured.data(),
                                           kWidth * sizeof(uint16_t),
                                           0x389100512363FEBDull, 6847) == 1);
        assert(captured == original);
    }
    std::puts("dust atlas alpha repair passed");
    return 0;
}
