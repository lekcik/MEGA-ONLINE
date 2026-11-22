#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
mkdir -p "$BUILD_DIR"

echo "=== Building MEGA (1v1 Zero-Dep P2P Edition) for Linux ==="
g++ -std=c++20 -O2 -Wall -Wextra -Isrc \
    src/main.cpp \
    src/core/Platform.cpp \
    src/net/NetSession.cpp \
    src/render/GlApi.cpp \
    src/render/Shader.cpp \
    src/render/Framebuffer.cpp \
    src/render/TextureAtlas.cpp \
    src/render/WorldMesher.cpp \
    src/render/BillboardRenderer.cpp \
    src/render/RobotRenderer.cpp \
    src/render/HudRenderer.cpp \
    src/world/WorldGrid.cpp \
    src/world/LightBaker.cpp \
    src/world/gen/MegaGenerator.cpp \
    src/game/Player.cpp \
    src/game/GameWorld.cpp \
    src/game/Inventory3D.cpp \
    src/game/LockpickMinigame.cpp \
    src/audio/AudioEngine.cpp \
    -ldl -lpthread \
    -o "$BUILD_DIR/mega"

if [ -f "loot_tables.cfg" ]; then
    cp -f "loot_tables.cfg" "$BUILD_DIR/loot_tables.cfg"
fi

echo ""
echo "Build complete! Run the game with:"
echo "  ./$BUILD_DIR/mega"
