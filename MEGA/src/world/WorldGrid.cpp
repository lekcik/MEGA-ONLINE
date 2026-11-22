#include "world/WorldGrid.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

WorldGrid::WorldGrid(int chunksX, int chunksY, int chunksZ, BlockType defaultFill)
    : chunksX_(chunksX), chunksY_(chunksY), chunksZ_(chunksZ),
      infinite_(chunksX <= 0 || chunksZ <= 0),
      defaultFill_(defaultFill) {}

bool WorldGrid::inBounds(int x, int y, int z) const {
    if (infinite_) {
        return y >= 0 && y < Chunk::SIZE;
    }
    return x >= 0 && x < sizeX() &&
           y >= 0 && y < sizeY() &&
           z >= 0 && z < sizeZ();
}

Chunk& WorldGrid::getOrCreateChunk(int cx, int cy, int cz) {
    ChunkCoord key{cx, cy, cz};
    auto it = chunkMap_.find(key);
    if (it != chunkMap_.end()) return it->second;
    auto [ins, _] = chunkMap_.emplace(key, Chunk(defaultFill_));
    ins->second.cx = cx;
    ins->second.cy = cy;
    ins->second.cz = cz;
    ins->second.dirty = true;
    return ins->second;
}

void WorldGrid::markNeighborDirty(int cx, int cy, int cz) {
    auto it = chunkMap_.find({cx, cy, cz});
    if (it != chunkMap_.end()) {
        it->second.dirty = true;
    }
}

Cell WorldGrid::getCell(int x, int y, int z) const {
    if (!inBounds(x, y, z)) {
        Cell solid;
        solid.type = infinite_ ? defaultFill_ : BlockType::DarkSteel;
        return solid;
    }
    int cx = floorDiv(x, Chunk::SIZE), lx = floorMod(x, Chunk::SIZE);
    int cy = floorDiv(y, Chunk::SIZE), ly = floorMod(y, Chunk::SIZE);
    int cz = floorDiv(z, Chunk::SIZE), lz = floorMod(z, Chunk::SIZE);

    auto it = chunkMap_.find({cx, cy, cz});
    if (it == chunkMap_.end()) {
        Cell def;
        def.type = defaultFill_;
        return def;
    }
    return it->second.at(lx, ly, lz);
}

BlockType WorldGrid::getBlock(int x, int y, int z) const {
    return getCell(x, y, z).type;
}

void WorldGrid::setBlock(int x, int y, int z, BlockType type, bool halfSlab) {
    if (!inBounds(x, y, z)) return;
    int cx = floorDiv(x, Chunk::SIZE), lx = floorMod(x, Chunk::SIZE);
    int cy = floorDiv(y, Chunk::SIZE), ly = floorMod(y, Chunk::SIZE);
    int cz = floorDiv(z, Chunk::SIZE), lz = floorMod(z, Chunk::SIZE);

    auto& ch = getOrCreateChunk(cx, cy, cz);
    auto& cell = ch.at(lx, ly, lz);
    cell.type = type;
    cell.halfSlab = (type != BlockType::Air) && halfSlab;
    ch.dirty = true;

    if (lx == 0) markNeighborDirty(cx - 1, cy, cz);
    if (lx == Chunk::SIZE - 1) markNeighborDirty(cx + 1, cy, cz);
    if (ly == 0) markNeighborDirty(cx, cy - 1, cz);
    if (ly == Chunk::SIZE - 1) markNeighborDirty(cx, cy + 1, cz);
    if (lz == 0) markNeighborDirty(cx, cy, cz - 1);
    if (lz == Chunk::SIZE - 1) markNeighborDirty(cx, cy, cz + 1);
}

void WorldGrid::setLight(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b) {
    if (!inBounds(x, y, z)) return;
    int cx = floorDiv(x, Chunk::SIZE), lx = floorMod(x, Chunk::SIZE);
    int cy = floorDiv(y, Chunk::SIZE), ly = floorMod(y, Chunk::SIZE);
    int cz = floorDiv(z, Chunk::SIZE), lz = floorMod(z, Chunk::SIZE);

    auto it = chunkMap_.find({cx, cy, cz});
    if (it == chunkMap_.end()) return;
    auto& c = it->second.at(lx, ly, lz);
    c.lightR = r;
    c.lightG = g;
    c.lightB = b;
    it->second.dirty = true;
}

void WorldGrid::fillBox(int x0, int y0, int z0, int x1, int y1, int z1, BlockType type, bool halfSlab) {
    int minX = std::min(x0, x1), maxX = std::max(x0, x1);
    int minY = std::min(y0, y1), maxY = std::max(y0, y1);
    int minZ = std::min(z0, z1), maxZ = std::max(z0, z1);
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                setBlock(x, y, z, type, halfSlab);
            }
        }
    }
}

void WorldGrid::addProp(const AABB& box, BlockType material, bool collidable,
                        bool emissive, bool stretchUV, int regX, int regZ) {
    props_.push_back({box, material, emissive, stretchUV, collidable, regX, regZ});
    if (collidable) {
        propColliders_.push_back(box);
    }
    propsDirty_ = true;
}

void WorldGrid::clearProps() {
    props_.clear();
    propColliders_.clear();
    propsDirty_ = true;
}

void WorldGrid::unloadFarRegions(int centerRegX, int centerRegZ, int keepRadius) {
    const int chunksPerReg = cfg::REGION_SIZE / Chunk::SIZE;
    const int minCx = (centerRegX - keepRadius) * chunksPerReg;
    const int maxCx = (centerRegX + keepRadius + 1) * chunksPerReg - 1;
    const int minCz = (centerRegZ - keepRadius) * chunksPerReg;
    const int maxCz = (centerRegZ + keepRadius + 1) * chunksPerReg - 1;

    for (auto it = chunkMap_.begin(); it != chunkMap_.end();) {
        if (it->first.cx < minCx || it->first.cx > maxCx ||
            it->first.cz < minCz || it->first.cz > maxCz) {
            it = chunkMap_.erase(it);
        } else {
            ++it;
        }
    }

    size_t oldPropCount = props_.size();
    props_.erase(
        std::remove_if(props_.begin(), props_.end(),
                       [&](const PropBox& p) {
                           return std::abs(p.regX - centerRegX) > keepRadius ||
                                  std::abs(p.regZ - centerRegZ) > keepRadius;
                       }),
        props_.end());

    if (props_.size() != oldPropCount) {
        propColliders_.clear();
        for (const auto& p : props_) {
            if (p.collidable) propColliders_.push_back(p.box);
        }
        propsDirty_ = true;
    }
}

} // namespace mega
