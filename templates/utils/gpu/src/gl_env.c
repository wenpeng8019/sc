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
#endif

#define GL_MAX_ACQUIRED 16

typedef struct GlSurface {
    _sc_gl_ctx* ctx;
    GLuint      vao;      /* core profile 必需的全局 VAO */
} GlSurface;

static struct {
    _sc_gpu_surface_t* acquired[GL_MAX_ACQUIRED];
    int acquiredCount;
} env;

/* ---- init/shutdown ----------------------------------------- */

static bool glInit(const sc_gpu_desc* desc) {
    (void)desc;
    memset(&env, 0, sizeof(env));
    return true;   /* 实际 GL 初始化随首个 surface 创建 */
}

static void glShutdown(void) {
    memset(&env, 0, sizeof(env));
}

static void* glDevice(void) { return NULL; }   /* GL 无设备概念（上下文即环境） */

/* ---- surface ----------------------------------------------- */

static bool glSurfaceCreate(_sc_gpu_surface_t* surf) {
    GlSurface* s = (GlSurface*)calloc(1, sizeof(GlSurface));
    if (!s) return false;
    if (surf->desc.sample_count > 1)
        _sc_gpu_log("gl: 交换链 MSAA 暂不支持（忽略 sample_count）");
    /* macOS 上限 4.1 core（scc tar glcore@410 对应） */
    s->ctx = _sc_gl_ctx_create(surf->desc.native_window, surf->desc.native_display,
                               4, 1, surf->desc.swap_interval);
    if (!s->ctx) { free(s); return false; }
    /* 创建后即为当前上下文 */
    glGenVertexArrays(1, &s->vao);
    glBindVertexArray(s->vao);
    surf->backend = s;
    return true;
}

static void glSurfaceDestroy(_sc_gpu_surface_t* surf) {
    GlSurface* s = (GlSurface*)surf->backend;
    if (!s) return;
    for (int i = 0; i < env.acquiredCount; i++) {
        if (env.acquired[i] == surf) {
            env.acquired[i] = env.acquired[--env.acquiredCount];
            break;
        }
    }
    if (s->ctx) {
        _sc_gl_ctx_make_current(s->ctx);
        if (s->vao) glDeleteVertexArrays(1, &s->vao);
        _sc_gl_ctx_destroy(s->ctx);
    }
    free(s);
    surf->backend = NULL;
}

static void glSurfaceActivate(_sc_gpu_surface_t* surf) {
    if (surf && surf->backend) {
        GlSurface* s = (GlSurface*)surf->backend;
        _sc_gl_ctx_make_current(s->ctx);
        glBindVertexArray(s->vao);
    } else {
        _sc_gl_ctx_make_current(NULL);
    }
}

static void glSurfaceResize(_sc_gpu_surface_t* surf, int w, int h) {
    (void)w; (void)h;
    GlSurface* s = (GlSurface*)surf->backend;
    if (s) _sc_gl_ctx_resize(s->ctx);
}

/* ---- 帧交付 ------------------------------------------------ */

static bool glFrameAcquire(_sc_gpu_surface_t* surf, sc_gpu_frame* f) {
    GlSurface* s = (GlSurface*)surf->backend;
    if (!s) return false;
    f->gl_fbo = 0;
    f->width = surf->desc.width;
    f->height = surf->desc.height;
    f->sample_count = 1;
    f->color_format = surf->desc.color_format;
    f->depth_format = surf->desc.depth_format;   /* 上下文像素格式自带 depth24s8 */

    bool listed = false;
    for (int i = 0; i < env.acquiredCount; i++)
        if (env.acquired[i] == surf) { listed = true; break; }
    if (!listed && env.acquiredCount < GL_MAX_ACQUIRED)
        env.acquired[env.acquiredCount++] = surf;
    return true;
}

static void glFrameEnd(void) {
    for (int i = 0; i < env.acquiredCount; i++) {
        GlSurface* s = (GlSurface*)env.acquired[i]->backend;
        if (s) _sc_gl_ctx_swap(s->ctx);   /* swap 不改变 current */
    }
    env.acquiredCount = 0;
}

/* ---- vtable ------------------------------------------------ */

static const _sc_gpu_env_api glApi = {
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
};

const _sc_gpu_env_api* _sc_gpu_env_gl(void) { return &glApi; }

#endif /* SC_GPU_GL */
