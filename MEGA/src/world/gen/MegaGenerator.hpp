#pragma once

#include "core/Config.hpp"
#include "core/LootTable.hpp"
#include "core/Math3D.hpp"
#include "world/Biome.hpp"
#include "world/WorldGrid.hpp"
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace mega {

struct PointLight3D {
    Vec3 pos;
    Vec3 color{0.72f, 0.88f, 1.0f};
    float radius = 18.0f;
    float intensity = 1.45f;
    int regX = 0;
    int regZ = 0;
};

struct CorridorSegment {
    int x0, z0, x1, z1;
    int width = 3;         // 2..4 meters
    bool alongX = false;
    bool isLoopReturn = false; // True for L-shaped ring/donut return corridor legs
    int connections = 0;   // Base 1..3 connections (+1 when a ring loop reconnects)
    int targetConnections = 2;
    int regX = 0;
    int regZ = 0;
};

struct LockerPlacement {
    Vec3 hingePos;
    float closedYaw = 0.0f;
    float openSign = 1.0f;
    float width = 0.95f;
    float height = 2.05f;
    AABB interiorBox;
    int regX = 0;
    int regZ = 0;
};

// Metal corridor cabinet with interior shelves, double doors (2 handles) & 1 shared lock
struct DoubleCabinetPlacement {
    Vec3 leftHingePos;
    float leftClosedYaw = 0.0f;
    float leftOpenSign = 1.0f;
    Vec3 rightHingePos;
    float rightClosedYaw = 0.0f;
    float rightOpenSign = -1.0f;
    float leafWidth = 0.56f;
    float height = 1.96f;
    bool isRare = false; // Rare reinforced cabinet in 3x4 dead-end rooms
    int regX = 0;
    int regZ = 0;
};

// Hinged metal door separating a 3x4 dead-end room from its corridor
struct RoomDoorPlacement {
    Vec3 hingePos;
    float closedYaw = 0.0f;
    float openSign = 1.0f;
    float width = 1.0f;
    float height = 2.0f;
    int regX = 0;
    int regZ = 0;
};

// 3D interior clearance volume where loot items can spawn, plus optional adjacent floor zone beside storage
struct LootSurface {
    LootSource source = LootSource::Crate;
    AABB surfaceBox;        // min.y = shelf floor, max.y = exact vertical clearance below next shelf/ceiling
    bool hasAdjacentFloor = false;
    AABB adjacentFloorBox{}; // Floor clearance zone right beside storage (ideal for tall brooms/mops!)
    int regX = 0;
    int regZ = 0;
};

struct MegaResult {
    WorldGrid world;
    uint64_t seed = 0x4D4547410001ULL;
    Vec3 spawnPos{58.0f, 1.0f, 58.5f};
    Vec3 exitPos{120.0f, 1.0f, 120.0f};
    std::vector<CorridorSegment> corridors;
    std::vector<PointLight3D> pointLights;
    std::vector<LockerPlacement> lockers;
    std::vector<DoubleCabinetPlacement> cabinets;
    std::vector<RoomDoorPlacement> roomDoors;
    std::vector<LootSurface> lootSurfaces;
    std::set<std::pair<int, int>> loadedRegions;
    int currentRegX = 0;
    int currentRegZ = 0;
    int ringLoopsCreated = 0;
    int deadEndRoomsCreated = 0;
    double generationMs = 0.0;

    MegaResult() : world(WorldGrid::createInfinite(BlockType::Concrete)) {}
};

class MegaGenerator {
public:
    // 11x11 regions of 128m = 1408x1408m (~1.98 km^2), centered at (64, 64)
    static constexpr int BIOME_MIN_REG = -5;
    static constexpr int BIOME_MAX_REG =  5;
    static constexpr float BIOME_CENTER_X = 64.0f;
    static constexpr float BIOME_CENTER_Z = 64.0f;
    static constexpr float BIOME_HALF_EXTENT = 704.0f; // 1408 / 2
    // Robots spawn ONLY within 500m of the biome edge (max(|x-64|, |z-64|) >= 204m)
    static constexpr float ROBOT_EDGE_BAND = 500.0f;
    static constexpr float ROBOT_MIN_DIST_FROM_CENTER = BIOME_HALF_EXTENT - ROBOT_EDGE_BAND;

    static bool isInsideUtilityBiome(int regX, int regZ) {
        return regX >= BIOME_MIN_REG && regX <= BIOME_MAX_REG &&
               regZ >= BIOME_MIN_REG && regZ <= BIOME_MAX_REG;
    }

    static bool isRobotSpawnAllowed(float x, float z) {
        float dx = std::abs(x - BIOME_CENTER_X);
        float dz = std::abs(z - BIOME_CENTER_Z);
        float chebyshev = std::max(dx, dz);
        return chebyshev >= ROBOT_MIN_DIST_FROM_CENTER && chebyshev <= BIOME_HALF_EXTENT;
    }

    static MegaResult generate(uint64_t seed, const GameSettings& settings = GameSettings{});

    // Streams new regions into the world as the player moves, and unloads far regions.
    static bool updateInfiniteStream(MegaResult& state, const Vec3& playerPos,
                                     const GameSettings& settings);
    static bool updateInfiniteStreamAtRegion(MegaResult& state, int rx, int rz,
                                             const GameSettings& settings);

private:
    static void generateRegion(MegaResult& state, int regX, int regZ,
                               const GameSettings& settings);
};

} // namespace mega
