#include "render/HudRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mega {

namespace {

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(r);
}

// Compact 3x5 pixel font for crisp retro HUD (ASCII 32..95)
struct Glyph3x5 { char ch; uint8_t rows[5]; };
constexpr Glyph3x5 FONT[] = {
    {' ', {0b000,0b000,0b000,0b000,0b000}},
    {'.', {0b000,0b000,0b000,0b000,0b010}},
    {',', {0b000,0b000,0b000,0b010,0b100}},
    {'_', {0b000,0b000,0b000,0b000,0b111}},
    {'!', {0b010,0b010,0b010,0b000,0b010}},
    {'=', {0b000,0b111,0b000,0b111,0b000}},
    {'-', {0b000,0b000,0b111,0b000,0b000}},
    {'+', {0b000,0b010,0b111,0b010,0b000}},
    {'>', {0b100,0b010,0b001,0b010,0b100}},
    {'(', {0b010,0b100,0b100,0b100,0b010}},
    {')', {0b010,0b001,0b001,0b001,0b010}},
    {':', {0b000,0b010,0b000,0b010,0b000}},
    {'/', {0b001,0b001,0b010,0b100,0b100}},
    {'[', {0b110,0b100,0b100,0b100,0b110}},
    {']', {0b011,0b001,0b001,0b001,0b011}},
    {'0', {0b111,0b101,0b101,0b101,0b111}},
    {'1', {0b010,0b110,0b010,0b010,0b111}},
    {'2', {0b111,0b001,0b111,0b100,0b111}},
    {'3', {0b111,0b001,0b111,0b001,0b111}},
    {'4', {0b101,0b101,0b111,0b001,0b001}},
    {'5', {0b111,0b100,0b111,0b001,0b111}},
    {'6', {0b111,0b100,0b111,0b101,0b111}},
    {'7', {0b111,0b001,0b001,0b010,0b010}},
    {'8', {0b111,0b101,0b111,0b101,0b111}},
    {'9', {0b111,0b101,0b111,0b001,0b111}},
    {'A', {0b010,0b101,0b111,0b101,0b101}},
    {'B', {0b110,0b101,0b110,0b101,0b110}},
    {'C', {0b111,0b100,0b100,0b100,0b111}},
    {'D', {0b110,0b101,0b101,0b101,0b110}},
    {'E', {0b111,0b100,0b110,0b100,0b111}},
    {'F', {0b111,0b100,0b110,0b100,0b100}},
    {'G', {0b111,0b100,0b101,0b101,0b111}},
    {'H', {0b101,0b101,0b111,0b101,0b101}},
    {'I', {0b111,0b010,0b010,0b010,0b111}},
    {'J', {0b001,0b001,0b001,0b101,0b111}},
    {'K', {0b101,0b101,0b110,0b101,0b101}},
    {'L', {0b100,0b100,0b100,0b100,0b111}},
    {'M', {0b101,0b111,0b111,0b101,0b101}},
    {'N', {0b101,0b111,0b111,0b111,0b101}},
    {'O', {0b111,0b101,0b101,0b101,0b111}},
    {'P', {0b111,0b101,0b111,0b100,0b100}},
    {'Q', {0b111,0b101,0b101,0b111,0b001}},
    {'R', {0b111,0b101,0b110,0b101,0b101}},
    {'S', {0b111,0b100,0b111,0b001,0b111}},
    {'T', {0b111,0b010,0b010,0b010,0b010}},
    {'U', {0b101,0b101,0b101,0b101,0b111}},
    {'V', {0b101,0b101,0b101,0b101,0b010}},
    {'W', {0b101,0b101,0b111,0b111,0b101}},
    {'X', {0b101,0b101,0b010,0b101,0b101}},
    {'Y', {0b101,0b101,0b010,0b010,0b010}},
    {'Z', {0b111,0b001,0b010,0b100,0b111}},
};

const Glyph3x5* findGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const auto& g : FONT) {
        if (g.ch == c) return &g;
    }
    return &FONT[0];
}

} // namespace

HudRenderer::~HudRenderer() {
    if (tex_) glDeleteTextures(1, &tex_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
}

bool HudRenderer::init(int width, int height) {
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<size_t>(width_ * height_), 0);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    auto compileStage = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    const char* vs = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); })";
    const char* fs = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uHud;
void main() {
    vec4 c = texture(uHud, vUV);
    if (c.a < 0.05) discard;
    FragColor = c;
})";

    GLuint v = compileStage(GL_VERTEX_SHADER, vs);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    return true;
}

void HudRenderer::clear() {
    std::fill(pixels_.begin(), pixels_.end(), 0u);
}

void HudRenderer::putPixel(int x, int y, uint32_t col) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    pixels_[static_cast<size_t>(y * width_ + x)] = col;
}

void HudRenderer::fillRect(int x, int y, int w, int h, uint32_t col) {
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            putPixel(px, py, col);
}

void HudRenderer::drawLine(int x0, int y0, int x1, int y1, uint32_t col, int thickness) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int r = thickness / 2;
    while (true) {
        fillRect(x0 - r, y0 - r, thickness, thickness, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void HudRenderer::drawChar(int x, int y, char c, uint32_t col, int scale) {
    const Glyph3x5* g = findGlyph(c);
    for (int r = 0; r < 5; ++r) {
        for (int colBit = 0; colBit < 3; ++colBit) {
            if (g->rows[r] & (1 << (2 - colBit))) {
                fillRect(x + colBit * scale, y + r * scale, scale, scale, col);
            }
        }
    }
}

void HudRenderer::drawText(int x, int y, const char* text, uint32_t col, int scale) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        drawChar(cx, y, *p, col, scale);
        cx += 4 * scale;
    }
}

void HudRenderer::drawBar(int x, int y, int w, int h, float pct, uint32_t fillCol) {
    fillRect(x, y, w, h, rgba(16, 20, 26, 210));
    int fw = static_cast<int>(std::clamp(pct, 0.0f, 1.0f) * (w - 2));
    if (fw > 0) {
        fillRect(x + 1, y + 1, fw, h - 2, fillCol);
    }
}

void HudRenderer::presentPixels() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glUniform1i(glGetUniformLocation(program_, "uHud"), 0);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void HudRenderer::drawMinimap(const GameWorld& gw) {
    const int mapW = 72;
    const int mapH = 72;
    const int ox = width_ - mapW - 6;
    const int oy = 6;

    fillRect(ox - 1, oy - 1, mapW + 2, mapH + 2, rgba(20, 26, 32, 210));

    bool asMonster = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
    Vec3 centerPos = asMonster ? gw.monster.pos : gw.player.position;
    int px = static_cast<int>(std::floor(centerPos.x));
    int py = static_cast<int>(std::floor(centerPos.y + 0.5f));
    int pz = static_cast<int>(std::floor(centerPos.z));

    int half = mapW / 2;
    for (int dy = 0; dy < mapH; ++dy) {
        for (int dx = 0; dx < mapW; ++dx) {
            int wx = px + (dx - half);
            int wz = pz + (dy - half);
            bool wall = gw.map.world.getCell(wx, py + 1, wz).isOpaque();
            bool floor = gw.map.world.getCell(wx, py - 1, wz).isSolid();
            if (wall) {
                putPixel(ox + dx, oy + dy, rgba(85, 100, 115, 230));
            } else if (floor) {
                putPixel(ox + dx, oy + dy, rgba(28, 34, 42, 210));
            }
        }
    }

    // In Solo mode, show AI enemies; in Online 1v1 mode, hide opponent so suspense/lockers work!
    if (!gw.onlineMode) {
        for (const auto& e : gw.enemies) {
            if (!e.alive) continue;
            int ex = static_cast<int>(std::floor(e.pos.x)) - px + half;
            int ez = static_cast<int>(std::floor(e.pos.z)) - pz + half;
            if (ex >= 0 && ex < mapW && ez >= 0 && ez < mapH) {
                fillRect(ox + ex - 1, oy + ez - 1, 2, 2, rgba(255, 65, 65, 255));
            }
        }
        int bx = static_cast<int>(std::floor(gw.map.exitPos.x)) - px + half;
        int bz = static_cast<int>(std::floor(gw.map.exitPos.z)) - pz + half;
        bx = std::clamp(bx, 1, mapW - 2);
        bz = std::clamp(bz, 1, mapH - 2);
        fillRect(ox + bx - 1, oy + bz - 1, 3, 3, rgba(60, 255, 235, 255));
    }

    // Self center dot
    uint32_t selfCol = asMonster ? rgba(255, 65, 65, 255) : rgba(255, 240, 80, 255);
    fillRect(ox + half - 1, oy + half - 1, 2, 2, selfCol);
}

void HudRenderer::renderMenu(const LobbyMenuState& menu) {
    clear();
    fillRect(0, 0, width_, height_, rgba(6, 10, 14, 215));

    int cx = width_ / 2;
    int cy = height_ / 2;
    int bw = 600, bh = 356;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;

    fillRect(bx, by, bw, bh, rgba(14, 19, 26, 245));
    fillRect(bx, by, bw, 3, rgba(75, 215, 200, 245));
    fillRect(bx, by + bh - 3, bw, 3, rgba(75, 215, 200, 245));

    drawText(bx + 24, by + 16, "MEGA // 1V1 ASYMMETRIC MULTIPLAYER", rgba(110, 240, 225, 255), 3);
    drawText(bx + 24, by + 42, "HUMAN SURVIVOR VS MONSTER STALKER (P2P NO VPN)", rgba(155, 180, 200, 220), 2);

    auto drawOption = [&](int idx, int y, const char* label, uint32_t col = rgba(220, 230, 240, 245)) {
        bool sel = (menu.cursor == idx);
        fillRect(bx + 28, y - 6, bw - 56, 28, sel ? rgba(36, 70, 82, 240) : rgba(20, 28, 36, 210));
        if (sel) fillRect(bx + 28, y - 6, 4, 28, rgba(95, 245, 220, 255));
        drawText(bx + 42, y + 2, label, sel ? rgba(255, 250, 175, 255) : col, 2);
    };

    if (menu.screen == 0) {
        drawOption(0, by + 92,  "HOST 1V1 MATCH (DIRECT P2P)");
        drawOption(1, by + 132, "JOIN 1V1 MATCH (CODE OR IP)");
        drawOption(2, by + 172, "SOLO PRACTICE (OFFLINE)");
        if (menu.activeMatch) {
            drawOption(3, by + 212, "RESUME MATCH", rgba(150, 245, 175, 245));
        }
        drawText(bx + 28, by + bh - 32, "[UP/DOWN] SELECT  [ENTER] CONFIRM  [F1] MENU", rgba(150, 175, 195, 220), 2);
    } else if (menu.screen == 1) {
        char infoBuf[128];
        std::snprintf(infoBuf, sizeof(infoBuf), "ROOM CODE: %s  (%s)", menu.myRoomCodeStr.c_str(), menu.natStatusStr.c_str());
        drawText(bx + 28, by + 68, infoBuf, rgba(130, 245, 185, 250), 2);
        char lanBuf[96];
        std::snprintf(lanBuf, sizeof(lanBuf), "LAN IP: %s", menu.localIpStr.c_str());
        drawText(bx + 28, by + 88, lanBuf, rgba(155, 200, 225, 220), 2);

        char roleBuf[64];
        std::snprintf(roleBuf, sizeof(roleBuf), "YOUR ROLE: < %s >",
                      menu.hostRole == PlayerRole::Human ? "HUMAN" : "MONSTER");
        drawOption(0, by + 118, roleBuf);

        char portBuf[64];
        std::snprintf(portBuf, sizeof(portBuf), "UDP PORT: %s%s", menu.hostPortStr.c_str(), menu.cursor == 1 ? "_" : "");
        drawOption(1, by + 152, portBuf);

        char guestBuf[96];
        std::snprintf(guestBuf, sizeof(guestBuf), "GUEST CODE (IF NO UPNP): %s%s",
                      menu.hostGuestCodeStr.empty() ? (menu.cursor == 2 ? "_" : "OPTIONAL") : menu.hostGuestCodeStr.c_str(),
                      (!menu.hostGuestCodeStr.empty() && menu.cursor == 2) ? "_" : "");
        drawOption(2, by + 186, guestBuf);

        drawOption(3, by + 220, "START HOSTING", rgba(120, 255, 185, 255));
        drawOption(4, by + 254, "BACK");
        drawText(bx + 28, by + bh - 32, "[L/R] ROLE  [TYPE] PORT/GUEST  [ENTER] START", rgba(150, 175, 195, 220), 2);
    } else if (menu.screen == 2) {
        char myBuf[128];
        std::snprintf(myBuf, sizeof(myBuf), "MY GUEST CODE: %s", menu.myRoomCodeStr.c_str());
        drawText(bx + 28, by + 72, myBuf, rgba(130, 245, 185, 250), 2);

        char ipBuf[96];
        std::snprintf(ipBuf, sizeof(ipBuf), "HOST CODE OR IP: %s%s",
                      menu.joinIpStr.c_str(), menu.cursor == 0 ? "_" : "");
        drawOption(0, by + 106, ipBuf);

        char portBuf[64];
        std::snprintf(portBuf, sizeof(portBuf), "PORT (IF LAN IP): %s%s",
                      menu.joinPortStr.c_str(), menu.cursor == 1 ? "_" : "");
        drawOption(1, by + 142, portBuf);

        drawOption(2, by + 178, "CONNECT TO HOST", rgba(120, 255, 185, 255));
        drawOption(3, by + 214, "BACK");
        drawText(bx + 28, by + bh - 32, "[TYPE] ROOM CODE OR IP  [TAB] NEXT  [ENTER] JOIN", rgba(150, 175, 195, 220), 2);
    }

    if (!menu.statusMessage.empty()) {
        drawText(bx + 28, by + bh - 56, menu.statusMessage.c_str(), rgba(255, 165, 110, 255), 2);
    }

    presentPixels();
}

void HudRenderer::render(const GameWorld& gw, NetMode netMode, bool peerConnected) {
    clear();

    bool asMonster = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
    float dmgFlash = asMonster ? gw.monster.hurtFlash : gw.damageFlash;

    // Damage vignette border
    if (dmgFlash > 0.01f) {
        uint8_t a = static_cast<uint8_t>(std::min(195.0f, dmgFlash * 255.0f));
        uint32_t red = rgba(215, 25, 25, a);
        fillRect(0, 0, width_, 8, red);
        fillRect(0, height_ - 8, width_, 8, red);
        fillRect(0, 0, 8, height_, red);
        fillRect(width_ - 8, 0, 8, height_, red);
    }

    int cx = width_ / 2;
    int cy = height_ / 2;

    if (asMonster) {
        // =====================================================================
        // MONSTER HUD (Claws, Dash, Sprint, Locker/Door Check, Night Vision)
        // =====================================================================

        if (gw.monster.grabbedDoorIdx >= 0) {
            fillRect(cx - 5, cy - 5, 10, 10, rgba(255, 110, 95, 210));
            drawText(cx - 56, cy + 16, "DRAG DOOR / LOCKER", rgba(255, 150, 130, 235), 2);
        } else if (gw.monster.hoveredDoorIdx >= 0 &&
                   gw.monster.hoveredDoorIdx < static_cast<int>(gw.doors.size())) {
            const auto& hd = gw.doors[gw.monster.hoveredDoorIdx];
            if (hd.locked) {
                fillRect(cx - 4, cy - 4, 8, 8, rgba(235, 75, 65, 220));
                drawText(cx - 32, cy + 16, "LOCKED", rgba(245, 105, 95, 240), 2);
            } else if (hd.isLocker) {
                fillRect(cx - 4, cy - 4, 8, 8, rgba(110, 255, 155, 210));
                drawText(cx - 96, cy + 16, "[E/LMB] CHECK LOCKER", rgba(130, 255, 175, 240), 2);
            } else {
                fillRect(cx - 4, cy - 4, 8, 8, rgba(255, 170, 110, 200));
                drawText(cx - 64, cy + 16, "[E] OPEN DOOR", rgba(255, 195, 140, 235), 2);
            }
        } else {
            // Monster predator crosshair
            fillRect(cx - 4, cy, 3, 1, rgba(255, 95, 95, 180));
            fillRect(cx + 2, cy, 3, 1, rgba(255, 95, 95, 180));
            fillRect(cx, cy - 4, 1, 3, rgba(255, 95, 95, 180));
            fillRect(cx, cy + 2, 1, 3, rgba(255, 95, 95, 180));
        }

        // Bottom-left Monster abilities & health
        int bx = 18;
        int by = height_ - 98;
        fillRect(bx - 8, by - 8, 236, 90, rgba(14, 10, 14, 185));

        drawText(bx, by + 2, "HP", rgba(255, 120, 120, 235), 2);
        drawBar(bx + 68, by, 148, 12, gw.monster.hp / std::max(1.0f, gw.monster.maxHp), rgba(220, 50, 55, 240));

        drawText(bx, by + 20, "STM", rgba(245, 195, 110, 235), 2);
        drawBar(bx + 68, by + 18, 148, 12, gw.monster.stamina / std::max(1.0f, gw.monster.maxStamina), rgba(225, 155, 50, 235));

        float dashPct = 1.0f - std::clamp(gw.monster.dashCooldown / 3.2f, 0.0f, 1.0f);
        drawText(bx, by + 38, "DASH", rgba(130, 230, 255, 235), 2);
        drawBar(bx + 68, by + 36, 148, 12, dashPct,
                dashPct >= 0.99f ? rgba(90, 245, 225, 245) : rgba(65, 120, 145, 210));

        drawText(bx, by + 58,
                 gw.monster.nightVision ? "NIGHT VISION [N]: ON" : "NIGHT VISION [N]: OFF",
                 gw.monster.nightVision ? rgba(110, 255, 130, 245) : rgba(155, 165, 175, 215), 2);

        // Bottom-right controls bar
        const char* mHint = "[LMB] CLAW  [RMB/SPACE] DASH  [SHIFT] SPRINT  [E] LOCKER  [N] NVG";
        int hw = static_cast<int>(std::strlen(mHint)) * 8;
        fillRect(width_ - hw - 26, height_ - 36, hw + 18, 24, rgba(16, 12, 16, 200));
        drawText(width_ - hw - 18, height_ - 29, mHint, rgba(255, 190, 170, 235), 2);
    } else {
        // =====================================================================
        // HUMAN HUD (Survival, 3D Backpack, Lockpick, Crafting, Melee — NO GUNS)
        // =====================================================================
        if (gw.lockpick.active) {
            drawLockpickMinigame(gw);
        } else if (gw.draggingItem) {
            Int3 sz = gw.dragItemOrient.applyToSize(itemBaseGridSize(gw.dragItemType));
            char dbuf[96];
            std::snprintf(dbuf, sizeof(dbuf), "DRAG %s %dX%dX%d  [LMB] PLACE  [R/T/Y] ROTATE  [RMB] CANCEL",
                          itemDisplayName(gw.dragItemType), sz.x, sz.y, sz.z);
            int dw = static_cast<int>(std::strlen(dbuf)) * 8;
            fillRect(cx - dw / 2 - 10, 44, dw + 20, 24, rgba(12, 20, 28, 220));
            drawText(cx - dw / 2, 51, dbuf, rgba(140, 235, 255, 245), 2);
        } else if (gw.hasHeldItem && gw.heldPlaceMode) {
            Int3 sz = gw.heldItemOrient.applyToSize(itemBaseGridSize(gw.heldItemType));
            char pbuf[112];
            std::snprintf(pbuf, sizeof(pbuf), "PLACE %s %dX%dX%d  [LMB] CONFIRM  [R/T/Y] ROTATE  [RMB] CANCEL",
                          itemDisplayName(gw.heldItemType), sz.x, sz.y, sz.z);
            int pw = static_cast<int>(std::strlen(pbuf)) * 8;
            fillRect(cx - pw / 2 - 10, 44, pw + 20, 24, rgba(12, 22, 26, 220));
            drawText(cx - pw / 2, 51, pbuf, rgba(140, 245, 220, 245), 2);
            fillRect(cx - 2, cy - 2, 5, 5, rgba(120, 240, 220, 195));
        } else if (gw.hoveredBackpackItemId >= 0) {
            const PackedItem* pi = gw.backpack.findById(gw.hoveredBackpackItemId);
            if (pi) {
                Int3 sz = pi->size();
                char bbuf[96];
                if (gw.hoveredBackpackBlocked) {
                    std::snprintf(bbuf, sizeof(bbuf), "%s %dX%dX%d - BLOCKED ABOVE",
                                  itemDisplayName(pi->type), sz.x, sz.y, sz.z);
                    int bw = static_cast<int>(std::strlen(bbuf)) * 8;
                    fillRect(cx - bw / 2 - 8, cy + 16, bw + 16, 22, rgba(35, 12, 12, 225));
                    drawText(cx - bw / 2, cy + 22, bbuf, rgba(255, 105, 95, 245), 2);
                } else {
                    std::snprintf(bbuf, sizeof(bbuf), "%s %dX%dX%d  [LMB] HAND  [RMB] DRAG",
                                  itemDisplayName(pi->type), sz.x, sz.y, sz.z);
                    int bw = static_cast<int>(std::strlen(bbuf)) * 8;
                    fillRect(cx - bw / 2 - 8, cy + 16, bw + 16, 22, rgba(12, 20, 28, 220));
                    drawText(cx - bw / 2, cy + 22, bbuf, rgba(185, 235, 255, 245), 2);
                }
            }
        } else if (gw.grabbedDoorIdx >= 0) {
            fillRect(cx - 5, cy - 5, 10, 10, rgba(130, 215, 255, 190));
            drawText(cx - 56, cy + 16, "DRAG MOUSE", rgba(160, 225, 255, 230), 2);
        } else if (gw.hoveredDoorIdx >= 0) {
            const auto& hd = gw.doors[gw.hoveredDoorIdx];
            if (hd.locked) {
                if (gw.inventory.lockpicks > 0) {
                    fillRect(cx - 4, cy - 4, 8, 8, rgba(255, 175, 85, 210));
                    char pbuf[64];
                    std::snprintf(pbuf, sizeof(pbuf), "[LMB] PICK LOCK [X%d]", gw.inventory.lockpicks);
                    drawText(cx - 88, cy + 16, pbuf, rgba(255, 200, 120, 240), 2);
                } else {
                    fillRect(cx - 4, cy - 4, 8, 8, rgba(235, 75, 65, 220));
                    drawText(cx - 88, cy + 16, "LOCKED - NEED LOCKPICK", rgba(245, 105, 95, 240), 2);
                }
            } else {
                fillRect(cx - 4, cy - 4, 8, 8, rgba(160, 225, 255, 170));
                const char* label = hd.isCabinet ? "[LMB DRAG] CABINET" : "[LMB DRAG] DOOR";
                drawText(cx - 72, cy + 16, label, rgba(210, 230, 245, 230), 2);
            }
        } else if (gw.hoveredItemIdx >= 0) {
            fillRect(cx - 4, cy - 4, 8, 8, rgba(255, 230, 120, 190));
            ItemType itT = gw.worldItems[gw.hoveredItemIdx].type;
            Int3 bsz = itemBaseGridSize(itT);
            char ibuf[64];
            std::snprintf(ibuf, sizeof(ibuf), "[LMB] %s %dX%dX%d", itemDisplayName(itT), bsz.x, bsz.y, bsz.z);
            drawText(cx - 72, cy + 16, ibuf, rgba(255, 235, 150, 235), 2);
        } else if (gw.hoveredGroundedBackpack) {
            fillRect(cx - 4, cy - 4, 8, 8, rgba(145, 225, 175, 210));
            drawText(cx - 104, cy + 16, "[B] PICK UP BACKPACK  [LMB] PACK", rgba(165, 235, 190, 240), 2);
        } else if (!gw.isCameraPausedForInventory()) {
            fillRect(cx - 1, cy - 1, 3, 3, rgba(200, 220, 240, 140));
        }

        if (gw.backpackMode == BackpackMode::InHands && !gw.draggingItem) {
            const char* bpHint = "PACK 6X8X4 + REAR 2X30X6  [HOLD RMB] SPIN  [LMB] HAND  [RMB] DRAG  [B] DROP";
            int bw = static_cast<int>(std::strlen(bpHint)) * 8;
            fillRect(cx - bw / 2 - 10, 44, bw + 20, 24, rgba(12, 18, 26, 215));
            drawText(cx - bw / 2, 51, bpHint, rgba(180, 225, 250, 240), 2);
        }

        if (!gw.availableCrafts.empty() && !gw.lockpick.active && gw.backpackMode != BackpackMode::InHands) {
            int maxLen = 22;
            for (const auto& c : gw.availableCrafts) {
                maxLen = std::max(maxLen, static_cast<int>(c.label.size()));
            }
            int cw = maxLen * 8 + 24;
            int ch = 26 + static_cast<int>(gw.availableCrafts.size()) * 20 + 8;
            int cbx = cx - cw / 2;
            int cby = cy + 46;
            fillRect(cbx, cby, cw, ch, rgba(12, 22, 26, 225));
            fillRect(cbx, cby, cw, 2, rgba(95, 225, 195, 240));
            fillRect(cbx, cby + ch - 2, cw, 2, rgba(95, 225, 195, 240));
            drawText(cbx + 12, cby + 8, "SURFACE CRAFTING", rgba(115, 245, 210, 245), 2);
            for (size_t i = 0; i < gw.availableCrafts.size(); ++i) {
                drawText(cbx + 12, cby + 28 + static_cast<int>(i) * 20,
                         gw.availableCrafts[i].label.c_str(), rgba(245, 235, 165, 245), 2);
            }
        }

        {
            char hbuf[112];
            if (gw.hasHeldItem) {
                Int3 sz = gw.heldItemOrient.applyToSize(itemBaseGridSize(gw.heldItemType));
                if (isMeleeWeapon(gw.heldItemType)) {
                    if ((gw.heldItemType == ItemType::Broom || gw.heldItemType == ItemType::Mop) && gw.heldItemDurability > 0) {
                        std::snprintf(hbuf, sizeof(hbuf), "HAND: %s %dX%dX%d (HITS:%d)  [LMB] STRIKE  [RMB] PLACE  [R/T/Y] ROT",
                                      itemDisplayName(gw.heldItemType), sz.x, sz.y, sz.z, gw.heldItemDurability);
                    } else {
                        std::snprintf(hbuf, sizeof(hbuf), "HAND: %s %dX%dX%d  [LMB] STRIKE  [RMB] PLACE  [R/T/Y] ROT",
                                      itemDisplayName(gw.heldItemType), sz.x, sz.y, sz.z);
                    }
                } else {
                    std::snprintf(hbuf, sizeof(hbuf), "HAND: %s %dX%dX%d  [E] USE  [RMB] PLACE  [R/T/Y] ROT",
                                  itemDisplayName(gw.heldItemType), sz.x, sz.y, sz.z);
                }
                int hw = static_cast<int>(std::strlen(hbuf)) * 8;
                fillRect(width_ - hw - 26, height_ - 36, hw + 18, 24, rgba(12, 18, 24, 210));
                drawText(width_ - hw - 18, height_ - 29, hbuf, rgba(255, 225, 140, 240), 2);
            } else {
                const char* modeStr = (gw.backpackMode == BackpackMode::OnGround) ? "GROUND" :
                                      (gw.backpackMode == BackpackMode::InHands) ? "HANDS" : "BACK";
                std::snprintf(hbuf, sizeof(hbuf), "PACK:%s  ITEMS:%d  PICKS:%d  [TAB] OPEN  [B] DROP",
                              modeStr, static_cast<int>(gw.backpack.items.size()), gw.inventory.lockpicks);
                int hw = static_cast<int>(std::strlen(hbuf)) * 8;
                fillRect(width_ - hw - 26, height_ - 36, hw + 18, 24, rgba(12, 16, 22, 185));
                drawText(width_ - hw - 18, height_ - 29, hbuf, rgba(175, 210, 230, 230), 2);
            }
        }

        // Bottom-left survival bars
        int bx = 18;
        int by = height_ - 116;
        fillRect(bx - 8, by - 8, 218, 108, rgba(10, 14, 18, 175));

        drawText(bx, by + 2,  "HP",  rgba(220, 225, 235, 230), 2);
        drawBar(bx + 68, by, 130, 12, gw.health / std::max(1.0f, gw.settings.maxHealth), rgba(205, 55, 55, 230));

        drawText(bx, by + 20, "FOOD", rgba(220, 190, 130, 230), 2);
        drawBar(bx + 68, by + 18, 130, 12, gw.hunger / std::max(1.0f, gw.settings.maxHunger), rgba(210, 145, 55, 230));

        drawText(bx, by + 38, "H2O",  rgba(120, 200, 245, 230), 2);
        drawBar(bx + 68, by + 36, 130, 12, gw.thirst / std::max(1.0f, gw.settings.maxThirst), rgba(65, 165, 230, 230));

        drawText(bx, by + 56, "NRG",  rgba(165, 230, 145, 230), 2);
        drawBar(bx + 68, by + 54, 130, 12, gw.energy / std::max(1.0f, gw.settings.maxEnergy), rgba(110, 205, 95, 230));

        if (gw.inventory.hasFlashlight) {
            drawText(bx, by + 76, "LAMP", rgba(240, 235, 145, 230), 2);
            drawBar(bx + 68, by + 74, 130, 12, gw.inventory.flashlightBattery / std::max(1.0f, gw.settings.maxBattery),
                    gw.flashlightOn ? rgba(180, 235, 255, 240) : rgba(110, 135, 155, 210));
        } else {
            drawText(bx, by + 76, "NO FLASHLIGHT", rgba(140, 150, 165, 200), 2);
        }
    }

    if (gw.hudNoticeTimer > 0.0f && !gw.hudNoticeText.empty()) {
        int textW = static_cast<int>(gw.hudNoticeText.size()) * 8;
        int nx = cx - textW / 2;
        int ny = cy - 165;
        fillRect(nx - 10, ny - 6, textW + 20, 22, rgba(24, 14, 14, 215));
        drawText(nx, ny, gw.hudNoticeText.c_str(), rgba(255, 145, 110, 245), 2);
    }

    if (gw.showMinimap) {
        drawMinimap(gw);
    }

    // Top-left Online Role & Peer Connection banner OR God Mode banner
    if (gw.onlineMode) {
        char obuf[96];
        std::snprintf(obuf, sizeof(obuf), "1V1 %s  |  %s  |  [F1] MENU",
                      gw.localRole == PlayerRole::Monster ? "MONSTER" : "HUMAN",
                      peerConnected ? "PEER ONLINE" : (netMode == NetMode::Host ? "WAITING PEER..." : "CONNECTING..."));
        int ow = static_cast<int>(std::strlen(obuf)) * 8;
        fillRect(10, 10, ow + 16, 24, rgba(14, 18, 24, 210));
        fillRect(10, 10, ow + 16, 2,
                 peerConnected ? rgba(95, 245, 175, 235) : rgba(245, 185, 75, 235));
        drawText(18, 17, obuf,
                 peerConnected ? rgba(155, 245, 195, 245) : rgba(255, 210, 120, 245), 2);
    } else if (gw.godMode || gw.godNoclip || gw.godInvisible || gw.godSpeedMult > 1.01f || gw.godTimeScale > 1.01f) {
        char gbuf[128];
        std::snprintf(gbuf, sizeof(gbuf), "GOD:%s  NOCLIP:%s  INVIS:%s  SPD:X%d  TIME:X%d",
                      gw.godMode ? "ON" : "OFF",
                      gw.godNoclip ? "ON" : "OFF",
                      gw.godInvisible ? "ON" : "OFF",
                      static_cast<int>(std::round(gw.godSpeedMult)),
                      static_cast<int>(std::round(gw.godTimeScale)));
        int gwPx = static_cast<int>(std::strlen(gbuf)) * 8;
        fillRect(10, 10, gwPx + 16, 24, rgba(16, 24, 34, 210));
        fillRect(10, 10, gwPx + 16, 2, rgba(95, 225, 180, 235));
        drawText(18, 17, gbuf, rgba(120, 245, 195, 245), 2);
    }

    if (gw.gameOver) {
        fillRect(cx - 190, cy - 48, 380, 96, rgba(16, 10, 12, 238));
        if (gw.onlineMode) {
            const char* title = (gw.matchPhase == MatchPhase::HumanWon)
                                    ? "HUMAN WINS - MONSTER DEFEATED!"
                                    : "MONSTER WINS - HUMAN HUNTED!";
            uint32_t col = (gw.matchPhase == MatchPhase::HumanWon)
                               ? rgba(105, 245, 175, 255)
                               : rgba(255, 75, 75, 255);
            int tw = static_cast<int>(std::strlen(title)) * 8;
            drawText(cx - tw / 2, cy - 22, title, col, 2);
            const char* sub = (netMode == NetMode::Host) ? "PRESS R FOR REMATCH" : "WAITING FOR HOST REMATCH";
            int sw = static_cast<int>(std::strlen(sub)) * 8;
            drawText(cx - sw / 2, cy + 12, sub, rgba(225, 230, 235, 240), 2);
        } else {
            drawText(cx - 88, cy - 22, "SIGNAL LOST", rgba(255, 70, 70), 2);
            drawText(cx - 128, cy + 8, "PRESS R TO RESTART", rgba(230, 230, 230), 2);
        }
    }

    presentPixels();
}

void HudRenderer::drawLockpickMinigame(const GameWorld& gw) {
    const auto& lp = gw.lockpick;
    int cx = width_ / 2;
    int cy = height_ / 2;

    int bw = 460, bh = 264;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;
    fillRect(bx, by, bw, bh, rgba(12, 16, 22, 232));
    fillRect(bx, by, bw, 2, rgba(105, 145, 180, 240));
    fillRect(bx, by + bh - 2, bw, 2, rgba(105, 145, 180, 240));
    char hdr[64];
    if (lp.breakChance <= 0.0001f) {
        std::snprintf(hdr, sizeof(hdr), "LOCKPICK  PICKS:%d  SAFE  [ESC]", gw.inventory.lockpicks);
    } else {
        std::snprintf(hdr, sizeof(hdr), "LOCKPICK  PICKS:%d  [ESC EXIT]", gw.inventory.lockpicks);
    }
    drawText(bx + 18, by + 12, hdr, rgba(185, 225, 250, 240), 2);

    int chLeft = bx + 95;
    int chW = 290;
    int chamberTop = by + 46;
    int chamberBot = by + 186; // chamberH = 140px
    int chamberH = chamberBot - chamberTop;
    int chTop = chamberBot;
    int chBot = chamberBot + 42; // Compact 42px keyway channel
    fillRect(chLeft, chTop, chW, chBot - chTop, rgba(24, 30, 38, 240));
    fillRect(chLeft, chTop, chW, 2, rgba(120, 135, 150, 240));
    fillRect(chLeft, chBot - 2, chW, 2, rgba(120, 135, 150, 240));

    int pinH = 22;
    int travelH = chamberH - pinH - 18; // 100px -> 20% = 20px!

    for (int i = 0; i < 3; ++i) {
        const auto& pin = lp.pins[i];
        int camX = chLeft + 34 + i * 92;
        int camW = 54;

        fillRect(camX, chamberTop, camW, chamberH, rgba(20, 25, 32, 240));
        fillRect(camX, chamberTop, 2, chamberH, rgba(110, 125, 140, 240));
        fillRect(camX + camW - 2, chamberTop, 2, chamberH, rgba(110, 125, 140, 240));
        fillRect(camX, chamberTop, camW, 2, rgba(110, 125, 140, 240));

        int targetY = chamberBot - pinH - static_cast<int>(pin.target * travelH);
        uint32_t targetCol = pin.pinned ? rgba(95, 245, 145, 240) : rgba(235, 195, 75, 200);
        fillRect(camX - 4, targetY, camW + 8, 2, targetCol);

        int pinY = chamberBot - pinH - static_cast<int>(pin.pos * travelH);
        uint32_t pinCol = pin.pinned
                              ? rgba(85, 225, 130, 255)
                              : (pin.brokenTimer > 0.0f ? rgba(235, 85, 75, 255) : rgba(165, 180, 195, 255));
        fillRect(camX + 8, pinY, camW - 16, pinH, pinCol);

        int sprTop = chamberTop + 4;
        int sprBot = pinY - 2;
        int coils = 6;
        uint32_t sprCol = (pin.brokenTimer > 0.0f) ? rgba(255, 110, 85, 255) : rgba(190, 205, 220, 240);

        if (pin.brokenTimer > 0.0f) {
            int midY = (sprTop + sprBot) / 2;
            int kinkX = static_cast<int>(pin.kinkDir * 14.0f);
            drawLine(camX + camW / 2, sprTop, camX + camW / 2 + kinkX, midY - 4, sprCol, 2);
            drawLine(camX + camW / 2 - kinkX / 2, midY + 4, camX + camW / 2, sprBot, sprCol, 2);
            fillRect(camX + 10, midY - 6, 6, 3, sprCol);
            fillRect(camX + camW - 16, midY + 5, 6, 3, sprCol);
        } else {
            int prevX = camX + camW / 2;
            int prevY = sprTop;
            for (int c = 1; c <= coils; ++c) {
                float frac = static_cast<float>(c) / static_cast<float>(coils);
                int waveOff = (c >= 1 && c <= 4) ? static_cast<int>(pin.coilWave[c - 1] * 26.0f) : 0;
                int ny = sprTop + static_cast<int>((sprBot - sprTop) * frac) + waveOff;
                ny = std::clamp(ny, sprTop, sprBot + 4);
                int nx = (c == coils) ? (camX + camW / 2)
                                      : (camX + camW / 2 + ((c % 2 == 1) ? 13 : -13));
                drawLine(prevX, prevY, nx, ny, sprCol, 2);
                prevX = nx;
                prevY = ny;
            }
        }
    }

    // Rigid L-shaped pick with tip = 20% of spring chamber height (28px of 140px)
    constexpr int FIXED_TIP_LEN = 28;
    int tipX = chLeft + 34 + static_cast<int>((lp.pickX - 0.5f) * 92.0f) + 27;
    int tipY = chamberBot - static_cast<int>(lp.pickY * travelH);
    int shaftY = tipY + FIXED_TIP_LEN;
    int handleX = bx + 22;

    uint32_t pickCol = rgba(220, 230, 240, 255);
    fillRect(handleX, shaftY - 4, 42, 9, rgba(80, 95, 110, 255));
    drawLine(handleX + 42, shaftY, tipX, shaftY, pickCol, 3);
    drawLine(tipX, shaftY, tipX, tipY, pickCol, 3);
}

} // namespace mega
