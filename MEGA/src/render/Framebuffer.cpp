#include "render/Framebuffer.hpp"
#include <iostream>

namespace mega {

Framebuffer::~Framebuffer() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
    if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
    if (quadVao_) glDeleteVertexArrays(1, &quadVao_);
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
}

bool Framebuffer::init(int width, int height) {
    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    // Use GL_RGBA16F for HDR lighting & bloom highlights
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer is incomplete!\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const float quadVerts[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    const std::string vs = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

    // PS3-era post-processing: soft HDR bloom glow from blue-white lamps,
    // cold desaturated contrast curve, subtle vignette & film noise,
    // plus optional post-process Night Vision for the Monster role
    const std::string fs = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScreenTex;
uniform vec2 uRetroSize;
uniform float uNightVision;

vec3 sampleGlow(vec2 uv, vec2 texel) {
    vec3 sum = vec3(0.0);
    float weights[5] = float[5](0.22, 0.19, 0.15, 0.11, 0.07);
    for (int i = 1; i <= 5; ++i) {
        float r = float(i) * 2.5;
        vec3 c1 = texture(uScreenTex, uv + vec2( r,  0.0) * texel).rgb;
        vec3 c2 = texture(uScreenTex, uv + vec2(-r,  0.0) * texel).rgb;
        vec3 c3 = texture(uScreenTex, uv + vec2( 0.0,  r) * texel).rgb;
        vec3 c4 = texture(uScreenTex, uv + vec2( 0.0, -r) * texel).rgb;
        vec3 c5 = texture(uScreenTex, uv + vec2( r,  r) * 0.7 * texel).rgb;
        vec3 c6 = texture(uScreenTex, uv + vec2(-r,  r) * 0.7 * texel).rgb;
        vec3 c7 = texture(uScreenTex, uv + vec2( r, -r) * 0.7 * texel).rgb;
        vec3 c8 = texture(uScreenTex, uv + vec2(-r, -r) * 0.7 * texel).rgb;
        vec3 avg = (c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8) * 0.125;
        sum += max(avg - vec3(0.75), vec3(0.0)) * weights[i - 1];
    }
    return sum;
}

void main() {
    vec2 texel = 1.0 / uRetroSize;
    vec3 hdr = texture(uScreenTex, vUV).rgb;
    vec3 bloom = sampleGlow(vUV, texel);
    hdr += bloom * vec3(0.75, 0.90, 1.10);

    if (uNightVision > 0.5) {
        hdr = hdr * 8.5 + vec3(0.012, 0.016, 0.012);
    }

    // ACES-inspired filmic tonemapping
    vec3 x = max(vec3(0.0), hdr - 0.004);
    vec3 col = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);

    // PS3-era slight desaturation & cold shadow tint
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(luma), col, 0.84);
    col = pow(clamp(col, 0.0, 1.0), vec3(0.96, 0.95, 0.92));

    // Vignette
    vec2 d = vUV - vec2(0.5);
    float vig = clamp(1.0 - dot(d, d) * 0.75, 0.35, 1.0);
    col *= vig;

    // Subtle film grain to prevent banding in dark corridors
    float grain = fract(sin(dot(vUV * uRetroSize, vec2(12.9898, 78.233))) * 43758.5453);
    col += (grain - 0.5) * (1.5 / 255.0);

    if (uNightVision > 0.5) {
        float nvLum = clamp(dot(col, vec3(0.299, 0.587, 0.114)) * 1.15 + 0.04, 0.0, 1.0);
        float scan = 0.94 + 0.06 * sin(vUV.y * uRetroSize.y * 1.5);
        col = vec3(nvLum * 0.24, nvLum * 1.02, nvLum * 0.36) * scan;
    }

    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
})";

    return postShader_.compile(vs, fs);
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void Framebuffer::unbind(int windowWidth, int windowHeight) const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowWidth, windowHeight);
}

void Framebuffer::present(int windowWidth, int windowHeight, bool nightVision) const {
    unbind(windowWidth, windowHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    postShader_.use();
    postShader_.setInt("uScreenTex", 0);
    postShader_.setFloat("uNightVision", nightVision ? 1.0f : 0.0f);
    glUniform2f(glGetUniformLocation(postShader_.id(), "uRetroSize"),
                static_cast<float>(width_), static_cast<float>(height_));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

} // namespace mega
