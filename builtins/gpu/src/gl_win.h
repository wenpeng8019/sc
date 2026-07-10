/* ============================================================
 * gl_win.h —— Windows 桌面 OpenGL 加载器（WGL）
 * ============================================================
 * opengl32.dll 只导出 GL 1.1；GL 1.2+ 函数须运行时经 wglGetProcAddress
 * 装载。本头补齐 gl_env.c / gl_gfx.c 用到的 GL 2.0+ 函数指针与枚举
 *（Windows SDK 的 <GL/gl.h> 不含），命名与标准一致——静态指针即以函数名
 * 命名，调用侧（gl_gfx.c 等）零改动直接用 glGenBuffers(...) 等。
 *
 * 头内为 static：每个包含它的 TU（gl_env.o / gl_gfx.o）各自持一份指针，
 * 各自 scgl_win_load() 装载；同一 current 上下文下 wglGetProcAddress 返回
 * 一致地址，互不影响。装载须在 GL 上下文 make current 之后调用。
 *
 * 仅 GL/gl.h 缺失的 1.2+ 符号在此登记；1.0/1.1 函数与枚举（glTexImage2D /
 * glViewport / GL_TEXTURE_2D 等）直接取自 <GL/gl.h> + opengl32.lib。
 * ============================================================ */
#ifndef SC_GPU_GL_WIN_H
#define SC_GPU_GL_WIN_H

#include <windows.h>
#include <GL/gl.h>
#include <stddef.h>
#include <stdbool.h>

/* --- <GL/gl.h>(1.1) 未定义的类型 --- */
#ifndef GLchar
typedef char GLchar;
#endif
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

/* --- GL 1.2+ 枚举（<GL/gl.h> 未含；#ifndef 保护，值取自 OpenGL registry） --- */
/* 缓冲 */
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
/* 纹理 */
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD 0x813A
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD 0x813B
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#endif
#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif
#ifndef GL_FLOAT_32_UNSIGNED_INT_24_8_REV
#define GL_FLOAT_32_UNSIGNED_INT_24_8_REV 0x8DAD
#endif
/* 带尺寸内部格式 */
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_R16F
#define GL_R16F 0x822D
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_DEPTH32F_STENCIL8
#define GL_DEPTH32F_STENCIL8 0x8CAD
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
/* 着色器 */
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
/* 帧缓冲 */
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
/* 混合（1.4/2.0） */
#ifndef GL_CONSTANT_COLOR
#define GL_CONSTANT_COLOR 0x8001
#endif
#ifndef GL_ONE_MINUS_CONSTANT_COLOR
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_FUNC_SUBTRACT
#define GL_FUNC_SUBTRACT 0x800A
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#endif
#ifndef GL_MIN
#define GL_MIN 0x8007
#endif
#ifndef GL_MAX
#define GL_MAX 0x8008
#endif
/* 模板环绕 */
#ifndef GL_INCR_WRAP
#define GL_INCR_WRAP 0x8507
#endif
#ifndef GL_DECR_WRAP
#define GL_DECR_WRAP 0x8508
#endif
/* 其它 */
#ifndef GL_SAMPLE_ALPHA_TO_COVERAGE
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#endif
#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

/* --- GL 1.2+ 函数清单（X-macro：返回类型, 名字, 参数列表） --- */
#define SCGL_WIN_FUNCS(X) \
    X(void,   glGenBuffers,             (GLsizei, GLuint*)) \
    X(void,   glBindBuffer,             (GLenum, GLuint)) \
    X(void,   glBufferData,             (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void,   glBufferSubData,          (GLenum, GLintptr, GLsizeiptr, const void*)) \
    X(void,   glDeleteBuffers,          (GLsizei, const GLuint*)) \
    X(void,   glBindBufferRange,        (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
    X(void,   glGenVertexArrays,        (GLsizei, GLuint*)) \
    X(void,   glBindVertexArray,        (GLuint)) \
    X(void,   glDeleteVertexArrays,     (GLsizei, const GLuint*)) \
    X(void,   glTexImage3D,             (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void,   glActiveTexture,          (GLenum)) \
    X(void,   glTexImage2DMultisample,  (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean)) \
    X(void,   glGenSamplers,            (GLsizei, GLuint*)) \
    X(void,   glSamplerParameteri,      (GLuint, GLenum, GLint)) \
    X(void,   glSamplerParameterf,      (GLuint, GLenum, GLfloat)) \
    X(void,   glSamplerParameterfv,     (GLuint, GLenum, const GLfloat*)) \
    X(void,   glDeleteSamplers,         (GLsizei, const GLuint*)) \
    X(void,   glBindSampler,            (GLuint, GLuint)) \
    X(GLuint, glCreateShader,           (GLenum)) \
    X(void,   glShaderSource,           (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void,   glCompileShader,          (GLuint)) \
    X(void,   glGetShaderiv,            (GLuint, GLenum, GLint*)) \
    X(void,   glGetShaderInfoLog,       (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void,   glDeleteShader,           (GLuint)) \
    X(GLuint, glCreateProgram,          (void)) \
    X(void,   glAttachShader,           (GLuint, GLuint)) \
    X(void,   glLinkProgram,            (GLuint)) \
    X(void,   glGetProgramiv,           (GLuint, GLenum, GLint*)) \
    X(void,   glGetProgramInfoLog,      (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void,   glDeleteProgram,          (GLuint)) \
    X(void,   glUseProgram,             (GLuint)) \
    X(GLuint, glGetUniformBlockIndex,   (GLuint, const GLchar*)) \
    X(void,   glUniformBlockBinding,    (GLuint, GLuint, GLuint)) \
    X(GLint,  glGetUniformLocation,     (GLuint, const GLchar*)) \
    X(void,   glUniform1i,              (GLint, GLint)) \
    X(void,   glGenFramebuffers,        (GLsizei, GLuint*)) \
    X(void,   glBindFramebuffer,        (GLenum, GLuint)) \
    X(void,   glDeleteFramebuffers,     (GLsizei, const GLuint*)) \
    X(void,   glFramebufferTexture2D,   (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void,   glFramebufferTextureLayer,(GLenum, GLenum, GLuint, GLint, GLint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void,   glGenRenderbuffers,       (GLsizei, GLuint*)) \
    X(void,   glBindRenderbuffer,       (GLenum, GLuint)) \
    X(void,   glRenderbufferStorage,    (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void,   glFramebufferRenderbuffer,(GLenum, GLenum, GLenum, GLuint)) \
    X(void,   glDeleteRenderbuffers,    (GLsizei, const GLuint*)) \
    X(void,   glDrawBuffers,            (GLsizei, const GLenum*)) \
    X(void,   glBlitFramebuffer,        (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void,   glClearBufferfv,          (GLenum, GLint, const GLfloat*)) \
    X(void,   glClearBufferfi,          (GLenum, GLint, GLfloat, GLint)) \
    X(void,   glStencilOpSeparate,      (GLenum, GLenum, GLenum, GLenum)) \
    X(void,   glStencilFuncSeparate,    (GLenum, GLenum, GLint, GLuint)) \
    X(void,   glBlendFuncSeparate,      (GLenum, GLenum, GLenum, GLenum)) \
    X(void,   glBlendEquationSeparate,  (GLenum, GLenum)) \
    X(void,   glBlendColor,             (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,   glDisableVertexAttribArray,(GLuint)) \
    X(void,   glEnableVertexAttribArray, (GLuint)) \
    X(void,   glVertexAttribPointer,    (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void,   glVertexAttribIPointer,   (GLuint, GLint, GLenum, GLsizei, const void*)) \
    X(void,   glVertexAttribDivisor,    (GLuint, GLuint)) \
    X(void,   glDrawElementsInstanced,  (GLenum, GLsizei, GLenum, const void*, GLsizei)) \
    X(void,   glDrawArraysInstanced,    (GLenum, GLint, GLsizei, GLsizei))

/* 函数指针（静态，命名即函数名——调用侧无需 #define 重映射） */
#define X(ret, name, params) typedef ret (APIENTRY *PFN_sc_##name) params; static PFN_sc_##name name = NULL;
SCGL_WIN_FUNCS(X)
#undef X

/* wglGetProcAddress 装载（对 1.1 名可能返回 0/1/2/3/-1 的非址值，退回 GetProcAddress）。
 * 本表全为 1.2+，正常返真址；退化分支仅作稳健兜底。 */
static void* scgl_win_proc(HMODULE gl, const char* n) {
    void* p = (void*)wglGetProcAddress(n);
    if (p == (void*)0 || p == (void*)1 || p == (void*)2 ||
        p == (void*)3 || p == (void*)-1)
        p = (void*)GetProcAddress(gl, n);
    return p;
}

/* 装载全部 GL 1.2+ 函数指针（须在 GL 上下文 make current 后调用）。全部命中返 true。 */
static bool scgl_win_load(void) {
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) gl = LoadLibraryA("opengl32.dll");
    if (!gl) return false;
    int missing = 0;
#define X(ret, name, params) \
    name = (PFN_sc_##name)scgl_win_proc(gl, #name); \
    if (!name) missing++;
    SCGL_WIN_FUNCS(X)
#undef X
    return missing == 0;
}

#endif /* SC_GPU_GL_WIN_H */
