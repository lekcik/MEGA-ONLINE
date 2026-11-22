#pragma once

#include "world/WorldGrid.hpp"
#include <cstdint>
#include <vector>

namespace mega {

struct LightSource {
    int x, y, z;
    uint8_t r, g, b;
    uint8_t radius;
};

class LightBaker {
public:
    // Propagates colored light in 3D via BFS flood-fill from emissive blocks and explicit sources
    static void bake(WorldGrid& world,
                     const std::vector<LightSource>& sources,
                     uint8_t baseAmbientR = 38,
                     uint8_t baseAmbientG = 40,
                     uint8_t baseAmbientB = 45);
};

} // namespace mega
