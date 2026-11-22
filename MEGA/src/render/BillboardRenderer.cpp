#include "render/BillboardRenderer.hpp"
#include "render/Shader.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mega {

namespace {

constexpr int SPR_SIZE = 24;

void setSprPx(std::vector<uint8_t>& buf, int x, int y,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= SPR_SIZE || y < 0 || y >= SPR_SIZE) return;
    size_t i = static_cast<size_t>((y * SPR_SIZE + x) * 4);
    buf[i + 0] = r;
    buf[i + 1] = g;
    buf[i + 2] = b;
    buf[i + 3] = a;
}

void drawDisc(std::vector<uint8_t>& buf, float cx, float cy, float rad,
              uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < SPR_SIZE; ++y) {
        for (int x = 0; x < SPR_SIZE; ++x) {
            float dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= rad * rad) {
                setSprPx(buf, x, y, r, g, b, 255);
            }
        }
    }
}

void drawRect(std::vector<uint8_t>& buf, int x0, int y0, int x1, int y1,
              uint8_t r, uint8_t g, uint8_t b) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            setSprPx(buf, x, y, r, g, b, 255);
}

} // namespace

BillboardRenderer::~BillboardRenderer() {
    if (texArray_) glDeleteTextures(1, &texArray_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
}

bool BillboardRenderer::init() {
    const int count = static_cast<int>(SpriteType::COUNT);
    glGenTextures(1, &texArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, SPR_SIZE, SPR_SIZE, count,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    std::vector<uint8_t> buf(SPR_SIZE * SPR_SIZE * 4, 0);
    auto upload = [&](SpriteType t) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<int>(t),
                        SPR_SIZE, SPR_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        std::fill(buf.begin(), buf.end(), 0);
    };

    // 0: Stalker (biomechanical crimson silhouette with glowing eyes)
    drawRect(buf, 8, 5, 15, 16, 110, 28, 34);
    drawDisc(buf, 11.5f, 5.0f, 4.0f, 130, 35, 42);
    drawRect(buf, 7, 16, 10, 23, 85, 22, 26);
    drawRect(buf, 13, 16, 16, 23, 85, 22, 26);
    drawRect(buf, 5, 8, 7, 15, 95, 25, 30);
    drawRect(buf, 16, 8, 18, 15, 95, 25, 30);
    setSprPx(buf, 10, 4, 255, 220, 60, 255);
    setSprPx(buf, 13, 4, 255, 220, 60, 255);
    upload(SpriteType::EnemyStalker);

    // 1: Drone (hovering steel sphere + cyan eye + thrusters)
    drawDisc(buf, 11.5f, 11.5f, 7.5f, 75, 85, 98);
    drawDisc(buf, 11.5f, 11.5f, 5.0f, 50, 58, 70);
    drawDisc(buf, 11.5f, 11.5f, 2.5f, 255, 70, 50);
    drawRect(buf, 3, 10, 5, 13, 140, 150, 165);
    drawRect(buf, 18, 10, 20, 13, 140, 150, 165);
    upload(SpriteType::EnemyDrone);

    // 2: Heavy Warden (armored industrial juggernaut)
    drawRect(buf, 5, 4, 18, 17, 72, 76, 84);
    drawRect(buf, 7, 2, 16, 6, 95, 100, 110);
    drawRect(buf, 6, 17, 10, 23, 55, 58, 65);
    drawRect(buf, 13, 17, 17, 23, 55, 58, 65);
    drawRect(buf, 9, 4, 14, 5, 255, 110, 30);
    drawRect(buf, 3, 7, 5, 15, 120, 95, 40);
    drawRect(buf, 18, 7, 20, 15, 120, 95, 40);
    upload(SpriteType::EnemyHeavy);

    // 3: Medkit (white box + red cross)
    drawRect(buf, 5, 8, 18, 20, 220, 225, 230);
    drawRect(buf, 10, 10, 13, 18, 220, 35, 35);
    drawRect(buf, 8, 12, 15, 16, 220, 35, 35);
    upload(SpriteType::PickupMedkit);

    // 4: Oxygen Canister (blue-cyan tank)
    drawRect(buf, 8, 6, 15, 21, 50, 180, 235);
    drawRect(buf, 9, 4, 14, 6, 180, 195, 210);
    drawRect(buf, 10, 10, 13, 17, 170, 240, 255);
    upload(SpriteType::PickupOxygen);

    // 5: Ammo Crate (olive/amber industrial box)
    drawRect(buf, 5, 10, 18, 21, 95, 105, 60);
    drawRect(buf, 6, 11, 17, 13, 215, 170, 45);
    upload(SpriteType::PickupAmmo);

    // 6: Energy Battery (yellow/cyan cell)
    drawRect(buf, 8, 7, 15, 20, 45, 55, 70);
    drawRect(buf, 9, 9, 14, 18, 255, 220, 50);
    upload(SpriteType::PickupEnergy);

    // 7: Exit Beacon (tall glowing portal pillar)
    drawRect(buf, 7, 1, 16, 23, 40, 220, 200);
    drawRect(buf, 9, 2, 14, 22, 170, 255, 245);
    upload(SpriteType::ExitBeacon);

    // 8: Projectile (plasma orb)
    drawDisc(buf, 11.5f, 11.5f, 5.0f, 255, 120, 40);
    drawDisc(buf, 11.5f, 11.5f, 2.5f, 255, 240, 160);
    upload(SpriteType::Projectile);

    // 9: Spark
    drawDisc(buf, 11.5f, 11.5f, 2.5f, 255, 220, 110);
    upload(SpriteType::Spark);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    Shader sh;
    const std::string vs = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aLayer;
layout (location = 3) in vec3 aTint;

out vec2 vUV;
out float vLayer;
out vec3 vTint;
out float vDist;

uniform mat4 uView;
uniform mat4 uProj;

void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vDist = length(vp.xyz);
    vUV = aUV;
    vLayer = aLayer;
    vTint = aTint;
    gl_Position = uProj * vp;
})";

    const std::string fs = R"(#version 330 core
in vec2 vUV;
in float vLayer;
in vec3 vTint;
in float vDist;
out vec4 FragColor;

uniform sampler2DArray uSprites;

void main() {
    vec4 tex = texture(uSprites, vec3(vUV, vLayer));
    if (tex.a < 0.5) discard;
    float fog = clamp((vDist - 12.0) / 56.0, 0.0, 1.0);
    vec3 col = mix(tex.rgb * vTint, vec3(0.03, 0.04, 0.05), fog * 0.75);
    FragColor = vec4(col, 1.0);
})";

    if (!sh.compile(vs, fs)) return false;
    // Transfer ownership of program ID manually or store Shader
    // Easier: keep Shader member? Let's just use glCreateProgram via Shader:
    // Wait: Shader destructor deletes program! So let's detach or store Shader in class.
    // Let's compile directly or store Shader! Let's fix by stealing program id? No, Shader has private program_.
    // Let's just compile inline cleanly here:
    auto compileStage = [](GLenum type, const std::string& src) -> GLuint {
        GLuint s = glCreateShader(type);
        const char* c = src.c_str();
        glShaderSource(s, 1, &c, nullptr);
        glCompileShader(s);
        return s;
    };
    GLuint v = compileStage(GL_VERTEX_SHADER, vs);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    struct SprVert { float x, y, z, u, v, layer, r, g, b; };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SprVert), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SprVert), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(SprVert), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(SprVert), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void BillboardRenderer::render(const Mat4& view, const Mat4& proj, const Vec3& camPos,
                               const std::vector<BillboardInstance>& instances) {
    if (instances.empty()) return;

    struct SprVert { float x, y, z, u, v, layer, r, g, b; };
    std::vector<SprVert> verts;
    verts.reserve(instances.size() * 6);

    for (const auto& inst : instances) {
        Vec3 toCam = {camPos.x - inst.pos.x, 0.0f, camPos.z - inst.pos.z};
        Vec3 fwd = normalize(toCam);
        if (lengthSq(fwd) < 1e-5f) fwd = {0.0f, 0.0f, 1.0f};
        Vec3 right = normalize(cross({0.0f, 1.0f, 0.0f}, fwd));

        float hw = inst.width * 0.5f;
        float h = inst.height;
        float layer = static_cast<float>(static_cast<int>(inst.sprite));

        Vec3 bl = inst.pos - right * hw;
        Vec3 br = inst.pos + right * hw;
        Vec3 tl = bl + Vec3{0.0f, h, 0.0f};
        Vec3 tr = br + Vec3{0.0f, h, 0.0f};

        SprVert v0{bl.x, bl.y, bl.z, 0.0f, 1.0f, layer, inst.tint.x, inst.tint.y, inst.tint.z};
        SprVert v1{br.x, br.y, br.z, 1.0f, 1.0f, layer, inst.tint.x, inst.tint.y, inst.tint.z};
        SprVert v2{tr.x, tr.y, tr.z, 1.0f, 0.0f, layer, inst.tint.x, inst.tint.y, inst.tint.z};
        SprVert v3{tl.x, tl.y, tl.z, 0.0f, 0.0f, layer, inst.tint.x, inst.tint.y, inst.tint.z};

        verts.push_back(v0); verts.push_back(v1); verts.push_back(v2);
        verts.push_back(v0); verts.push_back(v2); verts.push_back(v3);
    }

    glDisable(GL_CULL_FACE);
    glUseProgram(program_);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProj"), 1, GL_FALSE, proj.m);
    glUniform1i(glGetUniformLocation(program_, "uSprites"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(SprVert)),
                 verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

} // namespace mega
