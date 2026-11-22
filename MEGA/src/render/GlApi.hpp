#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
  #define MEGA_GL_API __stdcall
#else
  #define MEGA_GL_API
#endif

using GLboolean   = unsigned char;
using GLbyte      = signed char;
using GLubyte     = unsigned char;
using GLchar      = char;
using GLshort     = short;
using GLushort    = unsigned short;
using GLint       = int;
using GLuint      = unsigned int;
using GLenum      = unsigned int;
using GLbitfield  = unsigned int;
using GLsizei     = int;
using GLsizeiptr  = std::ptrdiff_t;
using GLintptr    = std::ptrdiff_t;
using GLfloat     = float;
using GLclampf    = float;
using GLvoid      = void;

inline constexpr GLboolean GL_FALSE = 0;
inline constexpr GLboolean GL_TRUE  = 1;

inline constexpr GLbitfield GL_DEPTH_BUFFER_BIT   = 0x00000100;
inline constexpr GLbitfield GL_COLOR_BUFFER_BIT   = 0x00004000;

inline constexpr GLenum GL_TRIANGLES              = 0x0004;
inline constexpr GLenum GL_UNSIGNED_BYTE          = 0x1401;
inline constexpr GLenum GL_UNSIGNED_INT           = 0x1405;
inline constexpr GLenum GL_FLOAT                  = 0x1406;

inline constexpr GLenum GL_SRC_ALPHA              = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA    = 0x0303;
inline constexpr GLenum GL_BACK                   = 0x0405;
inline constexpr GLenum GL_CULL_FACE              = 0x0B44;
inline constexpr GLenum GL_DEPTH_TEST             = 0x0B71;
inline constexpr GLenum GL_BLEND                  = 0x0BE2;
inline constexpr GLenum GL_CCW                    = 0x0901;

inline constexpr GLenum GL_TEXTURE_2D             = 0x0DE1;
inline constexpr GLenum GL_TEXTURE_2D_ARRAY       = 0x8C1A;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER     = 0x2800;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER     = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S         = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T         = 0x2803;
inline constexpr GLenum GL_NEAREST                = 0x2600;
inline constexpr GLenum GL_LINEAR                 = 0x2601;
inline constexpr GLenum GL_REPEAT                 = 0x2901;
inline constexpr GLenum GL_CLAMP_TO_EDGE          = 0x812F;
inline constexpr GLenum GL_TEXTURE0               = 0x84C0;

inline constexpr GLenum GL_RGBA                   = 0x1908;
inline constexpr GLenum GL_RGBA8                  = 0x8058;
inline constexpr GLenum GL_RGBA16F                = 0x881A;
inline constexpr GLenum GL_DEPTH24_STENCIL8       = 0x88F0;

inline constexpr GLenum GL_ARRAY_BUFFER           = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER   = 0x8893;
inline constexpr GLenum GL_STREAM_DRAW            = 0x88E0;
inline constexpr GLenum GL_STATIC_DRAW            = 0x88E4;

inline constexpr GLenum GL_FRAGMENT_SHADER        = 0x8B30;
inline constexpr GLenum GL_VERTEX_SHADER          = 0x8B31;
inline constexpr GLenum GL_COMPILE_STATUS         = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS            = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH        = 0x8B84;

inline constexpr GLenum GL_FRAMEBUFFER            = 0x8D40;
inline constexpr GLenum GL_RENDERBUFFER           = 0x8D41;
inline constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
inline constexpr GLenum GL_COLOR_ATTACHMENT0      = 0x8CE0;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE   = 0x8CD5;

namespace mega::glapi {

using PFN_glActiveTexture             = void (MEGA_GL_API*)(GLenum);
using PFN_glAttachShader              = void (MEGA_GL_API*)(GLuint, GLuint);
using PFN_glBindBuffer                = void (MEGA_GL_API*)(GLenum, GLuint);
using PFN_glBindFramebuffer           = void (MEGA_GL_API*)(GLenum, GLuint);
using PFN_glBindRenderbuffer          = void (MEGA_GL_API*)(GLenum, GLuint);
using PFN_glBindTexture               = void (MEGA_GL_API*)(GLenum, GLuint);
using PFN_glBindVertexArray           = void (MEGA_GL_API*)(GLuint);
using PFN_glBlendFunc                 = void (MEGA_GL_API*)(GLenum, GLenum);
using PFN_glBufferData                = void (MEGA_GL_API*)(GLenum, GLsizeiptr, const void*, GLenum);
using PFN_glCheckFramebufferStatus    = GLenum (MEGA_GL_API*)(GLenum);
using PFN_glClear                     = void (MEGA_GL_API*)(GLbitfield);
using PFN_glClearColor                = void (MEGA_GL_API*)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFN_glCompileShader             = void (MEGA_GL_API*)(GLuint);
using PFN_glCreateProgram             = GLuint (MEGA_GL_API*)();
using PFN_glCreateShader              = GLuint (MEGA_GL_API*)(GLenum);
using PFN_glCullFace                  = void (MEGA_GL_API*)(GLenum);
using PFN_glDeleteBuffers             = void (MEGA_GL_API*)(GLsizei, const GLuint*);
using PFN_glDeleteFramebuffers        = void (MEGA_GL_API*)(GLsizei, const GLuint*);
using PFN_glDeleteProgram             = void (MEGA_GL_API*)(GLuint);
using PFN_glDeleteRenderbuffers       = void (MEGA_GL_API*)(GLsizei, const GLuint*);
using PFN_glDeleteShader              = void (MEGA_GL_API*)(GLuint);
using PFN_glDeleteTextures            = void (MEGA_GL_API*)(GLsizei, const GLuint*);
using PFN_glDeleteVertexArrays        = void (MEGA_GL_API*)(GLsizei, const GLuint*);
using PFN_glDisable                   = void (MEGA_GL_API*)(GLenum);
using PFN_glDrawArrays                = void (MEGA_GL_API*)(GLenum, GLint, GLsizei);
using PFN_glDrawElements              = void (MEGA_GL_API*)(GLenum, GLsizei, GLenum, const void*);
using PFN_glEnable                    = void (MEGA_GL_API*)(GLenum);
using PFN_glEnableVertexAttribArray   = void (MEGA_GL_API*)(GLuint);
using PFN_glFramebufferRenderbuffer   = void (MEGA_GL_API*)(GLenum, GLenum, GLenum, GLuint);
using PFN_glFramebufferTexture2D      = void (MEGA_GL_API*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFN_glFrontFace                 = void (MEGA_GL_API*)(GLenum);
using PFN_glGenBuffers                = void (MEGA_GL_API*)(GLsizei, GLuint*);
using PFN_glGenFramebuffers           = void (MEGA_GL_API*)(GLsizei, GLuint*);
using PFN_glGenRenderbuffers          = void (MEGA_GL_API*)(GLsizei, GLuint*);
using PFN_glGenTextures               = void (MEGA_GL_API*)(GLsizei, GLuint*);
using PFN_glGenVertexArrays           = void (MEGA_GL_API*)(GLsizei, GLuint*);
using PFN_glGetProgramInfoLog         = void (MEGA_GL_API*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFN_glGetProgramiv              = void (MEGA_GL_API*)(GLuint, GLenum, GLint*);
using PFN_glGetShaderInfoLog          = void (MEGA_GL_API*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFN_glGetShaderiv               = void (MEGA_GL_API*)(GLuint, GLenum, GLint*);
using PFN_glGetUniformLocation        = GLint (MEGA_GL_API*)(GLuint, const GLchar*);
using PFN_glLinkProgram               = void (MEGA_GL_API*)(GLuint);
using PFN_glRenderbufferStorage       = void (MEGA_GL_API*)(GLenum, GLenum, GLsizei, GLsizei);
using PFN_glShaderSource              = void (MEGA_GL_API*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFN_glTexImage2D                = void (MEGA_GL_API*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using PFN_glTexImage3D                = void (MEGA_GL_API*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using PFN_glTexParameteri             = void (MEGA_GL_API*)(GLenum, GLenum, GLint);
using PFN_glTexSubImage2D             = void (MEGA_GL_API*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
using PFN_glTexSubImage3D             = void (MEGA_GL_API*)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
using PFN_glUniform1f                 = void (MEGA_GL_API*)(GLint, GLfloat);
using PFN_glUniform1i                 = void (MEGA_GL_API*)(GLint, GLint);
using PFN_glUniform2f                 = void (MEGA_GL_API*)(GLint, GLfloat, GLfloat);
using PFN_glUniform3f                 = void (MEGA_GL_API*)(GLint, GLfloat, GLfloat, GLfloat);
using PFN_glUniform4fv                = void (MEGA_GL_API*)(GLint, GLsizei, const GLfloat*);
using PFN_glUniformMatrix4fv          = void (MEGA_GL_API*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFN_glUseProgram                = void (MEGA_GL_API*)(GLuint);
using PFN_glVertexAttribPointer       = void (MEGA_GL_API*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PFN_glViewport                  = void (MEGA_GL_API*)(GLint, GLint, GLsizei, GLsizei);

extern PFN_glActiveTexture             glActiveTexture;
extern PFN_glAttachShader              glAttachShader;
extern PFN_glBindBuffer                glBindBuffer;
extern PFN_glBindFramebuffer           glBindFramebuffer;
extern PFN_glBindRenderbuffer          glBindRenderbuffer;
extern PFN_glBindTexture               glBindTexture;
extern PFN_glBindVertexArray           glBindVertexArray;
extern PFN_glBlendFunc                 glBlendFunc;
extern PFN_glBufferData                glBufferData;
extern PFN_glCheckFramebufferStatus    glCheckFramebufferStatus;
extern PFN_glClear                     glClear;
extern PFN_glClearColor                glClearColor;
extern PFN_glCompileShader             glCompileShader;
extern PFN_glCreateProgram             glCreateProgram;
extern PFN_glCreateShader              glCreateShader;
extern PFN_glCullFace                  glCullFace;
extern PFN_glDeleteBuffers             glDeleteBuffers;
extern PFN_glDeleteFramebuffers        glDeleteFramebuffers;
extern PFN_glDeleteProgram             glDeleteProgram;
extern PFN_glDeleteRenderbuffers       glDeleteRenderbuffers;
extern PFN_glDeleteShader              glDeleteShader;
extern PFN_glDeleteTextures            glDeleteTextures;
extern PFN_glDeleteVertexArrays        glDeleteVertexArrays;
extern PFN_glDisable                   glDisable;
extern PFN_glDrawArrays                glDrawArrays;
extern PFN_glDrawElements              glDrawElements;
extern PFN_glEnable                    glEnable;
extern PFN_glEnableVertexAttribArray   glEnableVertexAttribArray;
extern PFN_glFramebufferRenderbuffer   glFramebufferRenderbuffer;
extern PFN_glFramebufferTexture2D      glFramebufferTexture2D;
extern PFN_glFrontFace                 glFrontFace;
extern PFN_glGenBuffers                glGenBuffers;
extern PFN_glGenFramebuffers           glGenFramebuffers;
extern PFN_glGenRenderbuffers          glGenRenderbuffers;
extern PFN_glGenTextures               glGenTextures;
extern PFN_glGenVertexArrays           glGenVertexArrays;
extern PFN_glGetProgramInfoLog         glGetProgramInfoLog;
extern PFN_glGetProgramiv              glGetProgramiv;
extern PFN_glGetShaderInfoLog          glGetShaderInfoLog;
extern PFN_glGetShaderiv               glGetShaderiv;
extern PFN_glGetUniformLocation        glGetUniformLocation;
extern PFN_glLinkProgram               glLinkProgram;
extern PFN_glRenderbufferStorage       glRenderbufferStorage;
extern PFN_glShaderSource              glShaderSource;
extern PFN_glTexImage2D                glTexImage2D;
extern PFN_glTexImage3D                glTexImage3D;
extern PFN_glTexParameteri             glTexParameteri;
extern PFN_glTexSubImage2D             glTexSubImage2D;
extern PFN_glTexSubImage3D             glTexSubImage3D;
extern PFN_glUniform1f                 glUniform1f;
extern PFN_glUniform1i                 glUniform1i;
extern PFN_glUniform2f                 glUniform2f;
extern PFN_glUniform3f                 glUniform3f;
extern PFN_glUniform4fv                glUniform4fv;
extern PFN_glUniformMatrix4fv          glUniformMatrix4fv;
extern PFN_glUseProgram                glUseProgram;
extern PFN_glVertexAttribPointer       glVertexAttribPointer;
extern PFN_glViewport                  glViewport;

bool initGlApi();

} // namespace mega::glapi

using namespace mega::glapi;
