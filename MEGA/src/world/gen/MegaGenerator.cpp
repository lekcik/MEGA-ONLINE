#include "world/gen/MegaGenerator.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace mega {

namespace {

constexpr int FLOOR_Y = 1;
constexpr int CEIL_H  = 4;

uint64_t regionSeed(uint64_t baseSeed, int rx, int rz) {
    uint64_t hx = static_cast<uint64_t>(static_cast<uint32_t>(rx)) * 0x9E3779B97F4A7C15ULL;
    uint64_t hz = static_cast<uint64_t>(static_cast<uint32_t>(rz)) * 0xBF58476D1CE4E5B9ULL;
    return baseSeed ^ hx ^ (hz << 1);
}

bool canPlaceProp(const WorldGrid& w, const AABB& box, const Vec3& spawnPos, float clearanceMargin = 0.15f) {
    float cx = 0.5f * (box.min.x + box.max.x);
    float cz = 0.5f * (box.min.z + box.max.z);
    if (std::abs(cx - spawnPos.x) < 2.6f && std::abs(cz - spawnPos.z) < 2.6f) return false;
    // Keep starter room (x=54..61, z=54..62) and its doorway into spineNS1 (x=62..64, z=57..60) clear
    if (cx >= 53.5f && cx <= 64.5f && cz >= 53.5f && cz <= 62.5f) return false;

    AABB expanded{
        {box.min.x - clearanceMargin, box.min.y, box.min.z - clearanceMargin},
        {box.max.x + clearanceMargin, box.max.y, box.max.z + clearanceMargin}
    };
    for (const auto& c : w.propColliders()) {
        if (expanded.intersects(c)) return false;
    }

    int x0 = static_cast<int>(std::floor(box.min.x + 0.05f));
    int x1 = static_cast<int>(std::floor(box.max.x - 0.05f));
    int z0 = static_cast<int>(std::floor(box.min.z + 0.05f));
    int z1 = static_cast<int>(std::floor(box.max.z - 0.05f));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (w.getBlock(x, FLOOR_Y, z) != BlockType::Air) return false;
            if (!w.getCell(x, FLOOR_Y - 1, z).isSolid()) return false;
        }
    }
    return true;
}

void placeWallLamp(WorldGrid& w, std::vector<PointLight3D>& lights,
                   float wallX, float wallZ, int normalX, int normalZ,
                   int regX, int regZ, const GameSettings& settings) {
    int bx = static_cast<int>(std::floor(wallX - normalX * 0.25f));
    int bz = static_cast<int>(std::floor(wallZ - normalZ * 0.25f));
    int fx = static_cast<int>(std::floor(wallX + normalX * 0.35f));
    int fz = static_cast<int>(std::floor(wallZ + normalZ * 0.35f));
    if (w.getBlock(bx, 3, bz) == BlockType::Air) return;
    if (w.getBlock(fx, 3, fz) != BlockType::Air) return;

    Vec3 lightPos{
        wallX + normalX * 0.42f,
        static_cast<float>(FLOOR_Y) + 2.22f,
        wallZ + normalZ * 0.42f
    };
    for (const auto& l : lights) {
        if (lengthSq(l.pos - lightPos) < 7.5f * 7.5f) return;
    }

    float y0 = static_cast<float>(FLOOR_Y) + 2.12f;
    float y1 = y0 + 0.16f;
    float halfLen = 0.34f;

    if (normalX != 0) {
        float xWall = wallX;
        float xOut1 = wallX + normalX * 0.05f;
        float xOut2 = wallX + normalX * 0.12f;
        w.addProp({{std::min(xWall, xOut1), y0 - 0.03f, wallZ - halfLen - 0.04f},
                   {std::max(xWall, xOut1), y1 + 0.03f, wallZ + halfLen + 0.04f}},
                  BlockType::Grate, false, false, true, regX, regZ);
        w.addProp({{std::min(xOut1, xOut2), y0, wallZ - halfLen},
                   {std::max(xOut1, xOut2), y1, wallZ + halfLen}},
                  BlockType::ReactorGlow, false, true, true, regX, regZ);
    } else {
        float zWall = wallZ;
        float zOut1 = wallZ + normalZ * 0.05f;
        float zOut2 = wallZ + normalZ * 0.12f;
        w.addProp({{wallX - halfLen - 0.04f, y0 - 0.03f, std::min(zWall, zOut1)},
                   {wallX + halfLen + 0.04f, y1 + 0.03f, std::max(zWall, zOut1)}},
                  BlockType::Grate, false, false, true, regX, regZ);
        w.addProp({{wallX - halfLen, y0, std::min(zOut1, zOut2)},
                   {wallX + halfLen, y1, std::max(zOut1, zOut2)}},
                  BlockType::ReactorGlow, false, true, true, regX, regZ);
    }

    PointLight3D pl;
    pl.pos = lightPos;
    pl.color = {settings.lampColorR, settings.lampColorG, settings.lampColorB};
    pl.radius = settings.lampRadius;
    pl.intensity = settings.lampIntensity;
    pl.regX = regX;
    pl.regZ = regZ;
    lights.push_back(pl);
}

// Find a clear floor zone right beside a wall-mounted storage prop (for tall brooms/mops!)
bool findAdjacentFloorZone(const WorldGrid& w, const AABB& propOuter,
                           int normalX, int normalZ, const Vec3& spawnPos,
                           AABB& outFloorBox) {
    float y0 = static_cast<float>(FLOOR_Y);
    float zoneW = 0.34f;
    float depthX = propOuter.max.x - propOuter.min.x;
    float depthZ = propOuter.max.z - propOuter.min.z;

    for (float side : {1.0f, -1.0f}) {
        AABB cand;
        if (normalX != 0) {
            // Wall runs along Z -> adjacent spots are at +Z and -Z of propOuter
            float zA = (side > 0.0f) ? (propOuter.max.z + 0.04f) : (propOuter.min.z - 0.04f - zoneW);
            cand = {{propOuter.min.x + 0.02f, y0, zA},
                    {propOuter.min.x + std::min(depthX, 0.42f), y0 + 2.35f, zA + zoneW}};
        } else {
            // Wall runs along X -> adjacent spots are at +X and -X of propOuter
            float xA = (side > 0.0f) ? (propOuter.max.x + 0.04f) : (propOuter.min.x - 0.04f - zoneW);
            cand = {{xA, y0, propOuter.min.z + 0.02f},
                    {xA + zoneW, y0 + 2.35f, propOuter.min.z + std::min(depthZ, 0.42f)}};
        }
        (void)normalZ;
        if (canPlaceProp(w, cand, spawnPos, 0.04f)) {
            outFloorBox = cand;
            return true;
        }
    }
    return false;
}

void placeWoodenCrates(WorldGrid& w, std::vector<LootSurface>& lootSurfaces,
                       float wallX, float wallZ, int normalX, int normalZ,
                       int regX, int regZ, const Vec3& spawnPos, Rng& rng) {
    float depth = 0.74f + rng.nextFloat() * 0.14f;
    float width = 0.78f + rng.nextFloat() * 0.22f;
    float height = 0.76f + rng.nextFloat() * 0.14f;
    float y0 = static_cast<float>(FLOOR_Y);
    float ceilY = static_cast<float>(FLOOR_Y + CEIL_H) - 0.15f;

    AABB baseBox;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.02f;
        float xB = wallX + normalX * (0.02f + depth);
        baseBox = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
                   {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.02f;
        float zB = wallZ + normalZ * (0.02f + depth);
        baseBox = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
                   {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!canPlaceProp(w, baseBox, spawnPos)) return;
    w.addProp(baseBox, BlockType::HazardStripe, true, false, true, regX, regZ);

    AABB topSurface = baseBox;
    if (rng.chance(0.55f)) {
        float shrink = 0.08f;
        float h2 = 0.64f + rng.nextFloat() * 0.12f;
        AABB topBox{
            {baseBox.min.x + shrink, baseBox.max.y, baseBox.min.z + shrink},
            {baseBox.max.x - shrink, baseBox.max.y + h2, baseBox.max.z - shrink}
        };
        w.addProp(topBox, BlockType::HazardStripe, true, false, true, regX, regZ);
        topSurface = topBox;
    }
    AABB adjBox{};
    bool hasAdj = findAdjacentFloorZone(w, baseBox, normalX, normalZ, spawnPos, adjBox);
    lootSurfaces.push_back({
        LootSource::Crate,
        {{topSurface.min.x + 0.05f, topSurface.max.y, topSurface.min.z + 0.05f},
         {topSurface.max.x - 0.05f, ceilY, topSurface.max.z - 0.05f}},
        hasAdj, adjBox, regX, regZ
    });

    if (rng.chance(0.45f)) {
        float sideW = 0.70f;
        float sideH = 0.68f;
        AABB sideBox = baseBox;
        if (normalX != 0) {
            sideBox.min.z = baseBox.max.z + 0.04f;
            sideBox.max.z = sideBox.min.z + sideW;
        } else {
            sideBox.min.x = baseBox.max.x + 0.04f;
            sideBox.max.x = sideBox.min.x + sideW;
        }
        sideBox.max.y = y0 + sideH;
        if (canPlaceProp(w, sideBox, spawnPos, 0.02f)) {
            w.addProp(sideBox, BlockType::HazardStripe, true, false, true, regX, regZ);
            AABB sideAdj{};
            bool hasSideAdj = findAdjacentFloorZone(w, sideBox, normalX, normalZ, spawnPos, sideAdj);
            lootSurfaces.push_back({
                LootSource::Crate,
                {{sideBox.min.x + 0.05f, sideBox.max.y, sideBox.min.z + 0.05f},
                 {sideBox.max.x - 0.05f, ceilY, sideBox.max.z - 0.05f}},
                hasSideAdj, sideAdj, regX, regZ
            });
        }
    }
}

// Hollow Metal Storage Cabinet with interior shelves, double doors (2 handles) & 1 shared lock (always locked!)
// Rare variant (isRare = true) spawns in 3x4 dead-end rooms with DarkSteel chassis, 3 shelves + tall broom/mop bay!
void placeMetalCabinet(WorldGrid& w, std::vector<DoubleCabinetPlacement>& cabinets,
                       std::vector<LootSurface>& lootSurfaces,
                       float wallX, float wallZ, int normalX, int normalZ,
                       int regX, int regZ, const Vec3& spawnPos, Rng& rng,
                       bool isRare = false) {
    float depth = isRare ? 0.68f : 0.58f;
    float width = isRare ? 1.56f : (rng.chance(0.55f) ? 1.16f : 1.44f);
    float height = isRare ? 2.22f : 2.08f;
    float y0 = static_cast<float>(FLOOR_Y);
    float t = 0.055f;
    BlockType bodyMat = isRare ? BlockType::DarkSteel : BlockType::RustPlate;

    AABB outer;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.02f;
        float xB = wallX + normalX * (0.02f + depth);
        outer = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
                 {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.02f;
        float zB = wallZ + normalZ * (0.02f + depth);
        outer = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
                 {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!isRare && !canPlaceProp(w, outer, spawnPos)) return;

    // Top & bottom plates
    w.addProp({{outer.min.x, outer.max.y - t, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}},
              bodyMat, true, false, true, regX, regZ);
    w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, y0 + t, outer.max.z}},
              bodyMat, true, false, true, regX, regZ);

    AABB inner{{outer.min.x + t, y0 + t, outer.min.z + t},
               {outer.max.x - t, outer.max.y - t, outer.max.z - t}};
    // Extra clearance at front door face so items never clip through closed cabinet doors
    AABB safeInner = inner;
    if (normalX > 0) safeInner.max.x -= 0.04f;
    else if (normalX < 0) safeInner.min.x += 0.04f;
    else if (normalZ > 0) safeInner.max.z -= 0.04f;
    else safeInner.min.z += 0.04f;

    AABB adjBox{};
    bool hasAdj = findAdjacentFloorZone(w, outer, normalX, normalZ, spawnPos, adjBox);

    if (isRare) {
        // Rare cabinet: left half has 3 shelves for small/medium loot, right half is a tall utility bay for Brooms/Mops!
        float midX = 0.5f * (inner.min.x + inner.max.x);
        float midZ = 0.5f * (inner.min.z + inner.max.z);
        bool splitAlongZ = (normalX != 0);

        // Center divider wall
        if (splitAlongZ) {
            w.addProp({{inner.min.x, inner.min.y, midZ - 0.02f}, {inner.max.x, inner.max.y, midZ + 0.02f}},
                      BlockType::Grate, true, false, true, regX, regZ);
        } else {
            w.addProp({{midX - 0.02f, inner.min.y, inner.min.z}, {midX + 0.02f, inner.max.y, inner.max.z}},
                      BlockType::Grate, true, false, true, regX, regZ);
        }

        // 3 shelves on half A with exact vertical clearance to next shelf / cabinet roof
        const float shelfOffsets[3] = {0.48f, 0.98f, 1.48f};
        for (int s = 0; s < 3; ++s) {
            float sy = y0 + shelfOffsets[s];
            float ceilingY = (s < 2) ? (y0 + shelfOffsets[s + 1] - 0.01f) : (inner.max.y - 0.01f);
            AABB sBox = splitAlongZ
                            ? AABB{{inner.min.x, sy, inner.min.z}, {inner.max.x, sy + 0.045f, midZ - 0.02f}}
                            : AABB{{inner.min.x, sy, inner.min.z}, {midX - 0.02f, sy + 0.045f, inner.max.z}};
            w.addProp(sBox, BlockType::Grate, true, false, true, regX, regZ);
            AABB vol = splitAlongZ
                           ? AABB{{safeInner.min.x + 0.02f, sy + 0.045f, safeInner.min.z + 0.02f},
                                  {safeInner.max.x - 0.02f, ceilingY, midZ - 0.04f}}
                           : AABB{{safeInner.min.x + 0.02f, sy + 0.045f, safeInner.min.z + 0.02f},
                                  {midX - 0.04f, ceilingY, safeInner.max.z - 0.02f}};
            lootSurfaces.push_back({LootSource::RareCabinet, vol, hasAdj && (s == 0), adjBox, regX, regZ});
        }
        // Bottom floor on half B (tall 2.10m utility bay for standing Broom/Mop + extra loot)
        AABB bayBox = splitAlongZ
                          ? AABB{{safeInner.min.x + 0.02f, y0 + t, midZ + 0.04f},
                                 {safeInner.max.x - 0.02f, inner.max.y - 0.02f, safeInner.max.z - 0.02f}}
                          : AABB{{midX + 0.04f, y0 + t, safeInner.min.z + 0.02f},
                                 {safeInner.max.x - 0.02f, inner.max.y - 0.02f, safeInner.max.z - 0.02f}};
        lootSurfaces.push_back({LootSource::RareCabinet, bayBox, hasAdj, adjBox, regX, regZ});
    } else {
        // Standard metal cabinet: 3 full-width interior shelves with exact height clearance (~0.45m each)
        const float shelfOffsets[3] = {0.52f, 1.04f, 1.54f};
        for (int s = 0; s < 3; ++s) {
            float sy = y0 + shelfOffsets[s];
            float ceilingY = (s < 2) ? (y0 + shelfOffsets[s + 1] - 0.01f) : (inner.max.y - 0.01f);
            w.addProp({{inner.min.x, sy, inner.min.z}, {inner.max.x, sy + 0.045f, inner.max.z}},
                      BlockType::Grate, true, false, true, regX, regZ);
            lootSurfaces.push_back({
                LootSource::CabinetShelf,
                {{safeInner.min.x + 0.02f, sy + 0.045f, safeInner.min.z + 0.02f},
                 {safeInner.max.x - 0.02f, ceilingY, safeInner.max.z - 0.02f}},
                hasAdj && (s == 0), adjBox, regX, regZ
            });
        }
    }

    DoubleCabinetPlacement cp;
    cp.leafWidth = (width - t * 2.0f) * 0.5f - 0.005f;
    cp.height = height - t * 2.0f;
    cp.isRare = isRare;
    cp.regX = regX;
    cp.regZ = regZ;

    if (normalX > 0) { // Back at min.x, opens toward +X at max.x
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.min.x + t, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.min.z + t}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.max.z - t}, {outer.max.x, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        cp.leftHingePos  = {outer.max.x, y0 + t, outer.min.z + t};
        cp.leftClosedYaw = -PI * 0.5f;
        cp.leftOpenSign  = 1.0f;
        cp.rightHingePos  = {outer.max.x, y0 + t, outer.max.z - t};
        cp.rightClosedYaw = PI * 0.5f;
        cp.rightOpenSign  = -1.0f;
    } else if (normalX < 0) { // Back at max.x, opens toward -X at min.x
        w.addProp({{outer.max.x - t, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.min.z + t}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.max.z - t}, {outer.max.x, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        cp.leftHingePos  = {outer.min.x, y0 + t, outer.max.z - t};
        cp.leftClosedYaw = PI * 0.5f;
        cp.leftOpenSign  = 1.0f;
        cp.rightHingePos  = {outer.min.x, y0 + t, outer.min.z + t};
        cp.rightClosedYaw = -PI * 0.5f;
        cp.rightOpenSign  = -1.0f;
    } else if (normalZ > 0) { // Back at min.z, opens toward +Z at max.z
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.min.z + t}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.min.x + t, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.max.x - t, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        cp.leftHingePos  = {outer.min.x + t, y0 + t, outer.max.z};
        cp.leftClosedYaw = 0.0f;
        cp.leftOpenSign  = -1.0f;
        cp.rightHingePos  = {outer.max.x - t, y0 + t, outer.max.z};
        cp.rightClosedYaw = PI;
        cp.rightOpenSign  = 1.0f;
    } else { // Back at max.z, opens toward -Z at min.z
        w.addProp({{outer.min.x, y0, outer.max.z - t}, {outer.max.x, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.min.x + t, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        w.addProp({{outer.max.x - t, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}}, bodyMat, true, false, true, regX, regZ);
        cp.leftHingePos  = {outer.max.x - t, y0 + t, outer.min.z};
        cp.leftClosedYaw = PI;
        cp.leftOpenSign  = -1.0f;
        cp.rightHingePos  = {outer.min.x + t, y0 + t, outer.min.z};
        cp.rightClosedYaw = 0.0f;
        cp.rightOpenSign  = 1.0f;
    }
    cabinets.push_back(cp);
}

void placeShelvingRack(WorldGrid& w, std::vector<LootSurface>& lootSurfaces,
                       float wallX, float wallZ, int normalX, int normalZ,
                       int regX, int regZ, const Vec3& spawnPos, Rng& rng) {
    float depth = 0.54f;
    float width = 1.55f;
    float height = 2.12f;
    float y0 = static_cast<float>(FLOOR_Y);

    AABB bounds;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.02f;
        float xB = wallX + normalX * (0.02f + depth);
        bounds = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
                  {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.02f;
        float zB = wallZ + normalZ * (0.02f + depth);
        bounds = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
                  {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!canPlaceProp(w, bounds, spawnPos)) return;

    const float postT = 0.06f;
    float xs[2] = {bounds.min.x, bounds.max.x - postT};
    float zs[2] = {bounds.min.z, bounds.max.z - postT};
    for (int ix = 0; ix < 2; ++ix) {
        for (int iz = 0; iz < 2; ++iz) {
            w.addProp({{xs[ix], y0, zs[iz]},
                       {xs[ix] + postT, y0 + height, zs[iz] + postT}},
                      BlockType::Grate, false, false, true, regX, regZ);
        }
    }

    AABB rackAdj{};
    bool hasRackAdj = findAdjacentFloorZone(w, bounds, normalX, normalZ, spawnPos, rackAdj);

    const float shelfHeights[4] = {0.18f, 0.78f, 1.38f, 1.98f};
    for (int s = 0; s < 4; ++s) {
        float sy = y0 + shelfHeights[s];
        w.addProp({{bounds.min.x, sy, bounds.min.z},
                   {bounds.max.x, sy + 0.05f, bounds.max.z}},
                  BlockType::Grate, true, false, true, regX, regZ);

        if (s < 3 && rng.chance(0.32f)) {
            float boxH = 0.34f + rng.nextFloat() * 0.12f;
            float marginX = (bounds.max.x - bounds.min.x) * 0.14f;
            float marginZ = (bounds.max.z - bounds.min.z) * 0.14f;
            w.addProp({{bounds.min.x + marginX, sy + 0.05f, bounds.min.z + marginZ},
                       {bounds.max.x - marginX, sy + 0.05f + boxH, bounds.max.z - marginZ}},
                      BlockType::HazardStripe, false, false, true, regX, regZ);
        } else if (s < 3) {
            float nextShelfY = y0 + shelfHeights[s + 1] - 0.01f;
            lootSurfaces.push_back({
                LootSource::ShelvingRack,
                {{bounds.min.x + postT + 0.02f, sy + 0.05f, bounds.min.z + postT + 0.02f},
                 {bounds.max.x - postT - 0.02f, nextShelfY, bounds.max.z - postT - 0.02f}},
                hasRackAdj && (s == 0), rackAdj, regX, regZ
            });
        }
    }

    w.addProp({{bounds.min.x + 0.02f, y0, bounds.min.z + 0.02f},
               {bounds.max.x - 0.02f, y0 + 0.04f, bounds.max.z - 0.02f}},
              BlockType::Grate, true, false, true, regX, regZ);
}

void fillCorridorShell(WorldGrid& w, const CorridorSegment& seg) {
    int minX = std::min(seg.x0, seg.x1);
    int maxX = std::max(seg.x0, seg.x1);
    int minZ = std::min(seg.z0, seg.z1);
    int maxZ = std::max(seg.z0, seg.z1);

    if (seg.alongX) {
        int zStart = minZ;
        int zEnd = minZ + seg.width - 1;
        w.fillBox(minX - 1, FLOOR_Y - 1, zStart - 1, maxX + 1, FLOOR_Y + CEIL_H, zEnd + 1, BlockType::Concrete);
    } else {
        int xStart = minX;
        int xEnd = minX + seg.width - 1;
        w.fillBox(xStart - 1, FLOOR_Y - 1, minZ - 1, xEnd + 1, FLOOR_Y + CEIL_H, maxZ + 1, BlockType::Concrete);
    }
}

void carveCorridor(WorldGrid& w, const CorridorSegment& seg) {
    int minX = std::min(seg.x0, seg.x1);
    int maxX = std::max(seg.x0, seg.x1);
    int minZ = std::min(seg.z0, seg.z1);
    int maxZ = std::max(seg.z0, seg.z1);

    if (seg.alongX) {
        int zStart = minZ;
        int zEnd = minZ + seg.width - 1;
        w.fillBox(minX, FLOOR_Y, zStart, maxX, FLOOR_Y + CEIL_H - 1, zEnd, BlockType::Air);
        w.fillBox(minX, FLOOR_Y - 1, zStart, maxX, FLOOR_Y - 1, zEnd, BlockType::TechPanel);
        w.fillBox(minX, FLOOR_Y + CEIL_H, zStart, maxX, FLOOR_Y + CEIL_H, zEnd, BlockType::DarkSteel);
    } else {
        int xStart = minX;
        int xEnd = minX + seg.width - 1;
        w.fillBox(xStart, FLOOR_Y, minZ, xEnd, FLOOR_Y + CEIL_H - 1, maxZ, BlockType::Air);
        w.fillBox(xStart, FLOOR_Y - 1, minZ, xEnd, FLOOR_Y - 1, maxZ, BlockType::TechPanel);
        w.fillBox(xStart, FLOOR_Y + CEIL_H, minZ, xEnd, FLOOR_Y + CEIL_H, maxZ, BlockType::DarkSteel);
    }
}

// Hideable Hollow Locker (player can walk inside & close door!)
void placeHideableLocker(WorldGrid& w, std::vector<LockerPlacement>& lockers,
                         std::vector<LootSurface>& lootSurfaces,
                         float wallX, float wallZ, int normalX, int normalZ,
                         int regX, int regZ, const Vec3& spawnPos) {
    float depth = 0.88f;
    float width = 1.02f;
    float height = 2.12f;
    float y0 = static_cast<float>(FLOOR_Y);
    float t = 0.06f;

    AABB outer;
    if (normalX != 0) {
        float xA = wallX + normalX * 0.01f;
        float xB = wallX + normalX * (0.01f + depth);
        outer = {{std::min(xA, xB), y0, wallZ - width * 0.5f},
                 {std::max(xA, xB), y0 + height, wallZ + width * 0.5f}};
    } else {
        float zA = wallZ + normalZ * 0.01f;
        float zB = wallZ + normalZ * (0.01f + depth);
        outer = {{wallX - width * 0.5f, y0, std::min(zA, zB)},
                 {wallX + width * 0.5f, y0 + height, std::max(zA, zB)}};
    }

    if (!canPlaceProp(w, outer, spawnPos, 0.25f)) return;

    // Top & floor plates
    w.addProp({{outer.min.x, outer.max.y - t, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}},
              BlockType::RustPlate, false, false, true, regX, regZ);
    w.addProp({{outer.min.x, outer.min.y, outer.min.z}, {outer.max.x, outer.min.y + 0.03f, outer.max.z}},
              BlockType::RustPlate, false, false, true, regX, regZ);

    LockerPlacement lp;
    lp.width = width - t * 2.0f;
    lp.height = height - t * 1.5f;
    lp.regX = regX;
    lp.regZ = regZ;
    lp.interiorBox = {{outer.min.x + t, y0, outer.min.z + t},
                      {outer.max.x - t, y0 + lp.height, outer.max.z - t}};

    if (normalX > 0) { // Back at min.x, open at max.x
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.min.x + t, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.min.z + t}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.max.z - t}, {outer.max.x, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        lp.hingePos = {outer.max.x, y0 + 0.03f, outer.min.z + t};
        lp.closedYaw = -PI * 0.5f; // +X rotated by -PI/2 points along +Z
        lp.openSign = 1.0f;
    } else if (normalX < 0) { // Back at max.x, open at min.x
        w.addProp({{outer.max.x - t, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.min.z + t}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.max.z - t}, {outer.max.x, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        lp.hingePos = {outer.min.x, y0 + 0.03f, outer.max.z - t};
        lp.closedYaw = PI * 0.5f;
        lp.openSign = 1.0f;
    } else if (normalZ > 0) { // Back at min.z, open at max.z
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.min.z + t}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.min.x + t, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.max.x - t, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        lp.hingePos = {outer.min.x + t, y0 + 0.03f, outer.max.z};
        lp.closedYaw = 0.0f;
        lp.openSign = -1.0f;
    } else { // Back at max.z, open at min.z
        w.addProp({{outer.min.x, y0, outer.max.z - t}, {outer.max.x, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.min.x, y0, outer.min.z}, {outer.min.x + t, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        w.addProp({{outer.max.x - t, y0, outer.min.z}, {outer.max.x, outer.max.y, outer.max.z}}, BlockType::RustPlate, true, false, true, regX, regZ);
        lp.hingePos = {outer.max.x - t, y0 + 0.03f, outer.min.z};
        lp.closedYaw = PI;
        lp.openSign = -1.0f;
    }
    lockers.push_back(lp);
    AABB lockAdj{};
    bool hasLockAdj = findAdjacentFloorZone(w, outer, normalX, normalZ, spawnPos, lockAdj);
    lootSurfaces.push_back({
        LootSource::Locker,
        {{lp.interiorBox.min.x + 0.04f, y0 + 0.03f, lp.interiorBox.min.z + 0.04f},
         {lp.interiorBox.max.x - 0.04f, lp.interiorBox.max.y - 0.04f, lp.interiorBox.max.z - 0.04f}},
        hasLockAdj, lockAdj, regX, regZ
    });
}

void populateCorridor(WorldGrid& w, const CorridorSegment& seg,
                      std::vector<PointLight3D>& lights,
                      std::vector<LockerPlacement>& lockers,
                      std::vector<DoubleCabinetPlacement>& cabinets,
                      std::vector<LootSurface>& lootSurfaces,
                      const Vec3& spawnPos, const GameSettings& settings, Rng& rng) {
    int minX = std::min(seg.x0, seg.x1);
    int maxX = std::max(seg.x0, seg.x1);
    int minZ = std::min(seg.z0, seg.z1);
    int maxZ = std::max(seg.z0, seg.z1);

    if (seg.alongX) {
        float zNorthWall = static_cast<float>(minZ);
        float zSouthWall = static_cast<float>(minZ + seg.width);

        int lampStep = rng.rangeInt(12, 17);
        bool side = rng.chance(0.5f);
        for (int x = minX + 4; x <= maxX - 4; x += lampStep) {
            if (side) placeWallLamp(w, lights, x + 0.5f, zNorthWall, 0, 1, seg.regX, seg.regZ, settings);
            else      placeWallLamp(w, lights, x + 0.5f, zSouthWall, 0, -1, seg.regX, seg.regZ, settings);
            if (rng.chance(0.35f)) side = !side;
        }

        int step = (seg.width >= 3) ? rng.rangeInt(5, 8) : rng.rangeInt(9, 14);
        for (int x = minX + 3; x <= maxX - 3; x += step) {
            bool north = rng.chance(0.5f);
            float wz = north ? zNorthWall : zSouthWall;
            int nz = north ? 1 : -1;
            int wallCheckZ = north ? (minZ - 1) : (minZ + seg.width);
            if (w.getBlock(x, FLOOR_Y, wallCheckZ) == BlockType::Air ||
                w.getBlock(x - 1, FLOOR_Y, wallCheckZ) == BlockType::Air ||
                w.getBlock(x + 1, FLOOR_Y, wallCheckZ) == BlockType::Air) {
                continue;
            }

            int roll = rng.rangeInt(0, 9);
            if (seg.width == 2) {
                if (roll < 5) placeMetalCabinet(w, cabinets, lootSurfaces, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
                else placeShelvingRack(w, lootSurfaces, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
            } else {
                if (roll < 4) placeWoodenCrates(w, lootSurfaces, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
                else if (roll < 7) {
                    if (rng.chance(0.25f)) placeHideableLocker(w, lockers, lootSurfaces, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos);
                    else placeMetalCabinet(w, cabinets, lootSurfaces, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
                } else placeShelvingRack(w, lootSurfaces, x + 0.5f, wz, 0, nz, seg.regX, seg.regZ, spawnPos, rng);
            }
        }
    } else {
        float xWestWall = static_cast<float>(minX);
        float xEastWall = static_cast<float>(minX + seg.width);

        int lampStep = rng.rangeInt(12, 17);
        bool side = rng.chance(0.5f);
        for (int z = minZ + 4; z <= maxZ - 4; z += lampStep) {
            if (side) placeWallLamp(w, lights, xWestWall, z + 0.5f, 1, 0, seg.regX, seg.regZ, settings);
            else      placeWallLamp(w, lights, xEastWall, z + 0.5f, -1, 0, seg.regX, seg.regZ, settings);
            if (rng.chance(0.35f)) side = !side;
        }

        int step = (seg.width >= 3) ? rng.rangeInt(5, 8) : rng.rangeInt(9, 14);
        for (int z = minZ + 3; z <= maxZ - 3; z += step) {
            bool west = rng.chance(0.5f);
            float wx = west ? xWestWall : xEastWall;
            int nx = west ? 1 : -1;
            int wallCheckX = west ? (minX - 1) : (minX + seg.width);
            if (w.getBlock(wallCheckX, FLOOR_Y, z) == BlockType::Air ||
                w.getBlock(wallCheckX, FLOOR_Y, z - 1) == BlockType::Air ||
                w.getBlock(wallCheckX, FLOOR_Y, z + 1) == BlockType::Air) {
                continue;
            }

            int roll = rng.rangeInt(0, 9);
            if (seg.width == 2) {
                if (roll < 5) placeMetalCabinet(w, cabinets, lootSurfaces, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
                else placeShelvingRack(w, lootSurfaces, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
            } else {
                if (roll < 4) placeWoodenCrates(w, lootSurfaces, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
                else if (roll < 7) {
                    if (rng.chance(0.25f)) placeHideableLocker(w, lockers, lootSurfaces, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos);
                    else placeMetalCabinet(w, cabinets, lootSurfaces, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
                } else placeShelvingRack(w, lootSurfaces, wx, z + 0.5f, nx, 0, seg.regX, seg.regZ, spawnPos, rng);
            }
        }
    }
}

// Checks if two 1D intervals [a0, a1] and [b0, b1] overlap with a margin
bool intervalsOverlap(int a0, int a1, int b0, int b1, int margin = 0) {
    return (std::min(a0, a1) - margin <= std::max(b0, b1)) &&
           (std::max(a0, a1) + margin >= std::min(b0, b1));
}

} // namespace

void MegaGenerator::generateRegion(MegaResult& state, int regX, int regZ,
                                   const GameSettings& settings) {
    if (!isInsideUtilityBiome(regX, regZ)) return;
    if (state.loadedRegions.count({regX, regZ}) > 0) return;
    state.loadedRegions.insert({regX, regZ});

    Rng rng(regionSeed(state.seed, regX, regZ));
    const int baseX = regX * cfg::REGION_SIZE;
    const int baseZ = regZ * cfg::REGION_SIZE;

    std::vector<CorridorSegment> regCorridors;

    // 1. Primary North-South transit corridor at local X = 63 (72m long, reaching South border at 128)
    // and connecting to previous region's South portal at local Z = 0 via a 2-segment spine
    CorridorSegment spineNS1;
    spineNS1.x0 = baseX + 63;
    spineNS1.z0 = baseZ + 0;
    spineNS1.x1 = baseX + 63;
    spineNS1.z1 = baseZ + 68;
    spineNS1.width = 3;
    spineNS1.alongX = false;
    spineNS1.connections = 2; // Connects to north region + spineNS2
    spineNS1.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineNS1.regX = regX;
    spineNS1.regZ = regZ;
    regCorridors.push_back(spineNS1);

    CorridorSegment spineNS2;
    spineNS2.x0 = baseX + 63;
    spineNS2.z0 = baseZ + 65;
    spineNS2.x1 = baseX + 63;
    spineNS2.z1 = baseZ + cfg::REGION_SIZE;
    spineNS2.width = 3;
    spineNS2.alongX = false;
    spineNS2.connections = 2; // Connects to spineNS1 + south region
    spineNS2.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineNS2.regX = regX;
    spineNS2.regZ = regZ;
    regCorridors.push_back(spineNS2);

    // 2. Primary West-East transit corridors connecting West border (X=0) and East border (X=128)
    CorridorSegment spineWE1;
    spineWE1.x0 = baseX + 0;
    spineWE1.z0 = baseZ + 63;
    spineWE1.x1 = baseX + 66;
    spineWE1.z1 = baseZ + 63;
    spineWE1.width = 3;
    spineWE1.alongX = true;
    spineWE1.connections = 2; // West border + central junction
    spineWE1.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineWE1.regX = regX;
    spineWE1.regZ = regZ;
    regCorridors.push_back(spineWE1);

    CorridorSegment spineWE2;
    spineWE2.x0 = baseX + 63;
    spineWE2.z0 = baseZ + 63;
    spineWE2.x1 = baseX + cfg::REGION_SIZE;
    spineWE2.z1 = baseZ + 63;
    spineWE2.width = 3;
    spineWE2.alongX = true;
    spineWE2.connections = 2; // Central junction + East border
    spineWE2.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
    spineWE2.regX = regX;
    spineWE2.regZ = regZ;
    regCorridors.push_back(spineWE2);

    struct BranchInfo {
        int parentIdx = -1;
        int tipX = 0;
        int tipZ = 0;
        int dir = 1; // +1 or -1 along branch axis
    };
    std::vector<BranchInfo> branchMeta(regCorridors.size());

    // 3. Grow sparse branching corridors where each corridor connects to ONLY 2..3 others
    for (int attempt = 0; attempt < 48 && regCorridors.size() < 10; ++attempt) {
        int pIdx = rng.rangeInt(0, static_cast<int>(regCorridors.size()) - 1);
        auto& parent = regCorridors[pIdx];
        if (parent.connections >= parent.targetConnections) continue;

        bool childAlongX = !parent.alongX;
        int w = rng.rangeInt(settings.corridorMinWidth, settings.corridorMaxWidth);
        int len = rng.rangeInt(settings.corridorMinLen, settings.corridorMaxLen);
        int dir = rng.chance(0.5f) ? 1 : -1;

        CorridorSegment cand;
        cand.width = w;
        cand.alongX = childAlongX;
        cand.connections = 1;
        cand.targetConnections = rng.rangeInt(settings.minConnections, settings.maxConnections);
        cand.regX = regX;
        cand.regZ = regZ;

        BranchInfo meta;
        meta.parentIdx = pIdx;
        meta.dir = dir;

        if (childAlongX) {
            int pMinZ = std::min(parent.z0, parent.z1) + 6;
            int pMaxZ = std::max(parent.z0, parent.z1) - 6;
            if (pMaxZ <= pMinZ) continue;
            int branchZ = rng.rangeInt(pMinZ, pMaxZ);
            int startX = parent.x0;
            int endX = std::clamp(startX + dir * len, baseX + 10, baseX + cfg::REGION_SIZE - 10);
            if (std::abs(endX - startX) < settings.corridorMinLen) continue;
            cand.x0 = std::min(startX, endX);
            cand.x1 = std::max(startX, endX);
            cand.z0 = cand.z1 = branchZ;
            meta.tipX = endX;
            meta.tipZ = branchZ;
        } else {
            int pMinX = std::min(parent.x0, parent.x1) + 6;
            int pMaxX = std::max(parent.x0, parent.x1) - 6;
            if (pMaxX <= pMinX) continue;
            int branchX = rng.rangeInt(pMinX, pMaxX);
            int startZ = parent.z0;
            int endZ = std::clamp(startZ + dir * len, baseZ + 10, baseZ + cfg::REGION_SIZE - 10);
            if (std::abs(endZ - startZ) < settings.corridorMinLen) continue;
            cand.x0 = cand.x1 = branchX;
            cand.z0 = std::min(startZ, endZ);
            cand.z1 = std::max(startZ, endZ);
            meta.tipX = branchX;
            meta.tipZ = endZ;
        }

        // Validate against all existing corridors AND starter room in region (0, 0):
        if (regX == 0 && regZ == 0 &&
            intervalsOverlap(cand.x0, cand.x1, 53, 62, 2) &&
            intervalsOverlap(cand.z0, cand.z1, 53, 62, 2)) {
            continue;
        }
        bool valid = true;
        for (size_t j = 0; j < regCorridors.size(); ++j) {
            if (static_cast<int>(j) == pIdx) continue;
            const auto& other = regCorridors[j];
            if (other.alongX == cand.alongX) {
                if (cand.alongX) {
                    if (std::abs(cand.z0 - other.z0) < settings.minParallelSpacing &&
                        intervalsOverlap(cand.x0, cand.x1, other.x0, other.x1, 4)) {
                        valid = false;
                        break;
                    }
                } else {
                    if (std::abs(cand.x0 - other.x0) < settings.minParallelSpacing &&
                        intervalsOverlap(cand.z0, cand.z1, other.z0, other.z1, 4)) {
                        valid = false;
                        break;
                    }
                }
            } else {
                bool crossX = cand.alongX
                                  ? intervalsOverlap(cand.x0, cand.x1, other.x0, other.x0 + other.width, 2)
                                  : intervalsOverlap(other.x0, other.x1, cand.x0, cand.x0 + cand.width, 2);
                bool crossZ = cand.alongX
                                  ? intervalsOverlap(other.z0, other.z1, cand.z0, cand.z0 + cand.width, 2)
                                  : intervalsOverlap(cand.z0, cand.z1, other.z0, other.z0 + other.width, 2);
                if (crossX && crossZ) {
                    valid = false;
                    break;
                }
            }
        }

        if (!valid) continue;

        parent.connections++;
        regCorridors.push_back(cand);
        branchMeta.push_back(meta);
    }

    // 4. Dead-end reduction:
    //    - 1/3 of dead ends -> flush L-shaped return corridor loop ("donut/ring") back to parent corridor
    //    - 1/3 of dead ends -> walled 3x4 room with 1x2m doorway + hinged metal door + Rare Reinforced Cabinet
    //    - 1/3 of dead ends -> standard dead end
    struct DeadEndRoom {
        int x0, z0, x1, z1;
        int doorX, doorZ;
        RoomDoorPlacement door;
        float cabWallX, cabWallZ;
        int cabNormalX, cabNormalZ;
    };
    std::vector<DeadEndRoom> deadEndRooms;
    std::vector<CorridorSegment> loopCorridors;

    int deadEndOrdinal = 0;
    size_t origCount = regCorridors.size();
    for (size_t i = 4; i < origCount; ++i) {
        if (regCorridors[i].connections != 1) continue;
        int pIdx = branchMeta[i].parentIdx;
        if (pIdx < 0 || pIdx >= static_cast<int>(origCount)) continue;

        const auto& seg = regCorridors[i];
        const auto& par = regCorridors[pIdx];
        int d = branchMeta[i].dir;
        int mode = deadEndOrdinal % 3;
        deadEndOrdinal++;

        auto hitsStarter = [&](int ax0, int ax1, int az0, int az1) {
            return (regX == 0 && regZ == 0 &&
                    intervalsOverlap(ax0, ax1, 52, 63, 2) &&
                    intervalsOverlap(az0, az1, 52, 63, 2));
        };

        if (mode == 0) {
            // 1/3 -> Build an exact flush L-shaped return loop back to parent corridor ("donut / ring")
            int w = seg.width;
            int loopOffset = rng.rangeInt(14, 22);
            CorridorSegment leg1, leg2;
            leg1.width = w; leg1.regX = regX; leg1.regZ = regZ; leg1.connections = 2; leg1.isLoopReturn = true;
            leg2.width = w; leg2.regX = regX; leg2.regZ = regZ; leg2.connections = 2; leg2.isLoopReturn = true;

            bool built = false;
            if (seg.alongX) {
                // seg spans x=[seg.x0..seg.x1], z=[seg.z0..seg.z0+w-1]; parent spans x=[par.x0..par.x0+par.width-1]
                int pMinZ = std::min(par.z0, par.z1) + 4;
                int pMaxZ = std::max(par.z0, par.z1) - w - 3;
                for (int sign : {1, -1}) {
                    int retZ = seg.z0 + sign * loopOffset;
                    if (retZ < pMinZ || retZ > pMaxZ) continue;
                    // Flush leg1 X coordinate: aligns outer wall exactly with seg's dead-end tip!
                    int leg1X = (d > 0) ? (seg.x1 - (w - 1)) : seg.x0;
                    int envMinX = std::min(leg1X, par.x0);
                    int envMaxX = std::max(leg1X + w - 1, par.x0 + par.width - 1);
                    int envMinZ = std::min(seg.z0, retZ);
                    int envMaxZ = std::max(seg.z0, retZ) + w - 1;
                    if (hitsStarter(envMinX, envMaxX, envMinZ, envMaxZ)) continue;

                    leg1.alongX = false;
                    leg1.x0 = leg1.x1 = leg1X;
                    leg1.z0 = envMinZ;
                    leg1.z1 = envMaxZ;

                    leg2.alongX = true;
                    leg2.z0 = leg2.z1 = retZ;
                    leg2.x0 = envMinX;
                    leg2.x1 = envMaxX;
                    built = true;
                    break;
                }
            } else {
                // seg spans z=[seg.z0..seg.z1], x=[seg.x0..seg.x0+w-1]; parent spans z=[par.z0..par.z0+par.width-1]
                int pMinX = std::min(par.x0, par.x1) + 4;
                int pMaxX = std::max(par.x0, par.x1) - w - 3;
                for (int sign : {1, -1}) {
                    int retX = seg.x0 + sign * loopOffset;
                    if (retX < pMinX || retX > pMaxX) continue;
                    // Flush leg1 Z coordinate: aligns outer wall exactly with seg's dead-end tip!
                    int leg1Z = (d > 0) ? (seg.z1 - (w - 1)) : seg.z0;
                    int envMinX = std::min(seg.x0, retX);
                    int envMaxX = std::max(seg.x0, retX) + w - 1;
                    int envMinZ = std::min(leg1Z, par.z0);
                    int envMaxZ = std::max(leg1Z + w - 1, par.z0 + par.width - 1);
                    if (hitsStarter(envMinX, envMaxX, envMinZ, envMaxZ)) continue;

                    leg1.alongX = true;
                    leg1.z0 = leg1.z1 = leg1Z;
                    leg1.x0 = envMinX;
                    leg1.x1 = envMaxX;

                    leg2.alongX = false;
                    leg2.x0 = leg2.x1 = retX;
                    leg2.z0 = envMinZ;
                    leg2.z1 = envMaxZ;
                    built = true;
                    break;
                }
            }

            if (built) {
                regCorridors[i].connections++;
                regCorridors[pIdx].connections++;
                loopCorridors.push_back(leg1);
                loopCorridors.push_back(leg2);
                state.ringLoopsCreated++;
            }
        } else if (mode == 1) {
            // 1/3 -> Walled 3x4 room at dead-end tip with separating wall, 1x2m doorway, hinged door & Rare Cabinet
            DeadEndRoom rm{};
            rm.door.width = 1.0f;
            rm.door.height = 2.0f;
            rm.door.regX = regX;
            rm.door.regZ = regZ;
            if (seg.alongX) {
                // 3m deep along X, 4m wide along Z, with 1m separating wall between corridor and room
                int midZ = seg.z0 + seg.width / 2;
                rm.z0 = midZ - 2;
                rm.z1 = rm.z0 + 3; // 4 cells wide (inclusive)
                rm.doorZ = midZ;
                if (d > 0) {
                    rm.doorX = seg.x1 + 1;   // Solid dividing wall at x = seg.x1 + 1
                    rm.x0 = seg.x1 + 2;      // Room interior starts after dividing wall
                    rm.x1 = rm.x0 + 2;       // 3 cells deep
                    rm.door.hingePos = {static_cast<float>(rm.doorX) + 0.92f, static_cast<float>(FLOOR_Y), static_cast<float>(midZ)};
                    rm.door.closedYaw = -PI * 0.5f;
                    rm.door.openSign = 1.0f; // Swings inward (+X) into room
                    rm.cabWallX = static_cast<float>(rm.x1 + 1);
                    rm.cabWallZ = 0.5f * (rm.z0 + rm.z1 + 1);
                    rm.cabNormalX = -1;
                    rm.cabNormalZ = 0;
                } else {
                    rm.doorX = seg.x0 - 1;
                    rm.x1 = seg.x0 - 2;
                    rm.x0 = rm.x1 - 2;
                    rm.door.hingePos = {static_cast<float>(rm.doorX) + 0.08f, static_cast<float>(FLOOR_Y), static_cast<float>(midZ + 1)};
                    rm.door.closedYaw = PI * 0.5f;
                    rm.door.openSign = 1.0f; // Swings inward (-X) into room
                    rm.cabWallX = static_cast<float>(rm.x0);
                    rm.cabWallZ = 0.5f * (rm.z0 + rm.z1 + 1);
                    rm.cabNormalX = 1;
                    rm.cabNormalZ = 0;
                }
            } else {
                // 3m deep along Z, 4m wide along X, with 1m separating wall between corridor and room
                int midX = seg.x0 + seg.width / 2;
                rm.x0 = midX - 2;
                rm.x1 = rm.x0 + 3; // 4 cells wide
                rm.doorX = midX;
                if (d > 0) {
                    rm.doorZ = seg.z1 + 1;
                    rm.z0 = seg.z1 + 2;
                    rm.z1 = rm.z0 + 2; // 3 cells deep
                    rm.door.hingePos = {static_cast<float>(midX), static_cast<float>(FLOOR_Y), static_cast<float>(rm.doorZ) + 0.92f};
                    rm.door.closedYaw = 0.0f;
                    rm.door.openSign = -1.0f; // Swings inward (+Z) into room
                    rm.cabWallX = 0.5f * (rm.x0 + rm.x1 + 1);
                    rm.cabWallZ = static_cast<float>(rm.z1 + 1);
                    rm.cabNormalX = 0;
                    rm.cabNormalZ = -1;
                } else {
                    rm.doorZ = seg.z0 - 1;
                    rm.z1 = seg.z0 - 2;
                    rm.z0 = rm.z1 - 2;
                    rm.door.hingePos = {static_cast<float>(midX + 1), static_cast<float>(FLOOR_Y), static_cast<float>(rm.doorZ) + 0.08f};
                    rm.door.closedYaw = PI;
                    rm.door.openSign = -1.0f; // Swings inward (-Z) into room
                    rm.cabWallX = 0.5f * (rm.x0 + rm.x1 + 1);
                    rm.cabWallZ = static_cast<float>(rm.z0);
                    rm.cabNormalX = 0;
                    rm.cabNormalZ = 1;
                }
            }

            if (!hitsStarter(rm.x0 - 1, rm.x1 + 1, rm.z0 - 1, rm.z1 + 1)) {
                deadEndRooms.push_back(rm);
                state.deadEndRoomsCreated++;
            }
        }
    }

    for (const auto& lc : loopCorridors) {
        regCorridors.push_back(lc);
    }

    // Pass 1: Place outer concrete shells for all corridors & 3x4 dead-end rooms (including dividing wall!)
    for (const auto& seg : regCorridors) {
        fillCorridorShell(state.world, seg);
    }
    for (const auto& rm : deadEndRooms) {
        state.world.fillBox(rm.x0 - 1, FLOOR_Y - 1, rm.z0 - 1, rm.x1 + 1, FLOOR_Y + CEIL_H, rm.z1 + 1, BlockType::Concrete);
    }
    // Pass 2: Carve air/floor/ceiling for all corridors, 3x4 rooms & their 1x2m doorways
    for (const auto& seg : regCorridors) {
        carveCorridor(state.world, seg);
    }
    for (const auto& rm : deadEndRooms) {
        state.world.fillBox(rm.x0, FLOOR_Y, rm.z0, rm.x1, FLOOR_Y + CEIL_H - 1, rm.z1, BlockType::Air);
        state.world.fillBox(rm.x0, FLOOR_Y - 1, rm.z0, rm.x1, FLOOR_Y - 1, rm.z1, BlockType::TechPanel);
        state.world.fillBox(rm.x0, FLOOR_Y + CEIL_H, rm.z0, rm.x1, FLOOR_Y + CEIL_H, rm.z1, BlockType::DarkSteel);
        // Carve 1m x 2m doorway through the separating concrete wall
        state.world.fillBox(rm.doorX, FLOOR_Y, rm.doorZ, rm.doorX, FLOOR_Y + 1, rm.doorZ, BlockType::Air);
        state.world.setBlock(rm.doorX, FLOOR_Y - 1, rm.doorZ, BlockType::TechPanel);
        state.roomDoors.push_back(rm.door);
    }
    for (const auto& existing : state.corridors) {
        if (std::abs(existing.regX - regX) <= 1 && std::abs(existing.regZ - regZ) <= 1) {
            carveCorridor(state.world, existing);
        }
    }

    // Populate lamps, lockers, double-door metal cabinets and 3D furniture
    for (const auto& seg : regCorridors) {
        populateCorridor(state.world, seg, state.pointLights, state.lockers,
                         state.cabinets, state.lootSurfaces, state.spawnPos, settings, rng);
        state.corridors.push_back(seg);
    }

    // Furnish each 3x4 dead-end room with a warm wall lamp & a Rare Reinforced Cabinet (increased loot!)
    for (const auto& rm : deadEndRooms) {
        placeWallLamp(state.world, state.pointLights, rm.cabWallX, rm.cabWallZ,
                      rm.cabNormalX, rm.cabNormalZ, regX, regZ, settings);
        placeMetalCabinet(state.world, state.cabinets, state.lootSurfaces,
                          rm.cabWallX, rm.cabWallZ, rm.cabNormalX, rm.cabNormalZ,
                          regX, regZ, state.spawnPos, rng, true);
    }

    // Build the Central Starter Room in region (0, 0) at x=[55..60], z=[55..60]
    // Completely off the main corridors (spineNS at x=63..65, spineWE at z=63..65) so nothing overlaps!
    if (regX == 0 && regZ == 0) {
        auto& w = state.world;
        w.fillBox(54, FLOOR_Y - 1, 54, 62, FLOOR_Y + CEIL_H, 61, BlockType::Concrete);
        // Hollow 6x4x6 interior at x=[55..60], z=[55..60]
        w.fillBox(55, FLOOR_Y, 55, 60, FLOOR_Y + CEIL_H - 1, 60, BlockType::Air);
        w.fillBox(55, FLOOR_Y - 1, 55, 60, FLOOR_Y - 1, 60, BlockType::TechPanel);
        w.fillBox(55, FLOOR_Y + CEIL_H, 55, 60, FLOOR_Y + CEIL_H, 60, BlockType::DarkSteel);

        // 1m x 2m Doorway on East wall (x=61..62, z=58, y=1..2) opening directly into spineNS1 (x=63)
        w.fillBox(61, FLOOR_Y, 58, 62, FLOOR_Y + 1, 58, BlockType::Air);
        w.fillBox(61, FLOOR_Y - 1, 58, 62, FLOOR_Y - 1, 58, BlockType::TechPanel);

        // 1) Table against West wall (x=55.15..56.15, z=57.0..58.8)
        w.addProp({{55.15f, 1.76f, 57.00f}, {56.15f, 1.84f, 58.80f}}, BlockType::HazardStripe, true, false, true, 0, 0);
        w.addProp({{55.20f, 1.00f, 57.05f}, {55.30f, 1.76f, 57.15f}}, BlockType::Grate, true, false, true, 0, 0);
        w.addProp({{56.00f, 1.00f, 57.05f}, {56.10f, 1.76f, 57.15f}}, BlockType::Grate, true, false, true, 0, 0);
        w.addProp({{55.20f, 1.00f, 58.65f}, {55.30f, 1.76f, 58.75f}}, BlockType::Grate, true, false, true, 0, 0);
        w.addProp({{56.00f, 1.00f, 58.65f}, {56.10f, 1.76f, 58.75f}}, BlockType::Grate, true, false, true, 0, 0);

        // 2) Hollow Openable Food/Water Cabinet against North wall (x=57.3..58.7, z=55.05..56.05)
        w.addProp({{57.30f, 1.00f, 55.05f}, {58.70f, 2.85f, 55.13f}}, BlockType::RustPlate, true, false, true, 0, 0);
        w.addProp({{57.30f, 1.00f, 55.05f}, {57.38f, 2.85f, 56.05f}}, BlockType::RustPlate, true, false, true, 0, 0);
        w.addProp({{58.62f, 1.00f, 55.05f}, {58.70f, 2.85f, 56.05f}}, BlockType::RustPlate, true, false, true, 0, 0);
        w.addProp({{57.30f, 2.77f, 55.05f}, {58.70f, 2.85f, 56.05f}}, BlockType::RustPlate, false, false, true, 0, 0);
        w.addProp({{57.30f, 1.00f, 55.05f}, {58.70f, 1.08f, 56.05f}}, BlockType::RustPlate, false, false, true, 0, 0);
        w.addProp({{57.38f, 1.58f, 55.13f}, {58.62f, 1.64f, 56.00f}}, BlockType::Grate, false, false, true, 0, 0);
        w.addProp({{57.38f, 2.10f, 55.13f}, {58.62f, 2.16f, 56.00f}}, BlockType::Grate, false, false, true, 0, 0);

        // 3) Wall lamp inside starter room
        placeWallLamp(w, state.pointLights, 58.0f, 61.0f, 0, -1, 0, 0, settings);
    }
}

bool MegaGenerator::updateInfiniteStream(MegaResult& state, const Vec3& playerPos,
                                         const GameSettings& settings) {
    int rx = WorldGrid::floorDiv(static_cast<int>(std::floor(playerPos.x)), cfg::REGION_SIZE);
    int rz = WorldGrid::floorDiv(static_cast<int>(std::floor(playerPos.z)), cfg::REGION_SIZE);
    return updateInfiniteStreamAtRegion(state, rx, rz, settings);
}

bool MegaGenerator::updateInfiniteStreamAtRegion(MegaResult& state, int rx, int rz,
                                                 const GameSettings& settings) {
    state.currentRegX = rx;
    state.currentRegZ = rz;

    bool changed = false;
    int R = settings.streamRadius;
    for (int dz = -R; dz <= R; ++dz) {
        for (int dx = -R; dx <= R; ++dx) {
            int nx = rx + dx;
            int nz = rz + dz;
            if (state.loadedRegions.count({nx, nz}) == 0) {
                generateRegion(state, nx, nz, settings);
                changed = true;
            }
        }
    }

    // Unload regions outside keepRadius = R + 1
    int keepRadius = R + 1;
    std::vector<std::pair<int, int>> toUnload;
    for (const auto& reg : state.loadedRegions) {
        if (std::abs(reg.first - rx) > keepRadius || std::abs(reg.second - rz) > keepRadius) {
            toUnload.push_back(reg);
        }
    }

    if (!toUnload.empty()) {
        for (const auto& reg : toUnload) {
            state.loadedRegions.erase(reg);
        }
        state.world.unloadFarRegions(rx, rz, keepRadius);
        state.pointLights.erase(
            std::remove_if(state.pointLights.begin(), state.pointLights.end(),
                           [&](const PointLight3D& l) {
                               return std::abs(l.regX - rx) > keepRadius ||
                                      std::abs(l.regZ - rz) > keepRadius;
                           }),
            state.pointLights.end());
        state.corridors.erase(
            std::remove_if(state.corridors.begin(), state.corridors.end(),
                           [&](const CorridorSegment& c) {
                               return std::abs(c.regX - rx) > keepRadius ||
                                      std::abs(c.regZ - rz) > keepRadius;
                           }),
            state.corridors.end());
        state.lockers.erase(
            std::remove_if(state.lockers.begin(), state.lockers.end(),
                           [&](const LockerPlacement& l) {
                               return std::abs(l.regX - rx) > keepRadius ||
                                      std::abs(l.regZ - rz) > keepRadius;
                           }),
            state.lockers.end());
        state.cabinets.erase(
            std::remove_if(state.cabinets.begin(), state.cabinets.end(),
                           [&](const DoubleCabinetPlacement& c) {
                               return std::abs(c.regX - rx) > keepRadius ||
                                      std::abs(c.regZ - rz) > keepRadius;
                           }),
            state.cabinets.end());
        state.roomDoors.erase(
            std::remove_if(state.roomDoors.begin(), state.roomDoors.end(),
                           [&](const RoomDoorPlacement& d) {
                               return std::abs(d.regX - rx) > keepRadius ||
                                      std::abs(d.regZ - rz) > keepRadius;
                           }),
            state.roomDoors.end());
        state.lootSurfaces.erase(
            std::remove_if(state.lootSurfaces.begin(), state.lootSurfaces.end(),
                           [&](const LootSurface& s) {
                               return std::abs(s.regX - rx) > keepRadius ||
                                      std::abs(s.regZ - rz) > keepRadius;
                           }),
            state.lootSurfaces.end());
        changed = true;
    }

    return changed;
}

MegaResult MegaGenerator::generate(uint64_t seed, const GameSettings& settings) {
    auto t0 = std::chrono::steady_clock::now();
    MegaResult res;
    res.seed = seed;
    // Spawn player inside the central starter room (x=[55..60], z=[55..60])
    res.spawnPos = {58.0f, static_cast<float>(FLOOR_Y), 58.2f};

    // Generate initial active region window around spawn
    updateInfiniteStream(res, res.spawnPos, settings);

    auto t1 = std::chrono::steady_clock::now();
    res.generationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

} // namespace mega
