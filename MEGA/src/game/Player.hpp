#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "world/WorldGrid.hpp"
#include <vector>

namespace mega {

struct PlayerInput {
    float moveForward = 0.0f; // -1..1
    float moveRight   = 0.0f; // -1..1
    float moveUp      = 0.0f; // -1..1 (used in noclip flight)
    float yawDelta    = 0.0f;
    float pitchDelta  = 0.0f;
    bool jump         = false;
    bool sprint       = false;
    bool noclip       = false;
};

class Player {
public:
    Vec3 position{6.0f, 3.0f, 6.0f}; // Feet position
    Vec3 velocity{0.0f, 0.0f, 0.0f};
    float yaw   = 0.0f; // Radians (0 = -Z)
    float pitch = 0.0f; // Radians
    bool grounded = false;

    void update(const PlayerInput& input, const WorldGrid& world, float dt,
                const std::vector<AABB>& dynamicColliders = {},
                float moveSpeed = cfg::MOVE_SPEED,
                float sprintMult = cfg::SPRINT_MULT,
                float jumpSpeed = cfg::JUMP_SPEED);

    Vec3 eyePosition() const {
        return {position.x, position.y + cfg::PLAYER_EYE_Y, position.z};
    }

    Vec3 forwardDir() const;
    Vec3 rightDir() const;
    Mat4 viewMatrix() const;
    AABB boundingBox() const { return boxAt(position); }

private:
    static AABB boxAt(const Vec3& pos);
    bool collides(const WorldGrid& world, const AABB& box,
                  const std::vector<AABB>& dynamicColliders) const;
    void moveAndSlide(const WorldGrid& world, const Vec3& delta,
                      const std::vector<AABB>& dynamicColliders);
};

} // namespace mega
