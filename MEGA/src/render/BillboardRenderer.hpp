#pragma once

#include "core/Math3D.hpp"
#include "render/GlApi.hpp"
#include <vector>

namespace mega {

enum class SpriteType : int {
    EnemyStalker = 0,
    EnemyDrone   = 1,
    EnemyHeavy   = 2,
    PickupMedkit = 3,
    PickupOxygen = 4,
    PickupAmmo   = 5,
    PickupEnergy = 6,
    ExitBeacon   = 7,
    Projectile   = 8,
    Spark        = 9,
    COUNT
};

struct BillboardInstance {
    Vec3 pos;
    float width = 0.8f;
    float height = 1.2f;
    SpriteType sprite = SpriteType::EnemyStalker;
    Vec3 tint{1.0f, 1.0f, 1.0f};
};

class BillboardRenderer {
public:
    BillboardRenderer() = default;
    ~BillboardRenderer();

    bool init();
    void render(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                const std::vector<BillboardInstance>& instances);

private:
    GLuint texArray_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint program_ = 0;
};

} // namespace mega
