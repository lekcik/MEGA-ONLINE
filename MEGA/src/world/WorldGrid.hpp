#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mega {

enum class BlockType : uint8_t {
    Air = 0,
    Concrete = 1,     // Liminal utility wall
    RustPlate = 2,    // Painted metal cabinet
    HazardStripe = 3, // Wooden crate
    Grate = 4,        // Shelving rack dark steel
    TechPanel = 5,    // Worn floor tile
    ReactorGlow = 6,  // Emissive blue-white wall lamp
    DarkSteel = 7,    // Concrete ceiling slab
    BioWall = 8
};

struct Cell {
    BlockType type = BlockType::Air;
    uint8_t lightR = 255;
    uint8_t lightG = 255;
    uint8_t lightB = 255;
    bool halfSlab = false;

    bool isSolid() const {
        return type != BlockType::Air;
    }

    bool isOpaque() const {
        return type != BlockType::Air && !halfSlab;
    }

    bool isEmissive() const {
        return type == BlockType::ReactorGlow;
    }

    float topHeight() const {
        return halfSlab ? 0.5f : 1.0f;
    }
};

struct PropBox {
    AABB box;
    BlockType material = BlockType::HazardStripe;
    bool emissive = false;
    bool stretchUV = false;
    bool collidable = true;
    int regX = 0;
    int regZ = 0;
};

struct ChunkCoord {
    int cx = 0, cy = 0, cz = 0;
    bool operator==(const ChunkCoord& o) const {
        return cx == o.cx && cy == o.cy && cz == o.cz;
    }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const noexcept {
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(c.cx)) * 73856093ULL ^
                     static_cast<uint64_t>(static_cast<uint32_t>(c.cy)) * 19349663ULL ^
                     static_cast<uint64_t>(static_cast<uint32_t>(c.cz)) * 83492791ULL;
        return static_cast<size_t>(h);
    }
};

struct Chunk {
    static constexpr int SIZE = cfg::CHUNK_SIZE;
    static constexpr int VOLUME = SIZE * SIZE * SIZE;

    int cx = 0, cy = 0, cz = 0;
    std::array<Cell, VOLUME> cells{};
    bool dirty = true;

    explicit Chunk(BlockType fill = BlockType::Air) {
        Cell c;
        c.type = fill;
        cells.fill(c);
    }

    int index(int lx, int ly, int lz) const {
        return (ly * SIZE + lz) * SIZE + lx;
    }

    const Cell& at(int lx, int ly, int lz) const {
        return cells[index(lx, ly, lz)];
    }

    Cell& at(int lx, int ly, int lz) {
        return cells[index(lx, ly, lz)];
    }
};

class WorldGrid {
public:
    // Finite grid constructor (or pass 0,0,0 for infinite streaming grid)
    WorldGrid(int chunksX = 0, int chunksY = 0, int chunksZ = 0,
              BlockType defaultFill = BlockType::Air);

    static WorldGrid createInfinite(BlockType solidDefault = BlockType::Concrete) {
        WorldGrid w(0, 1, 0, solidDefault);
        w.infinite_ = true;
        return w;
    }

    bool isInfinite() const { return infinite_; }
    int chunksX() const { return chunksX_; }
    int chunksY() const { return chunksY_; }
    int chunksZ() const { return chunksZ_; }

    int sizeX() const { return chunksX_ * Chunk::SIZE; }
    int sizeY() const { return chunksY_ * Chunk::SIZE; }
    int sizeZ() const { return chunksZ_ * Chunk::SIZE; }

    bool inBounds(int x, int y, int z) const;

    Cell getCell(int x, int y, int z) const;
    BlockType getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockType type, bool halfSlab = false);
    void setLight(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b);

    void fillBox(int x0, int y0, int z0, int x1, int y1, int z1, BlockType type, bool halfSlab = false);

    // 3D Furniture & Wall Fixture Props
    void addProp(const AABB& box, BlockType material, bool collidable = true,
                 bool emissive = false, bool stretchUV = true, int regX = 0, int regZ = 0);
    void clearProps();
    const std::vector<PropBox>& props() const { return props_; }
    const std::vector<AABB>& propColliders() const { return propColliders_; }
    bool propsDirty() const { return propsDirty_; }
    void clearPropsDirty() { propsDirty_ = false; }

    // Unloads chunks and props outside the active region radius around (centerRegX, centerRegZ)
    void unloadFarRegions(int centerRegX, int centerRegZ, int keepRadius);

    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>& chunkMap() { return chunkMap_; }
    const std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>& chunkMap() const { return chunkMap_; }

    static int floorDiv(int a, int b) {
        int d = a / b;
        int r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) --d;
        return d;
    }

    static int floorMod(int a, int b) {
        int r = a % b;
        if (r < 0) r += b;
        return r;
    }

private:
    int chunksX_, chunksY_, chunksZ_;
    bool infinite_ = false;
    BlockType defaultFill_ = BlockType::Air;
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunkMap_;
    std::vector<PropBox> props_;
    std::vector<AABB> propColliders_;
    bool propsDirty_ = true;

    Chunk& getOrCreateChunk(int cx, int cy, int cz);
    void markNeighborDirty(int cx, int cy, int cz);
};

} // namespace mega
