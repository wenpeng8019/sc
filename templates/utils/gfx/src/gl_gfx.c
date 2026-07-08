/* ============================================================
 * gl_gfx.c —— OpenGL 渲染后端（GL 4.1 core：macOS 上限）
 * ============================================================
 * 与 Metal 渲染后端同构的 vtable 实现；上下文/交换链在 gpu 模块
 * （env 层：gl_env.c + gl_ctx.c），本文件假定上下文已 current。
 *
 * 版本约束（macOS 上限 GL 4.1 core，对应 scc tar glcore@410）：
 *   · 无 compute / SSBO（dispatch、storage 绑定报不支持）
 *   · 无 shader 内 explicit binding → 链接后按反射清单的名字
 *     解析 uniform 块索引 / sampler location，手动指派绑定点
 *
 * 绑定点分配（GL 全局命名空间 vs Metal 每阶段槽位）：
 *   uniform 块绑定点 = stage * MAX_UNIFORM_BLOCKS + slot
 *   纹理单元        = stage * MAX_IMAGES + slot
 *
 * uniform 数据：单 UBO 环缓冲（每帧孤儿化重分配），
 *   apply_uniforms 追加后 glBindBufferRange 到对应绑定点。
 *
 * 与 gpu（env 层）的衔接：
 *   · 交换链 pass：sc_gpu_frame_acquire() 取 fbo（默认 0）+ 像素尺寸
 *   · commit：sc_gpu_frame_end()（env 对触达的 surface swapBuffers）
 * ============================================================ */

#ifdef SC_GPU_GL

#include "internal.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
#elif defined(__linux__)
  #define GL_GLEXT_PROTOTYPES
  #include <GL/gl.h>
  #include <GL/glext.h>
  #include <EGL/egl.h>
  /* GL_OES_EGL_image（memimg 绑定：EGLImage → 纹理） */
  typedef void (*PFN_scEGLImageTargetTexture2DOES)(GLenum target, void* image);
#else
  #error "gl_gfx.c: Windows 需 GL 加载器（待补）"
#endif

#define GL_UB_RING_SIZE (1 * 1024 * 1024)

/* ---- 后端私有体 -------------------------------------------- */

typedef struct GlBuffer {
    GLuint buf;
    GLenum target;        /* GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER */
} GlBuffer;

typedef struct GlImage {
    GLuint tex;
    GLenum target;        /* GL_TEXTURE_2D / CUBE_MAP / 3D / 2D_ARRAY */
} GlImage;

typedef struct GlSampler {
    GLuint smp;
} GlSampler;

typedef struct GlShader {
    GLuint prog;
} GlShader;

typedef struct GlPipeline {
    GLenum primitive;
    GLenum indexType;     /* 0 = 非索引 */
    int    indexSize;
} GlPipeline;

/* ---- 全局状态 ---------------------------------------------- */

static struct {
    GLuint  ubRing;
    int     ubPos;
    int     ubAlign;
    bool    ubOrphaned;    /* 本帧是否已孤儿化 */

    /* 帧内状态 */
    gfx_pipeline_t* curPip;
    int     curIndexOffset;
    GLuint  curFbo;        /* 离屏 pass 的临时 FBO（0 = 交换链） */
    GLuint  resolveFbo;    /* MSAA 解析用 */
    int     curPassWidth, curPassHeight;
    bool    inPass;
    gfx_image_t* passResolveSrc[SC_GFX_MAX_COLOR_ATTACHMENTS];
    gfx_image_t* passResolveDst[SC_GFX_MAX_COLOR_ATTACHMENTS];
    sc_gfx_attachment passResolveAtt[SC_GFX_MAX_COLOR_ATTACHMENTS];
    int     passColorCount;
} gl;

/* ---- 格式映射 ---------------------------------------------- */

typedef struct GlFormatInfo {
    GLenum internal, format, type;
} GlFormatInfo;

static GlFormatInfo toGlFormat(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_R8:      return (GlFormatInfo){ GL_R8, GL_RED, GL_UNSIGNED_BYTE };
        case SC_GPU_PIXELFORMAT_R16F:    return (GlFormatInfo){ GL_R16F, GL_RED, GL_HALF_FLOAT };
        case SC_GPU_PIXELFORMAT_R32F:    return (GlFormatInfo){ GL_R32F, GL_RED, GL_FLOAT };
        case SC_GPU_PIXELFORMAT_RG8:     return (GlFormatInfo){ GL_RG8, GL_RG, GL_UNSIGNED_BYTE };
        case SC_GPU_PIXELFORMAT_RG16F:   return (GlFormatInfo){ GL_RG16F, GL_RG, GL_HALF_FLOAT };
        case SC_GPU_PIXELFORMAT_RG32F:   return (GlFormatInfo){ GL_RG32F, GL_RG, GL_FLOAT };
        case SC_GPU_PIXELFORMAT_SRGB8A8: return (GlFormatInfo){ GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case SC_GPU_PIXELFORMAT_DEFAULT:
        case SC_GPU_PIXELFORMAT_BGRA8:   /* GL 内部无 BGRA 内部格式：存 RGBA8 */
        case SC_GPU_PIXELFORMAT_RGBA8:   return (GlFormatInfo){ GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case SC_GPU_PIXELFORMAT_RGB10A2: return (GlFormatInfo){ GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV };
        case SC_GPU_PIXELFORMAT_RGBA16F: return (GlFormatInfo){ GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT };
        case SC_GPU_PIXELFORMAT_RGBA32F: return (GlFormatInfo){ GL_RGBA32F, GL_RGBA, GL_FLOAT };
        case SC_GPU_PIXELFORMAT_DEPTH:   return (GlFormatInfo){ GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT };
        case SC_GPU_PIXELFORMAT_DEPTH_STENCIL:
            return (GlFormatInfo){ GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV };
        default:                         return (GlFormatInfo){ 0, 0, 0 };
    }
}

/* 顶点格式 → (分量数, 类型, 归一化) */
static void toGlVertexFormat(sc_gfx_vertex_format f, GLint* size, GLenum* type,
                             GLboolean* norm, bool* isInt) {
    *isInt = false; *norm = GL_FALSE;
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:    *size = 1; *type = GL_FLOAT; break;
        case SC_GFX_VERTEXFORMAT_FLOAT2:   *size = 2; *type = GL_FLOAT; break;
        case SC_GFX_VERTEXFORMAT_FLOAT3:   *size = 3; *type = GL_FLOAT; break;
        case SC_GFX_VERTEXFORMAT_FLOAT4:   *size = 4; *type = GL_FLOAT; break;
        case SC_GFX_VERTEXFORMAT_BYTE4:    *size = 4; *type = GL_BYTE; *isInt = true; break;
        case SC_GFX_VERTEXFORMAT_BYTE4N:   *size = 4; *type = GL_BYTE; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_UBYTE4:   *size = 4; *type = GL_UNSIGNED_BYTE; *isInt = true; break;
        case SC_GFX_VERTEXFORMAT_UBYTE4N:  *size = 4; *type = GL_UNSIGNED_BYTE; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_SHORT2:   *size = 2; *type = GL_SHORT; *isInt = true; break;
        case SC_GFX_VERTEXFORMAT_SHORT2N:  *size = 2; *type = GL_SHORT; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_SHORT4:   *size = 4; *type = GL_SHORT; *isInt = true; break;
        case SC_GFX_VERTEXFORMAT_SHORT4N:  *size = 4; *type = GL_SHORT; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_USHORT2:  *size = 2; *type = GL_UNSIGNED_SHORT; *isInt = true; break;
        case SC_GFX_VERTEXFORMAT_USHORT2N: *size = 2; *type = GL_UNSIGNED_SHORT; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_USHORT4:  *size = 4; *type = GL_UNSIGNED_SHORT; *isInt = true; break;
        case SC_GFX_VERTEXFORMAT_USHORT4N: *size = 4; *type = GL_UNSIGNED_SHORT; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_HALF2:    *size = 2; *type = GL_HALF_FLOAT; break;
        case SC_GFX_VERTEXFORMAT_HALF4:    *size = 4; *type = GL_HALF_FLOAT; break;
        case SC_GFX_VERTEXFORMAT_UINT10N2: *size = 4; *type = GL_UNSIGNED_INT_2_10_10_10_REV; *norm = GL_TRUE; break;
        case SC_GFX_VERTEXFORMAT_UINT:     *size = 1; *type = GL_UNSIGNED_INT; *isInt = true; break;
        default:                           *size = 0; *type = 0; break;
    }
}

static GLenum toGlCompare(sc_gfx_compare c) {
    switch (c) {
        case SC_GFX_COMPARE_NEVER:         return GL_NEVER;
        case SC_GFX_COMPARE_LESS:          return GL_LESS;
        case SC_GFX_COMPARE_EQUAL:         return GL_EQUAL;
        case SC_GFX_COMPARE_LESS_EQUAL:    return GL_LEQUAL;
        case SC_GFX_COMPARE_GREATER:       return GL_GREATER;
        case SC_GFX_COMPARE_NOT_EQUAL:     return GL_NOTEQUAL;
        case SC_GFX_COMPARE_GREATER_EQUAL: return GL_GEQUAL;
        default:                           return GL_ALWAYS;
    }
}

static GLenum toGlStencilOp(sc_gfx_stencil_op op) {
    switch (op) {
        case SC_GFX_STENCILOP_ZERO:       return GL_ZERO;
        case SC_GFX_STENCILOP_REPLACE:    return GL_REPLACE;
        case SC_GFX_STENCILOP_INCR_CLAMP: return GL_INCR;
        case SC_GFX_STENCILOP_DECR_CLAMP: return GL_DECR;
        case SC_GFX_STENCILOP_INVERT:     return GL_INVERT;
        case SC_GFX_STENCILOP_INCR_WRAP:  return GL_INCR_WRAP;
        case SC_GFX_STENCILOP_DECR_WRAP:  return GL_DECR_WRAP;
        default:                          return GL_KEEP;
    }
}

static GLenum toGlBlendFactor(sc_gfx_blend_factor f) {
    switch (f) {
        case SC_GFX_BLEND_ZERO:                  return GL_ZERO;
        case SC_GFX_BLEND_SRC_COLOR:             return GL_SRC_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_SRC_COLOR:   return GL_ONE_MINUS_SRC_COLOR;
        case SC_GFX_BLEND_SRC_ALPHA:             return GL_SRC_ALPHA;
        case SC_GFX_BLEND_ONE_MINUS_SRC_ALPHA:   return GL_ONE_MINUS_SRC_ALPHA;
        case SC_GFX_BLEND_DST_COLOR:             return GL_DST_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_DST_COLOR:   return GL_ONE_MINUS_DST_COLOR;
        case SC_GFX_BLEND_DST_ALPHA:             return GL_DST_ALPHA;
        case SC_GFX_BLEND_ONE_MINUS_DST_ALPHA:   return GL_ONE_MINUS_DST_ALPHA;
        case SC_GFX_BLEND_SRC_ALPHA_SATURATED:   return GL_SRC_ALPHA_SATURATE;
        case SC_GFX_BLEND_BLEND_COLOR:           return GL_CONSTANT_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_BLEND_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
        default:                                 return GL_ONE;
    }
}

static GLenum toGlBlendOp(sc_gfx_blend_op op) {
    switch (op) {
        case SC_GFX_BLENDOP_SUBTRACT:         return GL_FUNC_SUBTRACT;
        case SC_GFX_BLENDOP_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
        case SC_GFX_BLENDOP_MIN:              return GL_MIN;
        case SC_GFX_BLENDOP_MAX:              return GL_MAX;
        default:                              return GL_FUNC_ADD;
    }
}

static GLenum toGlWrap(sc_gfx_wrap w) {
    switch (w) {
        case SC_GFX_WRAP_CLAMP:  return GL_CLAMP_TO_EDGE;
        case SC_GFX_WRAP_MIRROR: return GL_MIRRORED_REPEAT;
        case SC_GFX_WRAP_BORDER: return GL_CLAMP_TO_BORDER;
        default:                 return GL_REPEAT;
    }
}

static GLenum toGlPrimitive(sc_gfx_primitive p) {
    switch (p) {
        case SC_GFX_PRIMITIVE_POINTS:         return GL_POINTS;
        case SC_GFX_PRIMITIVE_LINES:          return GL_LINES;
        case SC_GFX_PRIMITIVE_LINE_STRIP:     return GL_LINE_STRIP;
        case SC_GFX_PRIMITIVE_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        default:                              return GL_TRIANGLES;
    }
}

/* ---- init/shutdown ----------------------------------------- */

/* uniform 环：懒建（首帧首个 pass 时上下文必已 current） */
static void ensureUbRing(void) {
    if (gl.ubRing) return;
    glGenBuffers(1, &gl.ubRing);
    glBindBuffer(GL_UNIFORM_BUFFER, gl.ubRing);
    glBufferData(GL_UNIFORM_BUFFER, GL_UB_RING_SIZE, NULL, GL_STREAM_DRAW);
    GLint align = 256;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    gl.ubAlign = align > 0 ? align : 256;
}

static bool glGfxInit(const sc_gfx_desc* desc) {
    (void)desc;
    memset(&gl, 0, sizeof(gl));
    return true;
}

static void glGfxShutdown(void) {
    if (gl.ubRing) glDeleteBuffers(1, &gl.ubRing);
    if (gl.resolveFbo) glDeleteFramebuffers(1, &gl.resolveFbo);
    memset(&gl, 0, sizeof(gl));
}

static void glGfxFinish(void) {
    glFinish();
}

/* ---- buffer ------------------------------------------------ */

static GLenum glBufUsage(sc_gfx_usage u) {
    switch (u) {
        case SC_GFX_USAGE_DYNAMIC: return GL_DYNAMIC_DRAW;
        case SC_GFX_USAGE_STREAM:  return GL_STREAM_DRAW;
        default:                   return GL_STATIC_DRAW;
    }
}

static bool glBufferCreate(gfx_buffer_t* buf) {
    if (buf->desc.kind == SC_GFX_BUFFERKIND_STORAGE) {
        gfx_log("gl: 4.1 无 SSBO（storage buffer 不支持）");
        return false;
    }
    GlBuffer* b = (GlBuffer*)calloc(1, sizeof(GlBuffer));
    if (!b) return false;
    b->target = buf->desc.kind == SC_GFX_BUFFERKIND_INDEX ? GL_ELEMENT_ARRAY_BUFFER
                                                          : GL_ARRAY_BUFFER;
    glGenBuffers(1, &b->buf);
    glBindBuffer(b->target, b->buf);
    glBufferData(b->target, (GLsizeiptr)buf->desc.size,
                 buf->desc.data.ptr, glBufUsage(buf->desc.usage));
    buf->backend = b;
    return true;
}

static void glBufferDestroy(gfx_buffer_t* buf) {
    GlBuffer* b = (GlBuffer*)buf->backend;
    if (!b) return;
    glDeleteBuffers(1, &b->buf);
    free(b);
    buf->backend = NULL;
}

static void glBufferUpdate(gfx_buffer_t* buf, const sc_gfx_range* data, int offset) {
    GlBuffer* b = (GlBuffer*)buf->backend;
    if (!b) return;
    glBindBuffer(b->target, b->buf);
    /* stream 整段替换时孤儿化，避免与在飞绘制串行 */
    if (offset == 0 && data->size == buf->desc.size &&
        buf->desc.usage == SC_GFX_USAGE_STREAM)
        glBufferData(b->target, (GLsizeiptr)buf->desc.size, NULL, GL_STREAM_DRAW);
    glBufferSubData(b->target, offset, (GLsizeiptr)data->size, data->ptr);
}

/* ---- image ------------------------------------------------- */

static int glFormatByteSize(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_R8:      return 1;
        case SC_GPU_PIXELFORMAT_R16F:
        case SC_GPU_PIXELFORMAT_RG8:     return 2;
        case SC_GPU_PIXELFORMAT_RG32F:
        case SC_GPU_PIXELFORMAT_RGBA16F: return 8;
        case SC_GPU_PIXELFORMAT_RGBA32F: return 16;
        default:                         return 4;
    }
}

static void imageUploadData(gfx_image_t* img, GlImage* m,
                            const sc_gfx_image_data* data) {
    const sc_gfx_image_desc* d = &img->desc;
    GlFormatInfo fi = toGlFormat(d->format);
    int faces = (d->kind == SC_GFX_IMAGEKIND_CUBE) ? 6 : 1;
    int bpp = glFormatByteSize(d->format);
    glBindTexture(m->target, m->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (int f = 0; f < faces; f++) {
        for (int mip = 0; mip < d->mip_count; mip++) {
            const sc_gfx_range* r = &data->subimage[f][mip];
            int mw = d->width  >> mip; if (mw < 1) mw = 1;
            int mh = d->height >> mip; if (mh < 1) mh = 1;
            const void* p = r->ptr;   /* NULL = 仅分配 */
            (void)bpp;
            switch (d->kind) {
                case SC_GFX_IMAGEKIND_CUBE:
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f, mip,
                                 (GLint)fi.internal, mw, mh, 0, fi.format, fi.type, p);
                    break;
                case SC_GFX_IMAGEKIND_3D: {
                    int md = d->slices >> mip; if (md < 1) md = 1;
                    glTexImage3D(GL_TEXTURE_3D, mip, (GLint)fi.internal,
                                 mw, mh, md, 0, fi.format, fi.type, p);
                    break;
                }
                case SC_GFX_IMAGEKIND_ARRAY:
                    glTexImage3D(GL_TEXTURE_2D_ARRAY, mip, (GLint)fi.internal,
                                 mw, mh, d->slices, 0, fi.format, fi.type, p);
                    break;
                default:
                    glTexImage2D(GL_TEXTURE_2D, mip, (GLint)fi.internal,
                                 mw, mh, 0, fi.format, fi.type, p);
                    break;
            }
        }
    }
}

static bool glImageCreate(gfx_image_t* img) {
    GlImage* m = (GlImage*)calloc(1, sizeof(GlImage));
    if (!m) return false;
    const sc_gfx_image_desc* d = &img->desc;

    /* memimg 绑定：纹理存储来自 gpu env 的 EGLImage（linux） */
    if (d->memimg) {
#if defined(__linux__)
        static PFN_scEGLImageTargetTexture2DOES pImageTarget;
        if (!pImageTarget) {
            pImageTarget = (PFN_scEGLImageTargetTexture2DOES)
                eglGetProcAddress("glEGLImageTargetTexture2DOES");
            if (!pImageTarget) {
                gfx_log("gl: 无 GL_OES_EGL_image 扩展");
                free(m);
                return false;
            }
        }
        void* eglimg = sc_gpu_memimg_native(d->memimg);
        if (!eglimg) {
            gfx_log("gl: memimg %u 无效", d->memimg);
            free(m);
            return false;
        }
        m->target = GL_TEXTURE_2D;
        glGenTextures(1, &m->tex);
        glBindTexture(GL_TEXTURE_2D, m->tex);
        pImageTarget(GL_TEXTURE_2D, eglimg);
        img->backend = m;
        return true;
#else
        gfx_log("gl: mac 上 memimg 绑定请用 Metal 后端");
        free(m);
        return false;
#endif
    }

    GlFormatInfo fi = toGlFormat(d->format);
    if (!fi.internal) { free(m); return false; }

    switch (d->kind) {
        case SC_GFX_IMAGEKIND_CUBE:  m->target = GL_TEXTURE_CUBE_MAP; break;
        case SC_GFX_IMAGEKIND_3D:    m->target = GL_TEXTURE_3D; break;
        case SC_GFX_IMAGEKIND_ARRAY: m->target = GL_TEXTURE_2D_ARRAY; break;
        default:
            m->target = d->sample_count > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
            break;
    }
    glGenTextures(1, &m->tex);
    glBindTexture(m->target, m->tex);
    if (m->target == GL_TEXTURE_2D_MULTISAMPLE) {
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, d->sample_count,
                                fi.internal, d->width, d->height, GL_TRUE);
    } else {
        glTexParameteri(m->target, GL_TEXTURE_MAX_LEVEL, d->mip_count - 1);
        imageUploadData(img, m, &d->data);
    }
    img->backend = m;
    return true;
}

static void glImageDestroy(gfx_image_t* img) {
    GlImage* m = (GlImage*)img->backend;
    if (!m) return;
    glDeleteTextures(1, &m->tex);
    free(m);
    img->backend = NULL;
}

static void glImageUpdate(gfx_image_t* img, const sc_gfx_image_data* data) {
    GlImage* m = (GlImage*)img->backend;
    if (!m || img->desc.render_target) return;
    imageUploadData(img, m, data);
}

/* ---- sampler ----------------------------------------------- */

static bool glSamplerCreate(gfx_sampler_t* smp) {
    GlSampler* m = (GlSampler*)calloc(1, sizeof(GlSampler));
    if (!m) return false;
    const sc_gfx_sampler_desc* d = &smp->desc;
    glGenSamplers(1, &m->smp);

    GLenum minf;
    if (d->min_filter == SC_GFX_FILTER_LINEAR)
        minf = d->mipmap_filter == SC_GFX_FILTER_LINEAR ? GL_LINEAR_MIPMAP_LINEAR
             : d->mipmap_filter == SC_GFX_FILTER_NEAREST ? GL_LINEAR_MIPMAP_NEAREST
             : GL_LINEAR;
    else
        minf = d->mipmap_filter == SC_GFX_FILTER_LINEAR ? GL_NEAREST_MIPMAP_LINEAR
             : d->mipmap_filter == SC_GFX_FILTER_NEAREST ? GL_NEAREST_MIPMAP_NEAREST
             : GL_NEAREST;
    glSamplerParameteri(m->smp, GL_TEXTURE_MIN_FILTER, (GLint)minf);
    glSamplerParameteri(m->smp, GL_TEXTURE_MAG_FILTER,
        d->mag_filter == SC_GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(m->smp, GL_TEXTURE_WRAP_S, (GLint)toGlWrap(d->wrap_u));
    glSamplerParameteri(m->smp, GL_TEXTURE_WRAP_T, (GLint)toGlWrap(d->wrap_v));
    glSamplerParameteri(m->smp, GL_TEXTURE_WRAP_R, (GLint)toGlWrap(d->wrap_w));
    glSamplerParameterf(m->smp, GL_TEXTURE_MIN_LOD, d->min_lod);
    glSamplerParameterf(m->smp, GL_TEXTURE_MAX_LOD, d->max_lod > 0.0f ? d->max_lod : 1000.0f);
    if (d->max_anisotropy > 1) {
        /* GL_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FE（4.1 经 EXT 扩展） */
        glSamplerParameterf(m->smp, 0x84FE, (float)d->max_anisotropy);
    }
    if (d->wrap_u == SC_GFX_WRAP_BORDER || d->wrap_v == SC_GFX_WRAP_BORDER ||
        d->wrap_w == SC_GFX_WRAP_BORDER) {
        float bc[4] = { 0, 0, 0, 0 };
        if (d->border_color == SC_GFX_BORDERCOLOR_OPAQUE_BLACK) bc[3] = 1.0f;
        else if (d->border_color == SC_GFX_BORDERCOLOR_OPAQUE_WHITE) {
            bc[0] = bc[1] = bc[2] = bc[3] = 1.0f;
        }
        glSamplerParameterfv(m->smp, GL_TEXTURE_BORDER_COLOR, bc);
    }
    if (d->compare != SC_GFX_COMPARE_ALWAYS) {
        glSamplerParameteri(m->smp, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m->smp, GL_TEXTURE_COMPARE_FUNC, (GLint)toGlCompare(d->compare));
    }
    smp->backend = m;
    return true;
}

static void glSamplerDestroy(gfx_sampler_t* smp) {
    GlSampler* m = (GlSampler*)smp->backend;
    if (!m) return;
    glDeleteSamplers(1, &m->smp);
    free(m);
    smp->backend = NULL;
}

/* ---- shader ------------------------------------------------ */

static GLuint compileGlStage(GLenum kind, const sc_gfx_range* code) {
    GLuint sh = glCreateShader(kind);
    const GLchar* src = (const GLchar*)code->ptr;
    GLint len = (GLint)code->size;
    glShaderSource(sh, 1, &src, &len);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        gfx_log("gl: 着色器编译失败: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static bool glShaderCreate(gfx_shader_t* shd, const sc_gfx_shader_desc* desc) {
    if (desc->cs.code.ptr) {
        gfx_log("gl: 4.1 无 compute（计算着色器不支持）");
        return false;
    }
    GlShader* m = (GlShader*)calloc(1, sizeof(GlShader));
    if (!m) return false;

    GLuint vs = compileGlStage(GL_VERTEX_SHADER, &desc->vs.code);
    GLuint fs = compileGlStage(GL_FRAGMENT_SHADER, &desc->fs.code);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        free(m);
        return false;
    }
    m->prog = glCreateProgram();
    glAttachShader(m->prog, vs);
    glAttachShader(m->prog, fs);
    glLinkProgram(m->prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(m->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m->prog, sizeof(log), NULL, log);
        gfx_log("gl: 程序链接失败: %s", log);
        glDeleteProgram(m->prog);
        free(m);
        return false;
    }

    /* 4.1 无 shader 内 explicit binding：按反射清单名字解析并指派 */
    glUseProgram(m->prog);
    const gfx_reflect* r = &shd->reflect;
    for (int i = 0; i < r->block_count; i++) {
        const gfx_reflect_block* b = &r->blocks[i];
        GLuint idx = glGetUniformBlockIndex(m->prog, b->name);
        if (idx != GL_INVALID_INDEX) {
            int stage = b->stage < 0 ? 0 : b->stage;
            glUniformBlockBinding(m->prog, idx,
                (GLuint)(stage * SC_GFX_MAX_UNIFORM_BLOCKS + b->slot));
        }
    }
    for (int i = 0; i < r->sampler_count; i++) {
        const gfx_reflect_sampler* s = &r->samplers[i];
        GLint loc = glGetUniformLocation(m->prog, s->name);
        if (loc >= 0) {
            int stage = s->stage < 0 ? 1 : s->stage;   /* 默认按 fs */
            glUniform1i(loc, stage * SC_GFX_MAX_IMAGES + s->slot);
        }
    }
    glUseProgram(0);
    shd->backend = m;
    return true;
}

static void glShaderDestroy(gfx_shader_t* shd) {
    GlShader* m = (GlShader*)shd->backend;
    if (!m) return;
    glDeleteProgram(m->prog);
    free(m);
    shd->backend = NULL;
}

/* ---- pipeline ---------------------------------------------- */

static bool glPipelineCreate(gfx_pipeline_t* pip) {
    if (pip->desc.compute) {
        gfx_log("gl: 4.1 无 compute（计算管线不支持）");
        return false;
    }
    GlPipeline* m = (GlPipeline*)calloc(1, sizeof(GlPipeline));
    if (!m) return false;
    m->primitive = toGlPrimitive(pip->desc.primitive);
    if (pip->desc.index_type == SC_GFX_INDEXTYPE_UINT32) {
        m->indexType = GL_UNSIGNED_INT; m->indexSize = 4;
    } else if (pip->desc.index_type == SC_GFX_INDEXTYPE_UINT16) {
        m->indexType = GL_UNSIGNED_SHORT; m->indexSize = 2;
    }
    pip->backend = m;
    return true;
}

static void glPipelineDestroy(gfx_pipeline_t* pip) {
    GlPipeline* m = (GlPipeline*)pip->backend;
    if (!m) return;
    free(m);
    pip->backend = NULL;
}

/* ---- 帧 ---------------------------------------------------- */

static void glAttachTexture(GLenum attach, gfx_image_t* img, const sc_gfx_attachment* a) {
    GlImage* m = (GlImage*)img->backend;
    switch (img->desc.kind) {
        case SC_GFX_IMAGEKIND_CUBE:
            glFramebufferTexture2D(GL_FRAMEBUFFER, attach,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)a->slice, m->tex, a->mip);
            break;
        case SC_GFX_IMAGEKIND_3D:
        case SC_GFX_IMAGEKIND_ARRAY:
            glFramebufferTextureLayer(GL_FRAMEBUFFER, attach, m->tex, a->mip, a->slice);
            break;
        default:
            glFramebufferTexture2D(GL_FRAMEBUFFER, attach,
                m->target, m->tex, a->mip);
            break;
    }
}

static void glBeginPass(const sc_gfx_pass* pass, gfx_image_t* colors[],
                        int color_count, gfx_image_t* resolves[],
                        gfx_image_t* depth) {
    if (pass->compute) {
        gfx_log("gl: 4.1 无 compute pass");
        return;
    }
    gl.inPass = true;
    gl.passColorCount = color_count;
    memset(gl.passResolveSrc, 0, sizeof(gl.passResolveSrc));
    memset(gl.passResolveDst, 0, sizeof(gl.passResolveDst));

    /* 本帧首个 pass：uniform 环孤儿化 */
    ensureUbRing();
    if (!gl.ubOrphaned) {
        glBindBuffer(GL_UNIFORM_BUFFER, gl.ubRing);
        glBufferData(GL_UNIFORM_BUFFER, GL_UB_RING_SIZE, NULL, GL_STREAM_DRAW);
        gl.ubPos = 0;
        gl.ubOrphaned = true;
    }

    if (color_count == 0) {
        /* 交换链 pass：目标经 gpu env 交付 */
        sc_gpu_frame f;
        if (!sc_gpu_frame_acquire(&f)) {
            gfx_log("gl: frame_acquire 失败");
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, f.gl_fbo);
        gl.curFbo = f.gl_fbo;
        gl.curPassWidth = f.width;
        gl.curPassHeight = f.height;
    } else {
        /* 离屏 pass：临时 FBO */
        gl.curPassWidth = colors[0]->desc.width >> pass->colors[0].mip;
        gl.curPassHeight = colors[0]->desc.height >> pass->colors[0].mip;
        if (gl.curPassWidth < 1) gl.curPassWidth = 1;
        if (gl.curPassHeight < 1) gl.curPassHeight = 1;

        glGenFramebuffers(1, &gl.curFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, gl.curFbo);
        GLenum bufs[SC_GFX_MAX_COLOR_ATTACHMENTS];
        for (int i = 0; i < color_count; i++) {
            glAttachTexture(GL_COLOR_ATTACHMENT0 + (GLenum)i, colors[i], &pass->colors[i]);
            bufs[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
            if (resolves[i]) {
                gl.passResolveSrc[i] = colors[i];
                gl.passResolveDst[i] = resolves[i];
                gl.passResolveAtt[i] = pass->resolves[i];
            }
        }
        glDrawBuffers(color_count, bufs);
        if (depth) {
            GLenum att = depth->desc.format == SC_GPU_PIXELFORMAT_DEPTH_STENCIL
                       ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            glAttachTexture(att, depth, &pass->depth_stencil);
        }
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            gfx_log("gl: FBO 不完整");
    }

    /* load action：清屏（关 scissor / 开写掩码后清） */
    glViewport(0, 0, gl.curPassWidth, gl.curPassHeight);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);

    int nclear = color_count == 0 ? 1 : color_count;
    for (int i = 0; i < nclear; i++) {
        if (pass->action.colors[i].load == SC_GFX_LOADACTION_CLEAR) {
            glClearBufferfv(GL_COLOR, i, pass->action.colors[i].clear);
        }
    }
    if (pass->action.depth.load == SC_GFX_LOADACTION_CLEAR) {
        glClearBufferfi(GL_DEPTH_STENCIL, 0, pass->action.depth.clear_depth,
                        pass->action.depth.clear_stencil);
    }
}

static void clampRect(int* x, int* y, int* w, int* h, bool topLeft) {
    /* GL 原点在左下：top_left 语义须翻转 */
    if (topLeft) *y = gl.curPassHeight - (*y + *h);
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > gl.curPassWidth)  *w = gl.curPassWidth - *x;
    if (*y + *h > gl.curPassHeight) *h = gl.curPassHeight - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

static void glApplyViewport(int x, int y, int w, int h, bool topLeft) {
    clampRect(&x, &y, &w, &h, topLeft);
    glViewport(x, y, w, h);
}

static void glApplyScissor(int x, int y, int w, int h, bool topLeft) {
    clampRect(&x, &y, &w, &h, topLeft);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
}

static void glApplyPipeline(gfx_pipeline_t* pip) {
    GlPipeline* m = (GlPipeline*)pip->backend;
    GlShader* shd = (GlShader*)pip->shader->backend;
    if (!m || !shd) return;
    gl.curPip = pip;
    const sc_gfx_pipeline_desc* d = &pip->desc;

    glUseProgram(shd->prog);

    /* 深度 */
    if (d->depth.format != SC_GPU_PIXELFORMAT_NONE &&
        (d->depth.compare != SC_GFX_COMPARE_ALWAYS || d->depth.write_enabled)) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(toGlCompare(d->depth.compare));
        glDepthMask(d->depth.write_enabled ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    if (d->depth.bias != 0.0f || d->depth.bias_slope_scale != 0.0f) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(d->depth.bias_slope_scale, d->depth.bias);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    /* 模板 */
    if (d->stencil.enabled) {
        glEnable(GL_STENCIL_TEST);
        glStencilOpSeparate(GL_FRONT,
            toGlStencilOp(d->stencil.front.fail_op),
            toGlStencilOp(d->stencil.front.depth_fail_op),
            toGlStencilOp(d->stencil.front.pass_op));
        glStencilFuncSeparate(GL_FRONT, toGlCompare(d->stencil.front.compare),
            d->stencil.ref, d->stencil.read_mask);
        glStencilOpSeparate(GL_BACK,
            toGlStencilOp(d->stencil.back.fail_op),
            toGlStencilOp(d->stencil.back.depth_fail_op),
            toGlStencilOp(d->stencil.back.pass_op));
        glStencilFuncSeparate(GL_BACK, toGlCompare(d->stencil.back.compare),
            d->stencil.ref, d->stencil.read_mask);
        glStencilMask(d->stencil.write_mask);
    } else {
        glDisable(GL_STENCIL_TEST);
    }

    /* 混合（GL 全局；取 colors[0]） */
    const sc_gfx_blend_state* bl = &d->colors[0].blend;
    if (bl->enabled) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(toGlBlendFactor(bl->src_factor_rgb),
                            toGlBlendFactor(bl->dst_factor_rgb),
                            toGlBlendFactor(bl->src_factor_alpha),
                            toGlBlendFactor(bl->dst_factor_alpha));
        glBlendEquationSeparate(toGlBlendOp(bl->op_rgb), toGlBlendOp(bl->op_alpha));
        glBlendColor(d->blend_color[0], d->blend_color[1],
                     d->blend_color[2], d->blend_color[3]);
    } else {
        glDisable(GL_BLEND);
    }
    int wm = d->colors[0].write_mask;
    if (wm)
        glColorMask((wm & 1) != 0, (wm & 2) != 0, (wm & 4) != 0, (wm & 8) != 0);
    else
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* 光栅化 */
    if (d->cull != SC_GFX_CULL_NONE) {
        glEnable(GL_CULL_FACE);
        glCullFace(d->cull == SC_GFX_CULL_FRONT ? GL_FRONT : GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
    glFrontFace(d->winding == SC_GFX_WINDING_CW ? GL_CW : GL_CCW);
    if (d->alpha_to_coverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}

static void glApplyBindings(gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                            gfx_buffer_t* vbufs[], gfx_buffer_t* ibuf,
                            gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                            gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                            gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]) {
    (void)sbufs;
    const sc_gfx_pipeline_desc* d = &pip->desc;

    /* 顶点属性：按管线布局逐属性设置指针 */
    GLuint lastBuf = 0;
    for (int i = 0; i < SC_GFX_MAX_VERTEX_ATTRS; i++) {
        const sc_gfx_vertex_attr* a = &d->attrs[i];
        if (a->format == SC_GFX_VERTEXFORMAT_INVALID) { glDisableVertexAttribArray((GLuint)i); continue; }
        gfx_buffer_t* vb = vbufs[a->buffer_index];
        if (!vb || !vb->backend) continue;
        GlBuffer* b = (GlBuffer*)vb->backend;
        if (b->buf != lastBuf) {
            glBindBuffer(GL_ARRAY_BUFFER, b->buf);
            lastBuf = b->buf;
        }
        GLint size; GLenum type; GLboolean norm; bool isInt;
        toGlVertexFormat(a->format, &size, &type, &norm, &isInt);
        const void* off = (const void*)(uintptr_t)(a->offset +
            bnd->vertex_buffer_offsets[a->buffer_index]);
        glEnableVertexAttribArray((GLuint)i);
        if (isInt)
            glVertexAttribIPointer((GLuint)i, size, type,
                d->buffers[a->buffer_index].stride, off);
        else
            glVertexAttribPointer((GLuint)i, size, type, norm,
                d->buffers[a->buffer_index].stride, off);
        glVertexAttribDivisor((GLuint)i,
            d->buffers[a->buffer_index].step_per_instance ? 1 : 0);
    }

    /* 索引缓冲 */
    if (ibuf && ibuf->backend)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GlBuffer*)ibuf->backend)->buf);
    else
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gl.curIndexOffset = bnd->index_buffer_offset;

    /* 纹理 + 采样器：单元 = stage * MAX_IMAGES + slot */
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < SC_GFX_MAX_IMAGES; i++) {
            if (!imgs[s][i]) continue;
            GlImage* im = (GlImage*)imgs[s][i]->backend;
            glActiveTexture((GLenum)(GL_TEXTURE0 + s * SC_GFX_MAX_IMAGES + i));
            glBindTexture(im->target, im->tex);
            if (i < SC_GFX_MAX_SAMPLERS && smps[s][i])
                glBindSampler((GLuint)(s * SC_GFX_MAX_IMAGES + i),
                              ((GlSampler*)smps[s][i]->backend)->smp);
        }
    }
}

static void glApplyUniforms(int stage, int slot, const void* data, size_t size) {
    ensureUbRing();
    int pos = (gl.ubPos + gl.ubAlign - 1) & ~(gl.ubAlign - 1);
    if ((size_t)pos + size > GL_UB_RING_SIZE) {
        gfx_log("gl: uniform 环缓冲溢出");
        return;
    }
    glBindBuffer(GL_UNIFORM_BUFFER, gl.ubRing);
    glBufferSubData(GL_UNIFORM_BUFFER, pos, (GLsizeiptr)size, data);
    gl.ubPos = pos + (int)size;
    glBindBufferRange(GL_UNIFORM_BUFFER,
        (GLuint)(stage * SC_GFX_MAX_UNIFORM_BLOCKS + slot),
        gl.ubRing, pos, (GLsizeiptr)size);
}

static void glDraw(int base, int count, int instances) {
    if (!gl.curPip) return;
    GlPipeline* m = (GlPipeline*)gl.curPip->backend;
    if (m->indexType) {
        const void* off = (const void*)(uintptr_t)(gl.curIndexOffset +
                                                   base * m->indexSize);
        glDrawElementsInstanced(m->primitive, count, m->indexType, off, instances);
    } else {
        glDrawArraysInstanced(m->primitive, base, count, instances);
    }
}

static void glDispatch(int gx, int gy, int gz) {
    (void)gx; (void)gy; (void)gz;
    gfx_log("gl: 4.1 无 compute dispatch");
}

static void glEndPass(void) {
    /* MSAA 解析：blit 到 resolve 附件 */
    for (int i = 0; i < gl.passColorCount; i++) {
        if (!gl.passResolveDst[i]) continue;
        if (!gl.resolveFbo) glGenFramebuffers(1, &gl.resolveFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gl.curFbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0 + (GLenum)i);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.resolveFbo);
        GlImage* dst = (GlImage*)gl.passResolveDst[i]->backend;
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            dst->target, dst->tex, gl.passResolveAtt[i].mip);
        glBlitFramebuffer(0, 0, gl.curPassWidth, gl.curPassHeight,
                          0, 0, gl.curPassWidth, gl.curPassHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (gl.curFbo) {
        glDeleteFramebuffers(1, &gl.curFbo);
        gl.curFbo = 0;
    }
    glDisable(GL_SCISSOR_TEST);
    gl.curPip = NULL;
    gl.inPass = false;
}

static void glCommit(void) {
    sc_gpu_frame_end();   /* env 对本帧触达的 surface swapBuffers */
    gl.ubOrphaned = false;
}

/* ---- 能力查询 ----------------------------------------------- */

static void glQueryPixelformat(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out) {
    switch (fmt) {
        case SC_GPU_PIXELFORMAT_DEPTH:
        case SC_GPU_PIXELFORMAT_DEPTH_STENCIL:
            out->sample = 1; out->render = 1; out->msaa = 1; out->depth = 1;
            break;
        case SC_GPU_PIXELFORMAT_NONE:
            break;
        default:
            out->sample = 1; out->filter = 1; out->render = 1;
            out->blend = 1; out->msaa = 1;
            break;
    }
}

/* ---- vtable ------------------------------------------------ */

static const gfx_backend_api glApi = {
    .name = "gl",
    .init = glGfxInit,
    .shutdown = glGfxShutdown,
    .finish = glGfxFinish,
    .buffer_create = glBufferCreate,
    .buffer_destroy = glBufferDestroy,
    .buffer_update = glBufferUpdate,
    .image_create = glImageCreate,
    .image_destroy = glImageDestroy,
    .image_update = glImageUpdate,
    .sampler_create = glSamplerCreate,
    .sampler_destroy = glSamplerDestroy,
    .shader_create = glShaderCreate,
    .shader_destroy = glShaderDestroy,
    .pipeline_create = glPipelineCreate,
    .pipeline_destroy = glPipelineDestroy,
    .begin_pass = glBeginPass,
    .apply_viewport = glApplyViewport,
    .apply_scissor = glApplyScissor,
    .apply_pipeline = glApplyPipeline,
    .apply_bindings = glApplyBindings,
    .apply_uniforms = glApplyUniforms,
    .draw = glDraw,
    .dispatch = glDispatch,
    .end_pass = glEndPass,
    .commit = glCommit,
    .query_pixelformat = glQueryPixelformat,
};

const gfx_backend_api* gfx_backend_gl(void) { return &glApi; }

#endif /* SC_GPU_GL */
