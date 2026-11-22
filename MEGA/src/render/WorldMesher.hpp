#pragma once

#include "world/WorldGrid.hpp"
#include "render/GlApi.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mega {

struct WorldVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float texLayer;
    float ao;
    float emissive;
};

struct ChunkMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;

    void destroy();
    void upload(const std::vector<WorldVertex>& verts, const std::vector<uint32_t>& indices);
};

class WorldMesher {
public:
    WorldMesher() = default;
    ~WorldMesher();

    void syncWorld(WorldGrid& world);
    void render() const;

private:
    std::unordered_map<ChunkCoord, ChunkMesh, ChunkCoordHash> meshes_;
    ChunkMesh propMesh_;
    void buildChunkMesh(const WorldGrid& world, const Chunk& chunk, ChunkMesh& outMesh);
    void buildPropMesh(const WorldGrid& world, ChunkMesh& outMesh);
};

} // namespace mega
