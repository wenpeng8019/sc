/* ============================================================
 * gl_env.c —— OpenGL 运行环境后端
 * ============================================================
 * env 层职责（渲染命令翻译在 gfx 模块的 gl_gfx.c）：
 *   · 每 surface 一个 GL 上下文（gl_ctx.c：NSGL/WGL/GLX）+ 全局
 *     VAO（core profile 必需，属上下文级设置）
 *   · surface_activate = GL make current（gfx 假定上下文已 current）
 *   · frame_acquire：交付默认帧缓冲（fbo=0）+ 像素尺寸
 *   · frame_end：swapBuffers 本帧触达的所有 surface
 *   · 交换链 MSAA 暂不支持（sample_count 恒交付 1）
 *   · MEMORY surface（无表面）：headless 上下文 + memimg 环
 *     - linux = EGL surfaceless + dma-buf→EGLImage→tex（gl_egl.c）
 *     - mac   = NSGL 无 view 上下文 + IOSurface→CGLTexImageIOSurface2D
 *       →GL_TEXTURE_RECTANGLE（NSGL 唯一包装途径；作 FBO 附件 OK，
 *       采样需 sampler2DRect）；无导出 fence，frame_end glFinish
 *
 * 限制：多 surface 时上下文间对象未共享（TODO shareContext），
 * gfx 资源须在使用它的 surface 上下文中创建。
 * ============================================================ */

#include "internal.h"   /* 先引入：后端宏按目标平台自推导（见 internal.h） */
#ifdef SC_GPU_GL

#include "gl_ctx.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
  #include <OpenGL/OpenGL.h>          /* CGL：当前上下文 */
  #include <OpenGL/CGLIOSurface.h>    /* CGLTexImageIOSurface2D */
  #include <IOSurface/IOSurfaceRef.h> /* IOSurface C API */
  #include <CoreFoundation/CoreFoundation.h>
  /* mac 无 EGL 原生 fence：dequeue 前 frame_end 已 glFinish，sync_fd 恒 -1 */
#elif defined(__linux__)
  #if defined(SC_GPU_GLES)
    #include <GLES3/gl31.h>           /* 入库 Khronos 头（khr/） */
    #include <GLES2/gl2ext.h>
  #else
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
    #include <GL/glext.h>
  #endif
  #include "gl_egl.h"
  #include <unistd.h>
  /* GL_OES_EGL_image（Mesa 桌面 GL 亦暴露） */
  typedef void (*PFN_scEGLImageTargetTexture2DOES)(GLenum target, void* image);
#endif

#define GL_MAX_ACQUIRED 16

typedef struct GlSurface {
    /* WINDOW：桌面 = gl_ctx（NSGL/WGL/GLX）；SC_GPU_GLES = EGL 窗口 */
#if defined(SC_GPU_GLES)
    gl_egl_win* eglWin;
#else
    gl_ctx* ctx;
#endif
    GLuint      vao;      /* core profile / ES3 惯用的全局 VAO */
    /* MEMORY：环槽 memimg 包装纹理 + FBO；共享深度 rbo
     * linux = EGLImage→GL_TEXTURE_2D；mac = IOSurface→GL_TEXTURE_RECTANGLE */
#if defined(__linux__) || defined(__APPLE__)
    GLuint ringTex[SC_GPU_MAX_MEMORY_IMAGES];
    GLuint ringFbo[SC_GPU_MAX_MEMORY_IMAGES];
    GLuint depthRbo;
#endif
#if defined(__linux__)
    int    ringFence[SC_GPU_MAX_MEMORY_IMAGES];   /* frame_end 存的 sync_fd */
#endif
} GlSurface;

static struct {
    gpu_surface_t* acquired[GL_MAX_ACQUIRED];
    int acquiredCount;
#if defined(__linux__) || defined(__APPLE__)
    GLuint headlessVao;   /* headless 上下文的全局 VAO */
#endif
#if defined(__APPLE__)
    gl_ctx* headlessCtx;  /* 无屏 NSGL 上下文（view=NULL） */
#endif
#if defined(__linux__)
    PFN_scEGLImageTargetTexture2DOES pImageTarget;
#endif
} env;

/* ---- init/shutdown ----------------------------------------- */

static bool glInit(const sc_gpu_desc* desc) {
    (void)desc;
    memset(&env, 0, sizeof(env));
    return true;   /* 实际 GL 初始化随首个 surface 创建 */
}

static void glShutdown(void) {
#if defined(__linux__)
    gl_egl_shutdown();
#endif
#if defined(__APPLE__)
    if (env.headlessCtx) {
        gl_ctx_make_current(env.headlessCtx);
        if (env.headlessVao) glDeleteVertexArrays(1, &env.headlessVao);
        gl_ctx_destroy(env.headlessCtx);
    }
#endif
    memset(&env, 0, sizeof(env));
}

static void* glDevice(void) { return NULL; }   /* GL 无设备概念（上下文即环境） */

/* ---- surface ----------------------------------------------- */

#if defined(__linux__)
/* MEMORY surface（linux）：headless 上下文 + 每槽 EGLImage→tex+FBO */
static bool glMemorySurfaceCreate(gpu_surface_t* surf, GlSurface* s) {
    if (!gl_egl_init()) return false;
    gl_egl_make_current();
    if (!env.headlessVao) glGenVertexArrays(1, &env.headlessVao);
    glBindVertexArray(env.headlessVao);
    if (!env.pImageTarget) {
        env.pImageTarget = (PFN_scEGLImageTargetTexture2DOES)
            gl_get_proc("glEGLImageTargetTexture2DOES");
        if (!env.pImageTarget) {
            gpu_log("gl: 无 GL_OES_EGL_image 扩展");
            return false;
        }
    }

    if (surf->desc.sample_count > 1)
        gpu_log("gl: MEMORY surface MSAA 暂不支持（忽略）");

    if (surf->desc.depth_format != SC_GPU_PIXELFORMAT_NONE) {
        glGenRenderbuffers(1, &s->depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, s->depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              surf->desc.width, surf->desc.height);
    }
    for (int i = 0; i < surf->desc.image_count; i++) {
        gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[i]);
        void* eglimg = img ? gl_memimg_egl_image((gl_memimg*)img->backend) : NULL;
        if (!eglimg) return false;
        glGenTextures(1, &s->ringTex[i]);
        glBindTexture(GL_TEXTURE_2D, s->ringTex[i]);
        env.pImageTarget(GL_TEXTURE_2D, eglimg);
        glGenFramebuffers(1, &s->ringFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, s->ringFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, s->ringTex[i], 0);
        if (s->depthRbo)
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, s->depthRbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gpu_log("gl: MEMORY 环槽 %d FBO 不完整", i);
            return false;
        }
        s->ringFence[i] = -1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}
#endif

#if defined(__APPLE__)
/* mac memimg：IOSurface（与 metal_env 同源语），纯 C 用 CF API 创建 */
typedef struct GlMacMemimg {
    IOSurfaceRef iosurf;
} GlMacMemimg;

static IOSurfaceRef macIosurfCreate(int w, int h) {
    long bpr = (long)IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, (size_t)w * 4);
    long lw = w, lh = h, bpe = 4, pixf = (long)(uint32_t)'BGRA';
    const void* keys[5] = { kIOSurfaceWidth, kIOSurfaceHeight,
                            kIOSurfaceBytesPerElement, kIOSurfaceBytesPerRow,
                            kIOSurfacePixelFormat };
    CFNumberRef vals[5] = {
        CFNumberCreate(NULL, kCFNumberLongType, &lw),
        CFNumberCreate(NULL, kCFNumberLongType, &lh),
        CFNumberCreate(NULL, kCFNumberLongType, &bpe),
        CFNumberCreate(NULL, kCFNumberLongType, &bpr),
        CFNumberCreate(NULL, kCFNumberLongType, &pixf),
    };
    CFDictionaryRef props = CFDictionaryCreate(NULL, keys, (const void**)vals, 5,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    for (int i = 0; i < 5; i++) CFRelease(vals[i]);
    IOSurfaceRef s = IOSurfaceCreate(props);
    CFRelease(props);
    return s;
}

/* headless 上下文（首次需要时创建）+ make current */
static bool macHeadlessCurrent(void) {
    if (!env.headlessCtx) {
        env.headlessCtx = gl_ctx_create(NULL, NULL, 4, 1, 0);
        if (!env.headlessCtx) {
            gpu_log("gl: 无屏 NSGL 上下文创建失败");
            return false;
        }
        glGenVertexArrays(1, &env.headlessVao);
    }
    gl_ctx_make_current(env.headlessCtx);
    glBindVertexArray(env.headlessVao);
    return true;
}

/* IOSurface 包装为当前上下文的 GL_TEXTURE_RECTANGLE 纹理（NSGL 唯一途径；
 * 可作 FBO 附件；采样需 sampler2DRect，普通 sampler2D 不适用） */
static bool macWrapIosurfTex(IOSurfaceRef surf, int w, int h, GLuint* out_tex) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_RECTANGLE, tex);
    CGLError err = CGLTexImageIOSurface2D(CGLGetCurrentContext(),
                                          GL_TEXTURE_RECTANGLE, GL_RGBA8, w, h,
                                          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                                          surf, 0);
    if (err != kCGLNoError) {
        gpu_log("gl: CGLTexImageIOSurface2D 失败 (%d)", (int)err);
        glDeleteTextures(1, &tex);
        return false;
    }
    *out_tex = tex;
    return true;
}

/* MEMORY surface（mac）：headless 上下文 + 每槽 IOSurface→rect tex+FBO */
static bool glMemorySurfaceCreate(gpu_surface_t* surf, GlSurface* s) {
    if (!macHeadlessCurrent()) return false;

    if (surf->desc.sample_count > 1)
        gpu_log("gl: MEMORY surface MSAA 暂不支持（忽略）");

    if (surf->desc.depth_format != SC_GPU_PIXELFORMAT_NONE) {
        glGenRenderbuffers(1, &s->depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, s->depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              surf->desc.width, surf->desc.height);
    }
    for (int i = 0; i < surf->desc.image_count; i++) {
        gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[i]);
        GlMacMemimg* m = img ? (GlMacMemimg*)img->backend : NULL;
        if (!m || !m->iosurf) return false;
        if (!macWrapIosurfTex(m->iosurf, surf->desc.width, surf->desc.height,
                              &s->ringTex[i]))
            return false;
        glGenFramebuffers(1, &s->ringFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, s->ringFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_RECTANGLE, s->ringTex[i], 0);
        if (s->depthRbo)
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, s->depthRbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gpu_log("gl: MEMORY 环槽 %d FBO 不完整", i);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}
#endif /* __APPLE__ */

static bool glSurfaceCreate(gpu_surface_t* surf) {
    GlSurface* s = (GlSurface*)calloc(1, sizeof(GlSurface));
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__) || defined(__APPLE__)
        if (!glMemorySurfaceCreate(surf, s)) {
            /* 失败清理由 surface_destroy 兼顾 */
            surf->backend = s;
            return false;
        }
        surf->backend = s;
        return true;
#else
        gpu_log("gl: 本平台 MEMORY surface 未支持");
        free(s);
        return false;
#endif
    }

    if (surf->desc.sample_count > 1)
        gpu_log("gl: 交换链 MSAA 暂不支持（忽略 sample_count）");
#if defined(SC_GPU_GLES)
    /* GLES：EGL 窗口 surface（ES3 优先，回落 ES2） */
    s->eglWin = gl_egl_win_create(surf->desc.native_display,
                                  surf->desc.native_window,
                                  surf->desc.swap_interval);
    if (!s->eglWin) { free(s); return false; }
#else
    /* 桌面：macOS 上限 4.1 core（scc tar glcore@410 对应） */
    s->ctx = gl_ctx_create(surf->desc.native_window, surf->desc.native_display,
                               4, 1, surf->desc.swap_interval);
    if (!s->ctx) { free(s); return false; }
#endif
    /* 创建后即为当前上下文 */
    glGenVertexArrays(1, &s->vao);
    glBindVertexArray(s->vao);
    surf->backend = s;
    return true;
}

static void glSurfaceDestroy(gpu_surface_t* surf) {
    GlSurface* s = (GlSurface*)surf->backend;
    if (!s) return;
    for (int i = 0; i < env.acquiredCount; i++) {
        if (env.acquired[i] == surf) {
            env.acquired[i] = env.acquired[--env.acquiredCount];
            break;
        }
    }
#if defined(__linux__) || defined(__APPLE__)
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__)
        gl_egl_make_current();
#else
        if (env.headlessCtx) gl_ctx_make_current(env.headlessCtx);
#endif
        for (int i = 0; i < surf->desc.image_count; i++) {
            if (s->ringFbo[i]) glDeleteFramebuffers(1, &s->ringFbo[i]);
            if (s->ringTex[i]) glDeleteTextures(1, &s->ringTex[i]);
#if defined(__linux__)
            if (s->ringFence[i] >= 0) close(s->ringFence[i]);
#endif
        }
        if (s->depthRbo) glDeleteRenderbuffers(1, &s->depthRbo);
        free(s);
        surf->backend = NULL;
        return;
    }
#endif
#if defined(SC_GPU_GLES)
    if (s->eglWin) {
        gl_egl_win_make_current(s->eglWin);
        if (s->vao) glDeleteVertexArrays(1, &s->vao);
        gl_egl_win_destroy(s->eglWin);
    }
#else
    if (s->ctx) {
        gl_ctx_make_current(s->ctx);
        if (s->vao) glDeleteVertexArrays(1, &s->vao);
        gl_ctx_destroy(s->ctx);
    }
#endif
    free(s);
    surf->backend = NULL;
}

static void glSurfaceActivate(gpu_surface_t* surf) {
    if (surf && surf->backend) {
        GlSurface* s = (GlSurface*)surf->backend;
        if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__)
            gl_egl_make_current();
            glBindVertexArray(env.headlessVao);
#elif defined(__APPLE__)
            macHeadlessCurrent();
#endif
            return;
        }
#if defined(SC_GPU_GLES)
        gl_egl_win_make_current(s->eglWin);
#else
        gl_ctx_make_current(s->ctx);
#endif
        glBindVertexArray(s->vao);
    } else {
#if defined(SC_GPU_GLES)
        gl_egl_win_make_current(NULL);
#else
        gl_ctx_make_current(NULL);
#endif
    }
}

static void glSurfaceResize(gpu_surface_t* surf, int w, int h) {
    (void)w; (void)h;
#if defined(SC_GPU_GLES)
    (void)surf;   /* EGL 窗口 surface 尺寸随 native window，无需显式 update */
#else
    GlSurface* s = (GlSurface*)surf->backend;
    if (s) gl_ctx_resize(s->ctx);
#endif
}

/* ---- 帧交付 ------------------------------------------------ */

static bool glFrameAcquire(gpu_surface_t* surf, sc_gpu_frame* f) {
    GlSurface* s = (GlSurface*)surf->backend;
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__) || defined(__APPLE__)
        if (surf->ring_cur < 0) return false;
        f->gl_fbo = s->ringFbo[surf->ring_cur];
        f->width = surf->desc.width;
        f->height = surf->desc.height;
        f->sample_count = 1;
        f->color_format = surf->desc.color_format;
        f->depth_format = s->depthRbo ? surf->desc.depth_format
                                      : SC_GPU_PIXELFORMAT_NONE;
#else
        return false;
#endif
    } else {
        f->gl_fbo = 0;
        f->width = surf->desc.width;
        f->height = surf->desc.height;
        f->sample_count = 1;
        f->color_format = surf->desc.color_format;
        f->depth_format = surf->desc.depth_format;   /* 上下文像素格式自带 depth24s8 */
    }

    bool listed = false;
    for (int i = 0; i < env.acquiredCount; i++)
        if (env.acquired[i] == surf) { listed = true; break; }
    if (!listed && env.acquiredCount < GL_MAX_ACQUIRED)
        env.acquired[env.acquiredCount++] = surf;
    return true;
}

static void glFrameEnd(void) {
    for (int i = 0; i < env.acquiredCount; i++) {
        gpu_surface_t* surf = env.acquired[i];
        GlSurface* s = (GlSurface*)surf->backend;
        if (!s) continue;
        if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__)
            /* 栅栏随命令流提交，存入本帧槽位（dequeue 时交付） */
            if (surf->ring_cur >= 0) {
                if (s->ringFence[surf->ring_cur] >= 0)
                    close(s->ringFence[surf->ring_cur]);
                s->ringFence[surf->ring_cur] = gl_egl_fence_fd();
                if (s->ringFence[surf->ring_cur] < 0)
                    glFinish();   /* 无 fence 扩展 → CPU 同步退化 */
            }
#elif defined(__APPLE__)
            glFinish();   /* NSGL 无导出 fence：CPU 同步，dequeue 即可消费 */
#endif
        } else {
#if defined(SC_GPU_GLES)
            gl_egl_win_swap(s->eglWin);
#else
            gl_ctx_swap(s->ctx);   /* swap 不改变 current */
#endif
        }
    }
    env.acquiredCount = 0;
}

/* ---- memimg（linux：dma-buf/EGLImage） ---------------------- */

#if defined(__linux__)

static bool glMemimgAlloc(gpu_memimg_t* img) {
    gl_memimg* m = gl_memimg_alloc(img->desc.width, img->desc.height,
                                           img->desc.fourcc, img->desc.memory,
                                           img->desc.modifier,
                                           img->desc.render_target);
    img->backend = m;
    return m != NULL;
}

static bool glMemimgImport(gpu_memimg_t* img, const sc_gpu_memory_frame* src) {
    gl_memimg* m = gl_memimg_import(src);
    img->backend = m;
    return m != NULL;
}

static bool glMemimgExport(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence) {
    if (!gl_memimg_export((gl_memimg*)img->backend, out)) return false;
    if (with_fence) {
        out->sync_fd = gl_egl_fence_fd();
        if (out->sync_fd < 0) glFinish();
    }
    return true;
}

static void* glMemimgNative(gpu_memimg_t* img) {
    return gl_memimg_egl_image((gl_memimg*)img->backend);
}

static void* glMemimgMap(gpu_memimg_t* img, int plane, uint32_t* out_stride) {
    (void)plane;
    return gl_memimg_map((gl_memimg*)img->backend, out_stride);
}

static void glMemimgUnmap(gpu_memimg_t* img, int plane) {
    (void)plane;
    gl_memimg_unmap((gl_memimg*)img->backend);
}

static void glMemimgFree(gpu_memimg_t* img) {
    gl_memimg_free((gl_memimg*)img->backend);
    img->backend = NULL;
}

static bool glSurfaceDequeue(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out) {
    GlSurface* s = (GlSurface*)surf->backend;
    gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[slot]);
    if (!s || !img) return false;
    if (!gl_memimg_export((gl_memimg*)img->backend, out)) return false;
    out->sync_fd = s->ringFence[slot];   /* 所有权交给调用方 */
    s->ringFence[slot] = -1;
    return true;
}

#endif /* __linux__ */

/* ---- memimg（mac：IOSurface） ------------------------------ */

#if defined(__APPLE__)

static bool glMemimgAlloc(gpu_memimg_t* img) {
    const sc_gpu_memimg_desc* d = &img->desc;
    if (d->format != SC_GPU_PIXELFORMAT_BGRA8 &&
        d->format != SC_GPU_PIXELFORMAT_DEFAULT) {
        gpu_log("gl: mac memimg 暂仅支持 BGRA8");
        return false;
    }
    GlMacMemimg* m = (GlMacMemimg*)calloc(1, sizeof(GlMacMemimg));
    if (!m) return false;
    m->iosurf = macIosurfCreate(d->width, d->height);
    if (!m->iosurf) {
        gpu_log("gl: IOSurfaceCreate 失败");
        free(m);
        return false;
    }
    img->backend = m;
    return true;
}

static bool glMemimgImport(gpu_memimg_t* img, const sc_gpu_memory_frame* src) {
    /* mac 导入源 = IOSurfaceRef（native 字段） */
    if (!src->native) {
        gpu_log("gl: memimg_import 需 native=IOSurfaceRef");
        return false;
    }
    GlMacMemimg* m = (GlMacMemimg*)calloc(1, sizeof(GlMacMemimg));
    if (!m) return false;
    m->iosurf = (IOSurfaceRef)src->native;
    CFRetain(m->iosurf);
    img->backend = m;
    return true;
}

static bool glMemimgExport(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence) {
    GlMacMemimg* m = (GlMacMemimg*)img->backend;
    if (!m) return false;
    if (with_fence) glFinish();   /* mac GL 无导出 fence → CPU 同步 */
    out->planes = 1;
    out->fd[0] = -1;
    out->stride[0] = (uint32_t)IOSurfaceGetBytesPerRow(m->iosurf);
    out->offset[0] = 0;
    out->fourcc = img->desc.fourcc;
    out->width = img->desc.width;
    out->height = img->desc.height;
    out->sync_fd = -1;
    out->native = (void*)m->iosurf;   /* 借用；可直送 VideoToolbox */
    return true;
}

static void* glMemimgNative(gpu_memimg_t* img) {
    GlMacMemimg* m = (GlMacMemimg*)img->backend;
    return m ? (void*)m->iosurf : NULL;   /* gfx 后端用 CGLTexImageIOSurface2D 包装 */
}

static void* glMemimgMap(gpu_memimg_t* img, int plane, uint32_t* out_stride) {
    (void)plane;
    GlMacMemimg* m = (GlMacMemimg*)img->backend;
    if (!m) return NULL;
    IOSurfaceLock(m->iosurf, kIOSurfaceLockReadOnly, NULL);
    if (out_stride) *out_stride = (uint32_t)IOSurfaceGetBytesPerRow(m->iosurf);
    return IOSurfaceGetBaseAddress(m->iosurf);
}

static void glMemimgUnmap(gpu_memimg_t* img, int plane) {
    (void)plane;
    GlMacMemimg* m = (GlMacMemimg*)img->backend;
    if (m) IOSurfaceUnlock(m->iosurf, kIOSurfaceLockReadOnly, NULL);
}

static void glMemimgFree(gpu_memimg_t* img) {
    GlMacMemimg* m = (GlMacMemimg*)img->backend;
    if (!m) return;
    if (m->iosurf) CFRelease(m->iosurf);
    free(m);
    img->backend = NULL;
}

static bool glSurfaceDequeue(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out) {
    gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[slot]);
    if (!img) return false;
    return glMemimgExport(img, out, false);   /* frame_end 已 glFinish，sync_fd=-1 */
}

#endif /* __APPLE__ */

/* ---- vtable ------------------------------------------------ */

static const gpu_env_api glApi = {
    .name = "gl",
    .kind = SC_GPU_BACKEND_GL,
    .init = glInit,
    .shutdown = glShutdown,
    .device = glDevice,
    .surface_create = glSurfaceCreate,
    .surface_destroy = glSurfaceDestroy,
    .surface_activate = glSurfaceActivate,
    .surface_resize = glSurfaceResize,
    .frame_acquire = glFrameAcquire,
    .frame_end = glFrameEnd,
#if defined(__linux__) || defined(__APPLE__)
    .memimg_alloc = glMemimgAlloc,
    .memimg_import = glMemimgImport,
    .memimg_export = glMemimgExport,
    .memimg_native = glMemimgNative,
    .memimg_map = glMemimgMap,
    .memimg_unmap = glMemimgUnmap,
    .memimg_free = glMemimgFree,
    .surface_dequeue = glSurfaceDequeue,
#endif
};

const gpu_env_api* gpu_env_gl(void) { return &glApi; }

#endif /* SC_GPU_GL */
