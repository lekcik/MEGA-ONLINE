#pragma once

#include "render/GlApi.hpp"
#include "render/Shader.hpp"

namespace mega {

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();

    bool init(int width, int height);
    void bind() const;
    void unbind(int windowWidth, int windowHeight) const;
    void present(int windowWidth, int windowHeight, bool nightVision = false) const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    GLuint fbo_ = 0;
    GLuint colorTex_ = 0;
    GLuint depthRbo_ = 0;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    Shader postShader_;
};

} // namespace mega
