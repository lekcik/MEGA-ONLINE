#include "world/LightBaker.hpp"
#include <algorithm>
#include <queue>

namespace mega {

namespace {
struct LightNode {
    int x, y, z;
    uint8_t r, g, b;
};
} // namespace

void LightBaker::bake(WorldGrid& world,
                      const std::vector<LightSource>& sources,
                      uint8_t baseAmbientR,
                      uint8_t baseAmbientG,
                      uint8_t baseAmbientB) {
    const int sx = world.sizeX();
    const int sy = world.sizeY();
    const int sz = world.sizeZ();
    const size_t total = static_cast<size_t>(sx * sy * sz);

    std::vector<uint8_t> lr(total, baseAmbientR);
    std::vector<uint8_t> lg(total, baseAmbientG);
    std::vector<uint8_t> lb(total, baseAmbientB);

    auto idx = [&](int x, int y, int z) -> size_t {
        return static_cast<size_t>((y * sz + z) * sx + x);
    };

    std::queue<LightNode> q;

    // Seed from emissive blocks in the world
    for (int y = 0; y < sy; ++y) {
        for (int z = 0; z < sz; ++z) {
            for (int x = 0; x < sx; ++x) {
                Cell c = world.getCell(x, y, z);
                if (c.isEmissive()) {
                    size_t i = idx(x, y, z);
                    lr[i] = 110;
                    lg[i] = 240;
                    lb[i] = 255;
                    q.push({x, y, z, 110, 240, 255});
                }
            }
        }
    }

    // Seed from explicit light sources
    for (const auto& s : sources) {
        if (!world.inBounds(s.x, s.y, s.z)) continue;
        size_t i = idx(s.x, s.y, s.z);
        lr[i] = std::max(lr[i], s.r);
        lg[i] = std::max(lg[i], s.g);
        lb[i] = std::max(lb[i], s.b);
        q.push({s.x, s.y, s.z, lr[i], lg[i], lb[i]});
    }

    static constexpr int DX[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int DY[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int DZ[6] = {0, 0, 0, 0, 1, -1};
    static constexpr int STEP_DROP = 22;

    while (!q.empty()) {
        LightNode cur = q.front();
        q.pop();

        for (int d = 0; d < 6; ++d) {
            int nx = cur.x + DX[d];
            int ny = cur.y + DY[d];
            int nz = cur.z + DZ[d];
            if (!world.inBounds(nx, ny, nz)) continue;

            Cell nc = world.getCell(nx, ny, nz);
            if (nc.isOpaque()) continue;

            uint8_t nr = (cur.r > STEP_DROP) ? static_cast<uint8_t>(cur.r - STEP_DROP) : 0;
            uint8_t ng = (cur.g > STEP_DROP) ? static_cast<uint8_t>(cur.g - STEP_DROP) : 0;
            uint8_t nb = (cur.b > STEP_DROP) ? static_cast<uint8_t>(cur.b - STEP_DROP) : 0;

            size_t ni = idx(nx, ny, nz);
            bool pushed = false;
            if (nr > lr[ni]) { lr[ni] = nr; pushed = true; }
            if (ng > lg[ni]) { lg[ni] = ng; pushed = true; }
            if (nb > lb[ni]) { lb[ni] = nb; pushed = true; }

            if (pushed) {
                q.push({nx, ny, nz, lr[ni], lg[ni], lb[ni]});
            }
        }
    }

    // Write baked light back into chunks
    for (int y = 0; y < sy; ++y) {
        for (int z = 0; z < sz; ++z) {
            for (int x = 0; x < sx; ++x) {
                size_t i = idx(x, y, z);
                world.setLight(x, y, z, lr[i], lg[i], lb[i]);
            }
        }
    }
}

} // namespace mega
