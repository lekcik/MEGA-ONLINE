#include "render/WorldMesher.hpp"
#include <algorithm>
#include <cstddef>

namespace mega {

void ChunkMesh::destroy() {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ebo) { glDeleteBuffers(1, &ebo); ebo = 0; }
    indexCount = 0;
}

void ChunkMesh::upload(const std::vector<WorldVertex>& verts, const std::vector<uint32_t>& indices) {
    indexCount = static_cast<GLsizei>(indices.size());
    if (indexCount == 0) {
        destroy();
        return;
    }

    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(WorldVertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)offsetof(WorldVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)offsetof(WorldVertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)offsetof(WorldVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)offsetof(WorldVertex, ao));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)offsetof(WorldVertex, emissive));

    glBindVertexArray(0);
}

WorldMesher::~WorldMesher() {
    for (auto& [_, m] : meshes_) {
        m.destroy();
    }
    propMesh_.destroy();
}

void WorldMesher::syncWorld(WorldGrid& world) {
    auto& chunkMap = world.chunkMap();

    // Free GPU meshes for chunks that were unloaded
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (chunkMap.find(it->first) == chunkMap.end()) {
            it->second.destroy();
            it = meshes_.erase(it);
        } else {
            ++it;
        }
    }

    // Build or update dirty chunks
    for (auto& [coord, chunk] : chunkMap) {
        if (chunk.dirty || meshes_.find(coord) == meshes_.end()) {
            buildChunkMesh(world, chunk, meshes_[coord]);
            chunk.dirty = false;
        }
    }

    if (world.propsDirty()) {
        buildPropMesh(world, propMesh_);
        world.clearPropsDirty();
    }
}

void WorldMesher::render() const {
    for (const auto& [_, m] : meshes_) {
        if (m.indexCount == 0 || m.vao == 0) continue;
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    if (propMesh_.indexCount > 0 && propMesh_.vao != 0) {
        glBindVertexArray(propMesh_.vao);
        glDrawElements(GL_TRIANGLES, propMesh_.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

namespace {

struct FaceDef {
    int nx, ny, nz;
    float corners[4][3];
    float uvs[4][2];
};

constexpr FaceDef FACES[6] = {
    { 1, 0, 0,
      {{1,0,1}, {1,0,0}, {1,1,0}, {1,1,1}},
      {{0,1}, {1,1}, {1,0}, {0,0}} },
    {-1, 0, 0,
      {{0,0,0}, {0,0,1}, {0,1,1}, {0,1,0}},
      {{0,1}, {1,1}, {1,0}, {0,0}} },
    { 0, 1, 0,
      {{0,1,1}, {1,1,1}, {1,1,0}, {0,1,0}},
      {{0,1}, {1,1}, {1,0}, {0,0}} },
    { 0,-1, 0,
      {{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}},
      {{0,1}, {1,1}, {1,0}, {0,0}} },
    { 0, 0, 1,
      {{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}},
      {{0,1}, {1,1}, {1,0}, {0,0}} },
    { 0, 0,-1,
      {{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}},
      {{0,1}, {1,1}, {1,0}, {0,0}} }
};

inline float vertexAO(const WorldGrid& world, int x, int y, int z,
                      int nx, int ny, int nz, float cx, float cy, float cz) {
    int sx = (cx > 0.5f) ? 1 : -1;
    int sy = (cy > 0.5f) ? 1 : -1;
    int sz = (cz > 0.5f) ? 1 : -1;

    int ax = x + nx, ay = y + ny, az = z + nz;
    int side1 = 0, side2 = 0, corner = 0;

    if (nx != 0) {
        side1  = world.getCell(ax, ay + sy, az).isOpaque() ? 1 : 0;
        side2  = world.getCell(ax, ay, az + sz).isOpaque() ? 1 : 0;
        corner = world.getCell(ax, ay + sy, az + sz).isOpaque() ? 1 : 0;
    } else if (ny != 0) {
        side1  = world.getCell(ax + sx, ay, az).isOpaque() ? 1 : 0;
        side2  = world.getCell(ax, ay, az + sz).isOpaque() ? 1 : 0;
        corner = world.getCell(ax + sx, ay, az + sz).isOpaque() ? 1 : 0;
    } else {
        side1  = world.getCell(ax + sx, ay, az).isOpaque() ? 1 : 0;
        side2  = world.getCell(ax, ay + sy, az).isOpaque() ? 1 : 0;
        corner = world.getCell(ax + sx, ay + sy, az).isOpaque() ? 1 : 0;
    }

    int occ = (side1 && side2) ? 3 : (side1 + side2 + corner);
    static constexpr float AO_CURVE[4] = {1.0f, 0.72f, 0.48f, 0.28f};
    return AO_CURVE[occ];
}

} // namespace

void WorldMesher::buildChunkMesh(const WorldGrid& world, const Chunk& chunk, ChunkMesh& outMesh) {
    std::vector<WorldVertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(1024);
    indices.reserve(1536);

    const int baseX = chunk.cx * Chunk::SIZE;
    const int baseY = chunk.cy * Chunk::SIZE;
    const int baseZ = chunk.cz * Chunk::SIZE;

    for (int ly = 0; ly < Chunk::SIZE; ++ly) {
        for (int lz = 0; lz < Chunk::SIZE; ++lz) {
            for (int lx = 0; lx < Chunk::SIZE; ++lx) {
                const Cell& cell = chunk.at(lx, ly, lz);
                if (!cell.isSolid()) continue;

                int wx = baseX + lx;
                int wy = baseY + ly;
                int wz = baseZ + lz;
                float layer = static_cast<float>(static_cast<uint8_t>(cell.type));
                float emissive = cell.isEmissive() ? 1.0f : 0.0f;

                for (int f = 0; f < 6; ++f) {
                    const FaceDef& fd = FACES[f];
                    int nx = wx + fd.nx;
                    int ny = wy + fd.ny;
                    int nz = wz + fd.nz;

                    Cell neighbor = world.getCell(nx, ny, nz);
                    if (neighbor.isOpaque()) continue;

                    uint32_t baseIdx = static_cast<uint32_t>(verts.size());
                    float ao[4];
                    float topY = cell.topHeight();
                    for (int c = 0; c < 4; ++c) {
                        ao[c] = cell.isEmissive()
                                    ? 1.0f
                                    : vertexAO(world, wx, wy, wz,
                                               fd.nx, fd.ny, fd.nz,
                                               fd.corners[c][0], fd.corners[c][1], fd.corners[c][2]);
                        WorldVertex v{};
                        v.x = static_cast<float>(wx) + fd.corners[c][0];
                        v.y = static_cast<float>(wy) + fd.corners[c][1] * topY;
                        v.z = static_cast<float>(wz) + fd.corners[c][2];
                        v.nx = static_cast<float>(fd.nx);
                        v.ny = static_cast<float>(fd.ny);
                        v.nz = static_cast<float>(fd.nz);
                        v.u = fd.uvs[c][0];
                        v.v = fd.uvs[c][1];
                        v.texLayer = layer;
                        v.ao = ao[c];
                        v.emissive = emissive;
                        verts.push_back(v);
                    }

                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        indices.push_back(baseIdx + 0);
                        indices.push_back(baseIdx + 1);
                        indices.push_back(baseIdx + 2);
                        indices.push_back(baseIdx + 0);
                        indices.push_back(baseIdx + 2);
                        indices.push_back(baseIdx + 3);
                    } else {
                        indices.push_back(baseIdx + 1);
                        indices.push_back(baseIdx + 2);
                        indices.push_back(baseIdx + 3);
                        indices.push_back(baseIdx + 1);
                        indices.push_back(baseIdx + 3);
                        indices.push_back(baseIdx + 0);
                    }
                }
            }
        }
    }

    outMesh.upload(verts, indices);
}

void WorldMesher::buildPropMesh(const WorldGrid& world, ChunkMesh& outMesh) {
    std::vector<WorldVertex> verts;
    std::vector<uint32_t> indices;
    const auto& props = world.props();
    verts.reserve(props.size() * 24);
    indices.reserve(props.size() * 36);

    for (const auto& p : props) {
        float sx = p.box.max.x - p.box.min.x;
        float sy = p.box.max.y - p.box.min.y;
        float sz = p.box.max.z - p.box.min.z;
        float layer = static_cast<float>(static_cast<uint8_t>(p.material));
        float emissive = p.emissive ? 1.0f : 0.0f;

        for (int f = 0; f < 6; ++f) {
            const FaceDef& fd = FACES[f];
            uint32_t baseIdx = static_cast<uint32_t>(verts.size());

            // Enforce unified 1 meter = 32 pixels texel density on all props
            float faceW = (fd.nx != 0) ? sz : sx;
            float faceH = (fd.ny != 0) ? sz : sy;

            for (int c = 0; c < 4; ++c) {
                WorldVertex v{};
                v.x = p.box.min.x + fd.corners[c][0] * sx;
                v.y = p.box.min.y + fd.corners[c][1] * sy;
                v.z = p.box.min.z + fd.corners[c][2] * sz;
                v.nx = static_cast<float>(fd.nx);
                v.ny = static_cast<float>(fd.ny);
                v.nz = static_cast<float>(fd.nz);
                if (p.emissive) {
                    // Keep lamp tube centered while preserving 1m = 32px scale
                    v.u = 0.5f + (fd.uvs[c][0] - 0.5f) * faceW;
                    v.v = 0.5f + (fd.uvs[c][1] - 0.5f) * faceH;
                } else {
                    v.u = fd.uvs[c][0] * faceW;
                    v.v = fd.uvs[c][1] * faceH;
                }
                v.texLayer = layer;
                bool nearFloor = (!p.emissive && v.y <= 1.06f && fd.ny == 0);
                v.ao = nearFloor ? 0.62f : 0.94f;
                v.emissive = emissive;
                verts.push_back(v);
            }

            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        }
    }

    outMesh.upload(verts, indices);
}

} // namespace mega
