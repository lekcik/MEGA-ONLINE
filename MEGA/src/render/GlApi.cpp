#include "render/GlApi.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <type_traits>

namespace mega::glapi {

PFN_glActiveTexture             glActiveTexture = nullptr;
PFN_glAttachShader              glAttachShader = nullptr;
PFN_glBindBuffer                glBindBuffer = nullptr;
PFN_glBindFramebuffer           glBindFramebuffer = nullptr;
PFN_glBindRenderbuffer          glBindRenderbuffer = nullptr;
PFN_glBindTexture               glBindTexture = nullptr;
PFN_glBindVertexArray           glBindVertexArray = nullptr;
PFN_glBlendFunc                 glBlendFunc = nullptr;
PFN_glBufferData                glBufferData = nullptr;
PFN_glCheckFramebufferStatus    glCheckFramebufferStatus = nullptr;
PFN_glClear                     glClear = nullptr;
PFN_glClearColor                glClearColor = nullptr;
PFN_glCompileShader             glCompileShader = nullptr;
PFN_glCreateProgram             glCreateProgram = nullptr;
PFN_glCreateShader              glCreateShader = nullptr;
PFN_glCullFace                  glCullFace = nullptr;
PFN_glDeleteBuffers             glDeleteBuffers = nullptr;
PFN_glDeleteFramebuffers        glDeleteFramebuffers = nullptr;
PFN_glDeleteProgram             glDeleteProgram = nullptr;
PFN_glDeleteRenderbuffers       glDeleteRenderbuffers = nullptr;
PFN_glDeleteShader              glDeleteShader = nullptr;
PFN_glDeleteTextures            glDeleteTextures = nullptr;
PFN_glDeleteVertexArrays        glDeleteVertexArrays = nullptr;
PFN_glDisable                   glDisable = nullptr;
PFN_glDrawArrays                glDrawArrays = nullptr;
PFN_glDrawElements              glDrawElements = nullptr;
PFN_glEnable                    glEnable = nullptr;
PFN_glEnableVertexAttribArray   glEnableVertexAttribArray = nullptr;
PFN_glFramebufferRenderbuffer   glFramebufferRenderbuffer = nullptr;
PFN_glFramebufferTexture2D      glFramebufferTexture2D = nullptr;
PFN_glFrontFace                 glFrontFace = nullptr;
PFN_glGenBuffers                glGenBuffers = nullptr;
PFN_glGenFramebuffers           glGenFramebuffers = nullptr;
PFN_glGenRenderbuffers          glGenRenderbuffers = nullptr;
PFN_glGenTextures               glGenTextures = nullptr;
PFN_glGenVertexArrays           glGenVertexArrays = nullptr;
PFN_glGetProgramInfoLog         glGetProgramInfoLog = nullptr;
PFN_glGetProgramiv              glGetProgramiv = nullptr;
PFN_glGetShaderInfoLog          glGetShaderInfoLog = nullptr;
PFN_glGetShaderiv               glGetShaderiv = nullptr;
PFN_glGetUniformLocation        glGetUniformLocation = nullptr;
PFN_glLinkProgram               glLinkProgram = nullptr;
PFN_glRenderbufferStorage       glRenderbufferStorage = nullptr;
PFN_glShaderSource              glShaderSource = nullptr;
PFN_glTexImage2D                glTexImage2D = nullptr;
PFN_glTexImage3D                glTexImage3D = nullptr;
PFN_glTexParameteri             glTexParameteri = nullptr;
PFN_glTexSubImage2D             glTexSubImage2D = nullptr;
PFN_glTexSubImage3D             glTexSubImage3D = nullptr;
PFN_glUniform1f                 glUniform1f = nullptr;
PFN_glUniform1i                 glUniform1i = nullptr;
PFN_glUniform2f                 glUniform2f = nullptr;
PFN_glUniform3f                 glUniform3f = nullptr;
PFN_glUniform4fv                glUniform4fv = nullptr;
PFN_glUniformMatrix4fv          glUniformMatrix4fv = nullptr;
PFN_glUseProgram                glUseProgram = nullptr;
PFN_glVertexAttribPointer       glVertexAttribPointer = nullptr;
PFN_glViewport                  glViewport = nullptr;

bool initGlApi() {
    auto load = [](auto& fn, const char* name) -> bool {
        using FnT = std::remove_reference_t<decltype(fn)>;
        fn = reinterpret_cast<FnT>(SDL_GL_GetProcAddress(name));
        if (!fn) {
            std::cerr << "Failed to load OpenGL symbol: " << name << "\n";
            return false;
        }
        return true;
    };

    bool ok = true;
    ok &= load(glActiveTexture,           "glActiveTexture");
    ok &= load(glAttachShader,            "glAttachShader");
    ok &= load(glBindBuffer,              "glBindBuffer");
    ok &= load(glBindFramebuffer,         "glBindFramebuffer");
    ok &= load(glBindRenderbuffer,        "glBindRenderbuffer");
    ok &= load(glBindTexture,             "glBindTexture");
    ok &= load(glBindVertexArray,         "glBindVertexArray");
    ok &= load(glBlendFunc,               "glBlendFunc");
    ok &= load(glBufferData,              "glBufferData");
    ok &= load(glCheckFramebufferStatus,  "glCheckFramebufferStatus");
    ok &= load(glClear,                   "glClear");
    ok &= load(glClearColor,              "glClearColor");
    ok &= load(glCompileShader,           "glCompileShader");
    ok &= load(glCreateProgram,           "glCreateProgram");
    ok &= load(glCreateShader,            "glCreateShader");
    ok &= load(glCullFace,                "glCullFace");
    ok &= load(glDeleteBuffers,           "glDeleteBuffers");
    ok &= load(glDeleteFramebuffers,      "glDeleteFramebuffers");
    ok &= load(glDeleteProgram,           "glDeleteProgram");
    ok &= load(glDeleteRenderbuffers,     "glDeleteRenderbuffers");
    ok &= load(glDeleteShader,            "glDeleteShader");
    ok &= load(glDeleteTextures,          "glDeleteTextures");
    ok &= load(glDeleteVertexArrays,      "glDeleteVertexArrays");
    ok &= load(glDisable,                 "glDisable");
    ok &= load(glDrawArrays,              "glDrawArrays");
    ok &= load(glDrawElements,            "glDrawElements");
    ok &= load(glEnable,                  "glEnable");
    ok &= load(glEnableVertexAttribArray, "glEnableVertexAttribArray");
    ok &= load(glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    ok &= load(glFramebufferTexture2D,    "glFramebufferTexture2D");
    ok &= load(glFrontFace,               "glFrontFace");
    ok &= load(glGenBuffers,              "glGenBuffers");
    ok &= load(glGenFramebuffers,         "glGenFramebuffers");
    ok &= load(glGenRenderbuffers,        "glGenRenderbuffers");
    ok &= load(glGenTextures,             "glGenTextures");
    ok &= load(glGenVertexArrays,         "glGenVertexArrays");
    ok &= load(glGetProgramInfoLog,       "glGetProgramInfoLog");
    ok &= load(glGetProgramiv,            "glGetProgramiv");
    ok &= load(glGetShaderInfoLog,        "glGetShaderInfoLog");
    ok &= load(glGetShaderiv,             "glGetShaderiv");
    ok &= load(glGetUniformLocation,      "glGetUniformLocation");
    ok &= load(glLinkProgram,             "glLinkProgram");
    ok &= load(glRenderbufferStorage,     "glRenderbufferStorage");
    ok &= load(glShaderSource,            "glShaderSource");
    ok &= load(glTexImage2D,              "glTexImage2D");
    ok &= load(glTexImage3D,              "glTexImage3D");
    ok &= load(glTexParameteri,           "glTexParameteri");
    ok &= load(glTexSubImage2D,           "glTexSubImage2D");
    ok &= load(glTexSubImage3D,           "glTexSubImage3D");
    ok &= load(glUniform1f,               "glUniform1f");
    ok &= load(glUniform1i,               "glUniform1i");
    ok &= load(glUniform2f,               "glUniform2f");
    ok &= load(glUniform3f,               "glUniform3f");
    ok &= load(glUniform4fv,              "glUniform4fv");
    ok &= load(glUniformMatrix4fv,        "glUniformMatrix4fv");
    ok &= load(glUseProgram,              "glUseProgram");
    ok &= load(glVertexAttribPointer,     "glVertexAttribPointer");
    ok &= load(glViewport,                "glViewport");
    return ok;
}

} // namespace mega::glapi
