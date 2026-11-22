#include "game/Inventory3D.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

namespace {

bool rayAABB(const Vec3& orig, const Vec3& dir, const AABB& b, float maxDist, float& outT) {
    float tmin = 0.0f;
    float tmax = maxDist;
    float o[3] = {orig.x, orig.y, orig.z};
    float d[3] = {dir.x, dir.y, dir.z};
    float mn[3] = {b.min.x, b.min.y, b.min.z};
    float mx[3] = {b.max.x, b.max.y, b.max.z};

    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-6f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (mn[i] - o[i]) * inv;
            float t2 = (mx[i] - o[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    if (tmax < 0.0f) return false;
    outT = std::max(0.0f, tmin);
    return outT <= maxDist;
}

} // namespace

Int3 itemBaseGridSize(ItemType type) {
    switch (type) {
        case ItemType::Flashlight: return {2, 2, 4};  // 2 x 2 x 4
        case ItemType::Battery:    return {1, 2, 1};  // 1 x 2 x 1
        case ItemType::Food:       return {1, 1, 2};  // 1 x 1 x 2
        case ItemType::Water:      return {2, 3, 2};  // 2 x 3 x 2
        case ItemType::Lockpick:   return {1, 1, 3};  // 1 x 1 x 3
        case ItemType::Broom:      return {2, 30, 2}; // 2 x 30 x 2
        case ItemType::Mop:        return {2, 30, 2}; // 2 x 30 x 2
        case ItemType::Tape:       return {2, 1, 2};  // 2 x 1 x 2
        case ItemType::Knife:      return {1, 6, 1};  // 1 x 6 x 1
        case ItemType::Stick:      return {1, 26, 1}; // 1 x 26 x 1 (broken broom/mop handle)
        case ItemType::Spear:      return {1, 30, 1}; // 1 x 30 x 1 (crafted spear, fits 2x30x6 back slot!)
    }
    return {1, 1, 1};
}

const char* itemDisplayName(ItemType type) {
    switch (type) {
        case ItemType::Flashlight: return "FLASHLIGHT";
        case ItemType::Battery:    return "BATTERY";
        case ItemType::Food:       return "FOOD BAR";
        case ItemType::Water:      return "WATER";
        case ItemType::Lockpick:   return "LOCKPICK";
        case ItemType::Broom:      return "BROOM";
        case ItemType::Mop:        return "MOP";
        case ItemType::Tape:       return "TAPE";
        case ItemType::Knife:      return "KNIFE";
        case ItemType::Stick:      return "STICK";
        case ItemType::Spear:      return "SPEAR";
    }
    return "ITEM";
}

int defaultItemDurability(ItemType type, uint64_t seed) {
    if (type == ItemType::Broom || type == ItemType::Mop) {
        Rng rng(seed ^ 0xB8004A1ULL);
        return rng.rangeInt(1, 3); // Breaks after 1..3 hits -> turns into Stick
    }
    return 0;
}

Mat4 ItemOrientation::toMatrix() const {
    return Mat4::rotateY(static_cast<float>(rotY & 3) * (PI * 0.5f))
         * Mat4::rotateX(static_cast<float>(rotX & 3) * (PI * 0.5f))
         * Mat4::rotateZ(static_cast<float>(rotZ & 3) * (PI * 0.5f));
}

Int3 ItemOrientation::applyToSize(Int3 baseSize) const {
    Int3 s = baseSize;
    if ((rotZ & 1) != 0) std::swap(s.x, s.y);
    if ((rotX & 1) != 0) std::swap(s.y, s.z);
    if ((rotY & 1) != 0) std::swap(s.x, s.z);
    return s;
}

void Backpack3D::clear() {
    items.clear();
    nextId = 1;
}

bool Backpack3D::inBounds(int gx, int gy, int gz, Int3 sz) const {
    return gx >= 0 && gy >= 0 && gz >= 0 &&
           gx + sz.x <= DIM_X &&
           gy + sz.y <= DIM_Y &&
           gz + sz.z <= DIM_Z;
}

bool Backpack3D::inBackBounds(int gx, int gy, int gz, Int3 sz) const {
    return gx >= 0 && gy >= 0 && gz >= 0 &&
           gx + sz.x <= BACK_DIM_X &&
           gy + sz.y <= BACK_DIM_Y &&
           gz + sz.z <= BACK_DIM_Z;
}

bool Backpack3D::isEligibleForBackSlot(ItemType type, const ItemOrientation& orient) {
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    // Must occupy at least 20 vertical cells to be secured by the two back straps
    return sz.y >= BACK_MIN_HEIGHT;
}

bool Backpack3D::overlapsAny(int gx, int gy, int gz, Int3 sz, bool inBackSlot, int ignoreId) const {
    for (const auto& it : items) {
        if (it.id == ignoreId || it.inBackSlot != inBackSlot) continue;
        Int3 os = it.size();
        bool ox = (gx < it.gx + os.x) && (gx + sz.x > it.gx);
        bool oy = (gy < it.gy + os.y) && (gy + sz.y > it.gy);
        bool oz = (gz < it.gz + os.z) && (gz + sz.z > it.gz);
        if (ox && oy && oz) return true;
    }
    return false;
}

bool Backpack3D::hasSupportBelow(int gx, int gy, int gz, Int3 sz, int ignoreId) const {
    if (gy == 0) return true;
    for (const auto& it : items) {
        if (it.id == ignoreId || it.inBackSlot) continue;
        Int3 os = it.size();
        if (it.gy + os.y != gy) continue;
        bool ox = (gx < it.gx + os.x) && (gx + sz.x > it.gx);
        bool oz = (gz < it.gz + os.z) && (gz + sz.z > it.gz);
        if (ox && oz) return true;
    }
    return false;
}

int Backpack3D::findDropY(int gx, int gz, Int3 sz, int ignoreId) const {
    if (gx < 0 || gz < 0 || gx + sz.x > DIM_X || gz + sz.z > DIM_Z) return -1;
    int restY = 0;
    for (const auto& it : items) {
        if (it.id == ignoreId || it.inBackSlot) continue;
        Int3 os = it.size();
        bool ox = (gx < it.gx + os.x) && (gx + sz.x > it.gx);
        bool oz = (gz < it.gz + os.z) && (gz + sz.z > it.gz);
        if (ox && oz) {
            restY = std::max(restY, it.gy + os.y);
        }
    }
    if (restY + sz.y > DIM_Y) return -1;
    if (overlapsAny(gx, restY, gz, sz, false, ignoreId)) return -1;
    return restY;
}

bool Backpack3D::canPlaceAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                            int ignoreId) const {
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    if (!inBounds(gx, gy, gz, sz)) return false;
    if (overlapsAny(gx, gy, gz, sz, false, ignoreId)) return false;
    if (!hasSupportBelow(gx, gy, gz, sz, ignoreId)) return false;
    return true;
}

int Backpack3D::placeAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                        int durability) {
    if (!canPlaceAt(gx, gy, gz, type, orient)) return -1;
    PackedItem pi;
    pi.id = nextId++;
    pi.type = type;
    pi.inBackSlot = false;
    pi.gx = gx;
    pi.gy = gy;
    pi.gz = gz;
    pi.durability = durability;
    pi.orient = orient;
    items.push_back(pi);
    return pi.id;
}

bool Backpack3D::canPlaceInBackSlot(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                                    int ignoreId) const {
    if (!isEligibleForBackSlot(type, orient)) return false;
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));
    if (!inBackBounds(gx, gy, gz, sz)) return false;
    if (overlapsAny(gx, gy, gz, sz, true, ignoreId)) return false;
    return true;
}

int Backpack3D::placeInBackSlot(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                                int durability) {
    if (!canPlaceInBackSlot(gx, gy, gz, type, orient)) return -1;
    PackedItem pi;
    pi.id = nextId++;
    pi.type = type;
    pi.inBackSlot = true;
    pi.gx = gx;
    pi.gy = gy;
    pi.gz = gz;
    pi.durability = durability;
    pi.orient = orient;
    items.push_back(pi);
    return pi.id;
}

int Backpack3D::tryAutoAdd(ItemType type, int durability) {
    static constexpr ItemOrientation CANDIDATES[6] = {
        {0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}
    };

    // 1) Try main 6x8x4 interior cavity first
    for (int gy = 0; gy < DIM_Y; ++gy) {
        for (const auto& ori : CANDIDATES) {
            Int3 sz = ori.applyToSize(itemBaseGridSize(type));
            if (gy + sz.y > DIM_Y) continue;
            for (int gz = 0; gz <= DIM_Z - sz.z; ++gz) {
                for (int gx = 0; gx <= DIM_X - sz.x; ++gx) {
                    if (canPlaceAt(gx, gy, gz, type, ori)) {
                        return placeAt(gx, gy, gz, type, ori, durability);
                    }
                }
            }
        }
    }

    // 2) For tall items (height >= 20 cells), try attaching to the rear 2x30x6 strap slot!
    for (const auto& ori : CANDIDATES) {
        if (!isEligibleForBackSlot(type, ori)) continue;
        Int3 sz = ori.applyToSize(itemBaseGridSize(type));
        for (int gx = 0; gx <= BACK_DIM_X - sz.x; ++gx) {
            for (int gz = 0; gz <= BACK_DIM_Z - sz.z; ++gz) {
                int gy = std::max(0, (BACK_DIM_Y - sz.y) / 2);
                if (canPlaceInBackSlot(gx, gy, gz, type, ori)) {
                    return placeInBackSlot(gx, gy, gz, type, ori, durability);
                }
            }
        }
    }
    return -1;
}

bool Backpack3D::isBlockedFromAbove(int itemId) const {
    const PackedItem* target = findById(itemId);
    if (!target || target->inBackSlot) return false; // Rear strapped items are directly accessible
    Int3 ts = target->size();
    int topY = target->gy + ts.y;

    for (const auto& other : items) {
        if (other.id == itemId || other.inBackSlot) continue;
        Int3 os = other.size();
        if (other.gy >= topY) {
            bool ox = (target->gx < other.gx + os.x) && (target->gx + ts.x > other.gx);
            bool oz = (target->gz < other.gz + os.z) && (target->gz + ts.z > other.gz);
            if (ox && oz) return true;
        }
    }
    return false;
}

const PackedItem* Backpack3D::findById(int itemId) const {
    for (const auto& it : items) {
        if (it.id == itemId) return &it;
    }
    return nullptr;
}

bool Backpack3D::removeById(int itemId, PackedItem* outRemoved) {
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (it->id == itemId) {
            if (outRemoved) *outRemoved = *it;
            items.erase(it);
            return true;
        }
    }
    return false;
}

int Backpack3D::countType(ItemType type) const {
    int n = 0;
    for (const auto& it : items) {
        if (it.type == type) n++;
    }
    return n;
}

int Backpack3D::findFirstAccessible(ItemType type) const {
    int bestId = -1;
    int bestY = -1;
    for (const auto& it : items) {
        if (it.type != type) continue;
        if (isBlockedFromAbove(it.id)) continue;
        if (it.gy > bestY) {
            bestY = it.gy;
            bestId = it.id;
        }
    }
    return bestId;
}

AABB Backpack3D::itemLocalBounds(int gx, int gy, int gz, Int3 sz, bool inBackSlot) {
    if (inBackSlot) {
        return {
            {gx * CELL_SIZE, BACK_OFFSET_Y + gy * CELL_SIZE, BACK_OFFSET_Z + gz * CELL_SIZE},
            {(gx + sz.x) * CELL_SIZE, BACK_OFFSET_Y + (gy + sz.y) * CELL_SIZE,
             BACK_OFFSET_Z + (gz + sz.z) * CELL_SIZE}
        };
    }
    return {
        {gx * CELL_SIZE, gy * CELL_SIZE, gz * CELL_SIZE},
        {(gx + sz.x) * CELL_SIZE, (gy + sz.y) * CELL_SIZE, (gz + sz.z) * CELL_SIZE}
    };
}

AABB Backpack3D::itemLocalBounds(const PackedItem& it) {
    return itemLocalBounds(it.gx, it.gy, it.gz, it.size(), it.inBackSlot);
}

AABB Backpack3D::backSlotOuterBounds() {
    return {
        {0.0f, BACK_OFFSET_Y, BACK_OFFSET_Z},
        {BACK_DIM_X * CELL_SIZE, BACK_OFFSET_Y + BACK_DIM_Y * CELL_SIZE,
         BACK_OFFSET_Z + BACK_DIM_Z * CELL_SIZE}
    };
}

int Backpack3D::raycastItemsLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                                  float* outT) const {
    int bestId = -1;
    float bestT = maxDist;
    for (const auto& it : items) {
        float t = 0.0f;
        if (rayAABB(localOrigin, localDir, itemLocalBounds(it), bestT, t)) {
            bestT = t;
            bestId = it.id;
        }
    }
    if (bestId >= 0 && outT) *outT = bestT;
    return bestId;
}

bool Backpack3D::raycastPlacementLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                                       ItemType type, const ItemOrientation& orient,
                                       int& outGx, int& outGy, int& outGz, bool& outInBackSlot) const {
    Int3 sz = orient.applyToSize(itemBaseGridSize(type));

    // 1) Check rear 2x30x6 strap slot first if item is tall (>= 20 cells) and ray hits back slot
    if (isEligibleForBackSlot(type, orient)) {
        AABB backBox = backSlotOuterBounds();
        float backT = 0.0f;
        if (rayAABB(localOrigin, localDir, backBox, maxDist, backT)) {
            Vec3 bp = localOrigin + localDir * backT;
            int cx = static_cast<int>(std::floor(bp.x / CELL_SIZE - sz.x * 0.5f + 0.5f));
            int cz = static_cast<int>(std::floor((bp.z - BACK_OFFSET_Z) / CELL_SIZE - sz.z * 0.5f + 0.5f));
            cx = std::clamp(cx, 0, std::max(0, BACK_DIM_X - sz.x));
            cz = std::clamp(cz, 0, std::max(0, BACK_DIM_Z - sz.z));
            int cy = std::clamp(static_cast<int>(std::floor((bp.y - BACK_OFFSET_Y) / CELL_SIZE - sz.y * 0.5f + 0.5f)),
                                0, std::max(0, BACK_DIM_Y - sz.y));

            if (canPlaceInBackSlot(cx, cy, cz, type, orient)) {
                outGx = cx; outGy = cy; outGz = cz; outInBackSlot = true;
                return true;
            }
            for (int dx = -2; dx <= 2; ++dx) {
                int nx = std::clamp(cx + dx, 0, std::max(0, BACK_DIM_X - sz.x));
                for (int nz = 0; nz <= BACK_DIM_Z - sz.z; ++nz) {
                    if (canPlaceInBackSlot(nx, cy, nz, type, orient)) {
                        outGx = nx; outGy = cy; outGz = nz; outInBackSlot = true;
                        return true;
                    }
                }
            }
        }
    }

    // 2) Check main 6x8x4 interior cavity
    AABB innerBox{
        {0.0f, 0.0f, 0.0f},
        {DIM_X * CELL_SIZE, DIM_Y * CELL_SIZE, DIM_Z * CELL_SIZE}
    };
    float boxT = 0.0f;
    if (!rayAABB(localOrigin, localDir, innerBox, maxDist, boxT)) return false;

    float bestT = maxDist;
    bool hitSurface = false;
    Vec3 hitPt{};

    for (const auto& it : items) {
        if (it.inBackSlot) continue;
        float t = 0.0f;
        if (rayAABB(localOrigin, localDir, itemLocalBounds(it), bestT, t)) {
            bestT = t;
            hitPt = localOrigin + localDir * t;
            hitSurface = true;
        }
    }

    if (std::abs(localDir.y) > 1e-5f) {
        float floorT = -localOrigin.y / localDir.y;
        if (floorT > 0.0f && floorT < bestT) {
            Vec3 fp = localOrigin + localDir * floorT;
            if (fp.x >= -0.02f && fp.x <= DIM_X * CELL_SIZE + 0.02f &&
                fp.z >= -0.02f && fp.z <= DIM_Z * CELL_SIZE + 0.02f) {
                bestT = floorT;
                hitPt = fp;
                hitSurface = true;
            }
        }
    }

    if (!hitSurface) {
        hitPt = localOrigin + localDir * boxT;
    }

    int cx = static_cast<int>(std::floor(hitPt.x / CELL_SIZE - sz.x * 0.5f + 0.5f));
    int cz = static_cast<int>(std::floor(hitPt.z / CELL_SIZE - sz.z * 0.5f + 0.5f));
    cx = std::clamp(cx, 0, std::max(0, DIM_X - sz.x));
    cz = std::clamp(cz, 0, std::max(0, DIM_Z - sz.z));

    int cy = findDropY(cx, cz, sz);
    if (cy >= 0 && canPlaceAt(cx, cy, cz, type, orient)) {
        outGx = cx; outGy = cy; outGz = cz; outInBackSlot = false;
        return true;
    }

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = std::clamp(cx + dx, 0, std::max(0, DIM_X - sz.x));
            int nz = std::clamp(cz + dz, 0, std::max(0, DIM_Z - sz.z));
            int ny = findDropY(nx, nz, sz);
            if (ny >= 0 && canPlaceAt(nx, ny, nz, type, orient)) {
                outGx = nx; outGy = ny; outGz = nz; outInBackSlot = false;
                return true;
            }
        }
    }
    return false;
}

} // namespace mega
