#pragma once

#include "core/LootTable.hpp"
#include "core/Math3D.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace mega {

struct Int3 {
    int x = 0;
    int y = 0;
    int z = 0;
};

// Discrete 90-degree 3D rotation state (rotX, rotY, rotZ in 0..3 quarter-turns)
struct ItemOrientation {
    int rotX = 0; // Pitch quarter-turns (0..3)
    int rotY = 0; // Yaw quarter-turns (0..3)
    int rotZ = 0; // Roll quarter-turns (0..3)

    void rotateX() { rotX = (rotX + 1) & 3; }
    void rotateY() { rotY = (rotY + 1) & 3; }
    void rotateZ() { rotZ = (rotZ + 1) & 3; }

    Mat4 toMatrix() const;
    Int3 applyToSize(Int3 baseSize) const;
};

// Base 3D grid dimensions (X, Y, Z) in backpack cells (1 cell = 0.05m = 5cm in world)
Int3 itemBaseGridSize(ItemType type);
const char* itemDisplayName(ItemType type);
int defaultItemDurability(ItemType type, uint64_t seed);

struct PackedItem {
    int id = 0;
    ItemType type = ItemType::Food;
    bool inBackSlot = false; // false = main 6x8x4 cavity, true = rear 2x30x6 strap slot
    int gx = 0;
    int gy = 0;
    int gz = 0;
    int durability = 0;      // 1..3 hits for Broom/Mop before head snaps into Stick
    ItemOrientation orient{};

    Int3 size() const { return orient.applyToSize(itemBaseGridSize(type)); }
};

class Backpack3D {
public:
    // Main interior compartment: 6 (width X) x 8 (height Y) x 4 (depth Z)
    static constexpr int DIM_X = 6;
    static constexpr int DIM_Y = 8;
    static constexpr int DIM_Z = 4;

    // Rear strap slot on backpack spine: 6 (width X) x 30 (height Y) x 2 (depth Z) = 2x30x6 volume
    // Only accepts long items with vertical height >= 20 cells!
    static constexpr int BACK_DIM_X = 6;
    static constexpr int BACK_DIM_Y = 30;
    static constexpr int BACK_DIM_Z = 2;
    static constexpr int BACK_MIN_HEIGHT = 20;

    static constexpr float CELL_SIZE = 0.05f; // 5cm per 3D cell (Broom/Mop 30 cells = 1.50m!)
    static constexpr float BACK_OFFSET_Y = -0.35f;
    static constexpr float BACK_OFFSET_Z = -(BACK_DIM_Z * CELL_SIZE + 0.018f);

    std::vector<PackedItem> items;
    int nextId = 1;

    void clear();

    // Main compartment checks
    bool inBounds(int gx, int gy, int gz, Int3 sz) const;
    bool overlapsAny(int gx, int gy, int gz, Int3 sz, bool inBackSlot = false, int ignoreId = -1) const;
    bool hasSupportBelow(int gx, int gy, int gz, Int3 sz, int ignoreId = -1) const;
    int findDropY(int gx, int gz, Int3 sz, int ignoreId = -1) const;

    bool canPlaceAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                    int ignoreId = -1) const;
    int placeAt(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                int durability = 0);

    // Rear 2x30x6 strap slot checks (requires vertical height >= 20 cells!)
    static bool isEligibleForBackSlot(ItemType type, const ItemOrientation& orient);
    bool inBackBounds(int gx, int gy, int gz, Int3 sz) const;
    bool canPlaceInBackSlot(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                            int ignoreId = -1) const;
    int placeInBackSlot(int gx, int gy, int gz, ItemType type, const ItemOrientation& orient,
                        int durability = 0);

    // Automatically packs into main 6x8x4 cavity or rear 2x30x6 strap slot (if tall >= 20)
    int tryAutoAdd(ItemType type, int durability = 0);

    // Physical top-occlusion rule (applies to main cavity; rear strapped items slide straight out)
    bool isBlockedFromAbove(int itemId) const;

    const PackedItem* findById(int itemId) const;
    bool removeById(int itemId, PackedItem* outRemoved = nullptr);

    int countType(ItemType type) const;
    int findFirstAccessible(ItemType type) const;

    // Local AABB inside backpack coordinates
    static AABB itemLocalBounds(const PackedItem& it);
    static AABB itemLocalBounds(int gx, int gy, int gz, Int3 sz, bool inBackSlot = false);
    static AABB backSlotOuterBounds();

    // Raycast in backpack-local space against BOTH main cavity and rear strap slot items
    int raycastItemsLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                          float* outT = nullptr) const;
    // Raycast in backpack-local space to find a valid placement cell (either main cavity or rear strap slot)
    bool raycastPlacementLocal(const Vec3& localOrigin, const Vec3& localDir, float maxDist,
                               ItemType type, const ItemOrientation& orient,
                               int& outGx, int& outGy, int& outGz, bool& outInBackSlot) const;
};

} // namespace mega
