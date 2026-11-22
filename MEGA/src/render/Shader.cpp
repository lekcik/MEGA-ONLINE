#include "render/Shader.hpp"
#include <iostream>
#include <vector>

namespace mega {

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

Shader::Shader(Shader&& o) noexcept : program_(o.program_) {
    o.program_ = 0;
}

Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        if (program_) glDeleteProgram(program_);
        program_ = o.program_;
        o.program_ = 0;
    }
    return *this;
}

GLuint Shader::compileStage(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* cstr = src.c_str();
    glShaderSource(s, 1, &cstr, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len + 1));
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::cerr << "Shader compile error: " << log.data() << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::compile(const std::string& vertexSrc, const std::string& fragmentSrc) {
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len + 1));
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        std::cerr << "Program link error: " << log.data() << "\n";
        glDeleteProgram(prog);
        return false;
    }

    if (program_) glDeleteProgram(program_);
    program_ = prog;
    return true;
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::setInt(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(program_, name), v);
}

void Shader::setFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(program_, name), v);
}

void Shader::setVec3(const char* name, const Vec3& v) const {
    glUniform3f(glGetUniformLocation(program_, name), v.x, v.y, v.z);
}

void Shader::setMat4(const char* name, const Mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, m.m);
}

} // namespace mega
