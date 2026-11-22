#include "game/Player.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

Vec3 Player::forwardDir() const {
    float cp = std::cos(pitch);
    return {
        std::sin(yaw) * cp,
        std::sin(pitch),
        -std::cos(yaw) * cp
    };
}

Vec3 Player::rightDir() const {
    return {
        std::cos(yaw),
        0.0f,
        std::sin(yaw)
    };
}

Mat4 Player::viewMatrix() const {
    Vec3 eye = eyePosition();
    return Mat4::lookAt(eye, eye + forwardDir(), {0.0f, 1.0f, 0.0f});
}

AABB Player::boxAt(const Vec3& pos) {
    return {
        {pos.x - cfg::PLAYER_RADIUS, pos.y, pos.z - cfg::PLAYER_RADIUS},
        {pos.x + cfg::PLAYER_RADIUS, pos.y + cfg::PLAYER_HEIGHT, pos.z + cfg::PLAYER_RADIUS}
    };
}

bool Player::collides(const WorldGrid& world, const AABB& box,
                      const std::vector<AABB>& dynamicColliders) const {
    int minX = static_cast<int>(std::floor(box.min.x));
    int maxX = static_cast<int>(std::floor(box.max.x - 1e-4f));
    int minY = static_cast<int>(std::floor(box.min.y));
    int maxY = static_cast<int>(std::floor(box.max.y - 1e-4f));
    int minZ = static_cast<int>(std::floor(box.min.z));
    int maxZ = static_cast<int>(std::floor(box.max.z - 1e-4f));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                Cell c = world.getCell(x, y, z);
                if (c.isSolid()) {
                    float blockTop = static_cast<float>(y) + c.topHeight();
                    if (box.min.y < blockTop - 1e-4f) {
                        return true;
                    }
                }
            }
        }
    }
    for (const auto& pc : world.propColliders()) {
        if (box.intersects(pc)) {
            return true;
        }
    }
    for (const auto& dc : dynamicColliders) {
        if (box.intersects(dc)) {
            return true;
        }
    }
    return false;
}

void Player::update(const PlayerInput& input, const WorldGrid& world, float dt,
                    const std::vector<AABB>& dynamicColliders,
                    float moveSpeed, float sprintMult, float jumpSpeed) {
    yaw += input.yawDelta;
    pitch = clampf(pitch + input.pitchDelta, -radians(89.0f), radians(89.0f));

    if (input.noclip) {
        Vec3 wish = forwardDir() * input.moveForward + rightDir() * input.moveRight +
                    Vec3{0.0f, input.moveUp, 0.0f};
        if (lengthSq(wish) > 1e-5f) {
            wish = normalize(wish);
        }
        float flySpeed = moveSpeed * (input.sprint ? sprintMult : 1.0f);
        velocity = wish * flySpeed;
        position += velocity * dt;
        grounded = false;
        return;
    }

    Vec3 fwdXZ{std::sin(yaw), 0.0f, -std::cos(yaw)};
    Vec3 rightXZ{std::cos(yaw), 0.0f, std::sin(yaw)};
    Vec3 wish = fwdXZ * input.moveForward + rightXZ * input.moveRight;
    if (lengthSq(wish) > 1e-5f) {
        wish = normalize(wish);
    }

    float maxSpeed = moveSpeed * (input.sprint ? sprintMult : 1.0f);

    if (grounded) {
        Vec3 horiz{velocity.x, 0.0f, velocity.z};
        float speed = length(horiz);
        if (speed > 0.001f) {
            float drop = speed * cfg::GROUND_FRICTION * dt;
            float newSpeed = std::max(0.0f, speed - drop) / speed;
            velocity.x *= newSpeed;
            velocity.z *= newSpeed;
        }
    }

    float accel = grounded ? cfg::GROUND_ACCEL : cfg::AIR_ACCEL;
    Vec3 targetVel = wish * maxSpeed;
    velocity.x += (targetVel.x - velocity.x) * std::min(1.0f, accel * dt);
    velocity.z += (targetVel.z - velocity.z) * std::min(1.0f, accel * dt);

    if (grounded && input.jump) {
        velocity.y = jumpSpeed;
        grounded = false;
    }
    velocity.y -= cfg::GRAVITY * dt;

    moveAndSlide(world, velocity * dt, dynamicColliders);
}

void Player::moveAndSlide(const WorldGrid& world, const Vec3& delta,
                          const std::vector<AABB>& dynamicColliders) {
    Vec3 horizPos = position;
    bool hitX = false, hitZ = false;

    horizPos.x += delta.x;
    if (collides(world, boxAt(horizPos), dynamicColliders)) {
        horizPos.x = position.x;
        hitX = true;
    }

    horizPos.z += delta.z;
    if (collides(world, boxAt(horizPos), dynamicColliders)) {
        horizPos.z = position.z;
        hitZ = true;
    }

    if ((hitX || hitZ) && grounded) {
        Vec3 stepped = position;
        stepped.y += cfg::STEP_HEIGHT;
        if (!collides(world, boxAt(stepped), dynamicColliders)) {
            Vec3 tryStep = stepped;
            tryStep.x += delta.x;
            if (collides(world, boxAt(tryStep), dynamicColliders)) tryStep.x = stepped.x;
            tryStep.z += delta.z;
            if (collides(world, boxAt(tryStep), dynamicColliders)) tryStep.z = stepped.z;

            float oldDistSq = (horizPos.x - position.x) * (horizPos.x - position.x) +
                              (horizPos.z - position.z) * (horizPos.z - position.z);
            float newDistSq = (tryStep.x - position.x) * (tryStep.x - position.x) +
                              (tryStep.z - position.z) * (tryStep.z - position.z);
            if (newDistSq > oldDistSq + 1e-6f) {
                horizPos = tryStep;
                float stepDown = cfg::STEP_HEIGHT;
                Vec3 probe = horizPos;
                probe.y -= stepDown;
                if (collides(world, boxAt(probe), dynamicColliders)) {
                    float lo = 0.0f, hi = stepDown;
                    for (int i = 0; i < 6; ++i) {
                        float mid = 0.5f * (lo + hi);
                        Vec3 p = horizPos;
                        p.y -= mid;
                        if (collides(world, boxAt(p), dynamicColliders)) hi = mid;
                        else lo = mid;
                    }
                    horizPos.y -= lo;
                }
                hitX = hitZ = false;
            }
        }
    }

    if (hitX) velocity.x = 0.0f;
    if (hitZ) velocity.z = 0.0f;
    position = horizPos;

    Vec3 vertPos = position;
    vertPos.y += delta.y;
    if (collides(world, boxAt(vertPos), dynamicColliders)) {
        if (delta.y < 0.0f) {
            grounded = true;
        }
        velocity.y = 0.0f;
    } else {
        position = vertPos;
        grounded = false;
    }
}

} // namespace mega
