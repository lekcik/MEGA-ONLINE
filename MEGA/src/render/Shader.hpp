#pragma once

#include "core/Math3D.hpp"
#include "render/GlApi.hpp"
#include <string>

namespace mega {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept;
    Shader& operator=(Shader&& o) noexcept;

    bool compile(const std::string& vertexSrc, const std::string& fragmentSrc);
    void use() const;

    void setInt(const char* name, int v) const;
    void setFloat(const char* name, float v) const;
    void setVec3(const char* name, const Vec3& v) const;
    void setMat4(const char* name, const Mat4& m) const;

    GLuint id() const { return program_; }

private:
    GLuint program_ = 0;
    static GLuint compileStage(GLenum type, const std::string& src);
};

} // namespace mega
