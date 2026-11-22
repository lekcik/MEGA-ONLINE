#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "game/Inventory3D.hpp"
#include "render/Shader.hpp"
#include "render/GlApi.hpp"
#include <vector>

namespace mega {

struct RobotRenderState {
    Vec3 pos;
    float yaw = 0.0f;       // Facing direction (0 = +Z)
    Vec3 footWorldPos[2];   // World-space anchored/stepping foot positions (Left=0, Right=1)
    float pelvisOffsetY = 0.0f;
    float armAngle[2] = {0.0f, 0.0f};
    float torsoLeanX = 0.0f;
    bool alerted = false;   // false = cyan LED, true = red LED
    bool hurt = false;
};

struct DoorRenderState {
    Vec3 hingePos;
    float width = 1.0f;
    float height = 2.2f;
    float thickness = 0.08f;
    float yaw = 0.0f; // closedYaw + openSign * angle
    bool isCabinet = false;
    bool isRareCabinet = false;
    bool highlighted = false;
};

struct ItemRenderState {
    Vec3 pos;               // World bottom-center or min corner of item
    int itemKind = 0;       // ItemType index (0..Count-1)
    ItemOrientation orient{};
    bool highlighted = false;
};

struct GhostPreviewState {
    bool active = false;
    bool valid = false;
    bool inBackpack = false;
    bool inBackSlot = false; // True when snapping to rear 2x30x6 strap slot
    ItemType type = ItemType::Food;
    ItemOrientation orient{};
    Vec3 worldPos{};        // World bottom-center when !inBackpack
    int gx = 0, gy = 0, gz = 0; // Backpack cell coords when inBackpack
};

struct BackpackRenderState {
    bool visible = false;
    Mat4 worldMatrix = Mat4::identity(); // Local [0..DIM_X*CELL, 0..DIM_Y*CELL, 0..DIM_Z*CELL] -> World
    int hoveredItemId = -1;
    bool hoveredBlocked = false;
    const Backpack3D* backpack = nullptr;
};

struct SurvivorRenderState {
    bool visible = false;
    Vec3 pos{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool flashlightOn = false;
    bool hasHeldItem = false;
    ItemType heldItemType = ItemType::Food;
    ItemOrientation heldOrient{};
    float meleeSwingTimer = 0.0f;
    float hurtFlash = 0.0f;
    float walkPhase = 0.0f;
    float speedXZ = 0.0f;
};

class RobotRenderer {
public:
    RobotRenderer() = default;
    ~RobotRenderer();

    bool init();
    void render(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                const std::vector<RobotRenderState>& robots,
                const GameSettings& settings = GameSettings{});
    void renderSurvivor(const Shader& worldShader, const SurvivorRenderState& survivor);
    void renderMonsterClaws(const Shader& worldShader, const Vec3& eye, const Vec3& fwd,
                            const Vec3& right, float swingTimer, float dashTimer);
    void renderInteractables(const Shader& worldShader, const Mat4& view, const Mat4& proj,
                             const Vec3& camPos,
                             const std::vector<DoorRenderState>& doors,
                             const std::vector<ItemRenderState>& items,
                             const BackpackRenderState& backpackState = {},
                             const GhostPreviewState& ghost = {},
                             bool hasHeldItem = false,
                             ItemType heldType = ItemType::Food,
                             const ItemOrientation& heldOrient = {},
                             const Mat4& heldMatrix = Mat4::identity());

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    Shader robotShader_;

    void drawPart(const Mat4& model, const Vec3& partScale, float texLayer, float emissive,
                  const Vec3& emissiveColor = {1.0f, 1.0f, 1.0f});
};

} // namespace mega
