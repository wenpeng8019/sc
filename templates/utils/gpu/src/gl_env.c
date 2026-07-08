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
 *
 * 限制：多 surface 时上下文间对象未共享（TODO shareContext），
 * gfx 资源须在使用它的 surface 上下文中创建。
 * ============================================================ */

#ifdef SC_GPU_GL

#include "internal.h"
#include "gl_ctx.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
#elif defined(__linux__)
  #define GL_GLEXT_PROTOTYPES
  #include <GL/gl.h>
  #include <GL/glext.h>
  #include "gl_egl.h"
  #include <unistd.h>
  /* GL_OES_EGL_image（Mesa 桌面 GL 亦暴露） */
  typedef void (*PFN_scEGLImageTargetTexture2DOES)(GLenum target, void* image);
#endif

#define GL_MAX_ACQUIRED 16

typedef struct GlSurface {
    /* WINDOW */
    gl_ctx* ctx;
    GLuint      vao;      /* core profile 必需的全局 VAO */
#if defined(__linux__)
    /* MEMORY：环槽 EGLImage 包装纹理 + FBO；共享深度 rbo */
    GLuint ringTex[SC_GPU_MAX_MEMORY_IMAGES];
    GLuint ringFbo[SC_GPU_MAX_MEMORY_IMAGES];
    int    ringFence[SC_GPU_MAX_MEMORY_IMAGES];   /* frame_end 存的 sync_fd */
    GLuint depthRbo;
#endif
} GlSurface;

static struct {
    gpu_surface_t* acquired[GL_MAX_ACQUIRED];
    int acquiredCount;
#if defined(__linux__)
    GLuint headlessVao;   /* headless 上下文的全局 VAO */
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

static bool glSurfaceCreate(gpu_surface_t* surf) {
    GlSurface* s = (GlSurface*)calloc(1, sizeof(GlSurface));
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__)
        if (!glMemorySurfaceCreate(surf, s)) {
            /* 失败清理由 surface_destroy 兼顾 */
            surf->backend = s;
            return false;
        }
        surf->backend = s;
        return true;
#else
        gpu_log("gl: mac 上 MEMORY surface 请用 Metal 后端");
        free(s);
        return false;
#endif
    }

    if (surf->desc.sample_count > 1)
        gpu_log("gl: 交换链 MSAA 暂不支持（忽略 sample_count）");
    /* macOS 上限 4.1 core（scc tar glcore@410 对应） */
    s->ctx = gl_ctx_create(surf->desc.native_window, surf->desc.native_display,
                               4, 1, surf->desc.swap_interval);
    if (!s->ctx) { free(s); return false; }
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
#if defined(__linux__)
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        gl_egl_make_current();
        for (int i = 0; i < surf->desc.image_count; i++) {
            if (s->ringFbo[i]) glDeleteFramebuffers(1, &s->ringFbo[i]);
            if (s->ringTex[i]) glDeleteTextures(1, &s->ringTex[i]);
            if (s->ringFence[i] >= 0) close(s->ringFence[i]);
        }
        if (s->depthRbo) glDeleteRenderbuffers(1, &s->depthRbo);
        free(s);
        surf->backend = NULL;
        return;
    }
#endif
    if (s->ctx) {
        gl_ctx_make_current(s->ctx);
        if (s->vao) glDeleteVertexArrays(1, &s->vao);
        gl_ctx_destroy(s->ctx);
    }
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
#endif
            return;
        }
        gl_ctx_make_current(s->ctx);
        glBindVertexArray(s->vao);
    } else {
        gl_ctx_make_current(NULL);
    }
}

static void glSurfaceResize(gpu_surface_t* surf, int w, int h) {
    (void)w; (void)h;
    GlSurface* s = (GlSurface*)surf->backend;
    if (s) gl_ctx_resize(s->ctx);
}

/* ---- 帧交付 ------------------------------------------------ */

static bool glFrameAcquire(gpu_surface_t* surf, sc_gpu_frame* f) {
    GlSurface* s = (GlSurface*)surf->backend;
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if defined(__linux__)
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
#endif
        } else {
            gl_ctx_swap(s->ctx);   /* swap 不改变 current */
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
#if defined(__linux__)
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
