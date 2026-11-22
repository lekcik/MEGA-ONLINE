#pragma once

#include <cstdint>

namespace mega {

// Deterministic PCG32 RNG for reproducible megastructure generation & textures
class Rng {
public:
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ULL) {
        state_ = seed + 0x9E3779B97F4A7C15ULL;
        nextU32();
    }

    uint32_t nextU32() {
        uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    // [0.0f, 1.0f)
    float nextFloat() {
        return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
    }

    // [minVal, maxVal] inclusive
    int rangeInt(int minVal, int maxVal) {
        if (minVal >= maxVal) return minVal;
        uint32_t span = static_cast<uint32_t>(maxVal - minVal + 1);
        return minVal + static_cast<int>(nextU32() % span);
    }

    bool chance(float p) {
        return nextFloat() < p;
    }

private:
    uint64_t state_;
};

} // namespace mega
