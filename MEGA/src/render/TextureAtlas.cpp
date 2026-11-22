#include "render/TextureAtlas.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mega {

TextureAtlas::~TextureAtlas() {
    if (texArray_) glDeleteTextures(1, &texArray_);
}

static inline void setPx(std::vector<uint8_t>& buf, int x, int y,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    size_t idx = static_cast<size_t>((y * TextureAtlas::TEX_SIZE + x) * 4);
    buf[idx + 0] = r;
    buf[idx + 1] = g;
    buf[idx + 2] = b;
    buf[idx + 3] = a;
}

bool TextureAtlas::init() {
    glGenTextures(1, &texArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, LAYER_COUNT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    std::vector<uint8_t> pixels(TEX_SIZE * TEX_SIZE * 4, 255);
    Rng rng(0xDEADBEEF12345678ULL);

    auto uploadLayer = [&](int layer) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };

    // Layer 0: Fallback
    for (int y = 0; y < TEX_SIZE; ++y)
        for (int x = 0; x < TEX_SIZE; ++x)
            setPx(pixels, x, y, 120, 120, 120);
    uploadLayer(0);

    // Layer 1: Concrete / Liminal Plaster Wall (32x32 crisp pixel art)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int fine = rng.rangeInt(-8, 8);
            int base = 138 + fine;
            if (x == 0 || x == 16) base -= 16;
            if (y == 0 || y == 16) base -= 12;
            if (y >= 27) base -= 26; // Dark baseboard strip
            uint8_t r = static_cast<uint8_t>(std::clamp(base - 4, 28, 215));
            uint8_t g = static_cast<uint8_t>(std::clamp(base - 2, 28, 215));
            uint8_t b = static_cast<uint8_t>(std::clamp(base, 28, 220));
            setPx(pixels, x, y, r, g, b);
        }
    }
    uploadLayer(1);

    // Layer 2: MetalCabinet / Locker Body (seamless brushed dark blue-grey steel, no repeating door pattern)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-4, 4);
            int streak = static_cast<int>(std::sin(y * 0.45f) * 2.0f);
            int r = 74 + n + streak;
            int g = 84 + n + streak;
            int b = 94 + n + streak;
            if (x == 0 || x == 31) { r -= 14; g -= 14; b -= 14; }
            setPx(pixels, x, y,
                  static_cast<uint8_t>(std::clamp(r, 20, 160)),
                  static_cast<uint8_t>(std::clamp(g, 20, 165)),
                  static_cast<uint8_t>(std::clamp(b, 20, 175)));
        }
    }
    uploadLayer(2);

    // Layer 3: WoodCrate (32x32 wooden planks & cross brace)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-7, 7);
            int r = 138 + n;
            int g = 102 + n;
            int b = 66 + n;

            bool frame = (x < 3 || x > 28 || y < 3 || y > 28 || std::abs(x - y) <= 1);
            if (frame) {
                r += 10; g += 8; b += 5;
                if (x == 3 || x == 28 || y == 3 || y == 28) {
                    r -= 30; g -= 24; b -= 18;
                }
            } else if (y % 6 == 0) {
                r -= 32; g -= 26; b -= 18;
            }
            setPx(pixels, x, y,
                  static_cast<uint8_t>(std::clamp(r, 25, 220)),
                  static_cast<uint8_t>(std::clamp(g, 20, 190)),
                  static_cast<uint8_t>(std::clamp(b, 15, 150)));
        }
    }
    uploadLayer(3);

    // Layer 4: ShelfMetal (32x32 dark steel rack frame)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-6, 6);
            int base = 72 + n;
            if (x == 0 || x == 31 || y == 0 || y == 31) base -= 20;
            if ((x % 4 == 2) && (y % 4 == 2)) base -= 22;
            uint8_t v = static_cast<uint8_t>(std::clamp(base, 20, 150));
            setPx(pixels, x, y, v, v + 3, v + 7);
        }
    }
    uploadLayer(4);

    // Layer 5: FloorTile (32x32 worn utility floor tiles)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-7, 7);
            int base = 96 + n;
            bool grout = (x % 16 == 0 || y % 16 == 0);
            if (grout) base -= 28;
            else if (x % 16 == 1 || y % 16 == 1) base += 8;
            uint8_t v = static_cast<uint8_t>(std::clamp(base, 25, 185));
            setPx(pixels, x, y, v, v + 2, v + 5);
        }
    }
    uploadLayer(5);

    // Layer 6: WallLampTube (32x32 blue-white fluorescent fixture)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            bool bezel = (x < 2 || x > 29 || y < 3 || y > 28);
            if (bezel) {
                setPx(pixels, x, y, 52, 58, 66);
            } else {
                float cy = std::abs(y - 15.5f) / 13.0f;
                float glow = std::clamp(1.1f - cy * 0.45f, 0.65f, 1.0f);
                setPx(pixels, x, y,
                      static_cast<uint8_t>(200 * glow),
                      static_cast<uint8_t>(232 * glow),
                      static_cast<uint8_t>(255 * glow));
            }
        }
    }
    uploadLayer(6);

    // Layer 7: CeilingSlab (32x32 dark concrete ceiling panels)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-6, 6);
            int base = 74 + n;
            if (x % 8 == 0 || y % 16 == 0) base -= 18;
            uint8_t v = static_cast<uint8_t>(std::clamp(base, 20, 145));
            setPx(pixels, x, y, v, v + 1, v + 4);
        }
    }
    uploadLayer(7);

    // Layer 8: Trim fallback
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-7, 7);
            setPx(pixels, x, y, 88 + n, 76 + n, 68 + n);
        }
    }
    uploadLayer(8);

    // Layer 9: ChippedBlueArmor (peeling/chipped light-blue paint over dark steel)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-8, 8);
            bool borderWear = (x <= 1 || x >= 30 || y <= 1 || y >= 30) && rng.chance(0.7f);
            float patch = std::sin(x * 0.45f) * std::cos(y * 0.4f) + rng.nextFloat() * 0.8f;
            if (borderWear || patch > 0.95f) {
                // Exposed dark metal underneath
                uint8_t v = static_cast<uint8_t>(std::clamp(44 + n, 20, 75));
                setPx(pixels, x, y, v, v + 2, v + 5);
            } else {
                // Chipped pale-cyan/blue paint
                setPx(pixels, x, y,
                      static_cast<uint8_t>(std::clamp(74 + n, 40, 115)),
                      static_cast<uint8_t>(std::clamp(146 + n, 105, 185)),
                      static_cast<uint8_t>(std::clamp(192 + n, 145, 230)));
            }
        }
    }
    uploadLayer(9);

    // Layer 10: DarkSkeleton (dark gunmetal frame)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-5, 5);
            int base = 36 + n;
            if (x == 0 || y == 0) base -= 10;
            uint8_t v = static_cast<uint8_t>(std::clamp(base, 14, 65));
            setPx(pixels, x, y, v, v + 1, v + 3);
        }
    }
    uploadLayer(10);

    // Layer 11: CabinetDoor (matches Layer 2 exact blue-grey palette, single non-repeating panel)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-4, 4);
            int r = 74 + n, g = 84 + n, b = 94 + n;
            bool rim = (x <= 1 || x >= 30 || y <= 1 || y >= 30);
            bool inset = (x == 3 || x == 28 || y == 3 || y == 28);
            if (rim) { r -= 16; g -= 16; b -= 16; }
            else if (inset) { r -= 10; g -= 10; b -= 10; }
            if (x >= 8 && x <= 23 && (y == 6 || y == 8 || y == 10)) {
                r -= 24; g -= 24; b -= 24; // Top louver vents only (once per door)
            }
            setPx(pixels, x, y,
                  static_cast<uint8_t>(std::clamp(r, 20, 160)),
                  static_cast<uint8_t>(std::clamp(g, 20, 165)),
                  static_cast<uint8_t>(std::clamp(b, 20, 175)));
        }
    }
    uploadLayer(11);

    // Layer 12: HeavyMetalDoor (2m reinforced industrial steel exit door)
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int n = rng.rangeInt(-5, 5);
            int r = 68 + n, g = 74 + n, b = 82 + n;
            bool frame = (x < 3 || x > 28 || y < 3 || y > 28);
            bool midBrace = (y >= 15 && y <= 17);
            if (frame || midBrace) { r -= 16; g -= 16; b -= 16; }
            bool rivet = ((x == 4 || x == 27) && (y == 4 || y == 14 || y == 18 || y == 27));
            if (rivet) { r += 34; g += 34; b += 34; }
            setPx(pixels, x, y,
                  static_cast<uint8_t>(std::clamp(r, 18, 175)),
                  static_cast<uint8_t>(std::clamp(g, 18, 180)),
                  static_cast<uint8_t>(std::clamp(b, 18, 190)));
        }
    }
    uploadLayer(12);

    // Crisp pixel-art filtering (no blur!)
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return true;
}

void TextureAtlas::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
}

} // namespace mega
