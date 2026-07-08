/* ============================================================
 * gl_egl.c —— Linux EGL headless（GBM/DRM/dma-heap）
 * ============================================================
 * 参考 invo-avm 的 gbm_egl.c/buffer_device.c 精炼而来的现代化路径：
 *   · surfaceless 上下文（EGL_KHR_surfaceless_context），免 1×1
 *     pbuffer 技巧；GBM 平台 display（EGL_PLATFORM_GBM）
 *   · memimg 分配：GBM BO（设备最优）/ dma-heap CMA（物理连续）
 *     → EGLImage（EGL_LINUX_DMA_BUF_EXT，多平面 fd/stride/offset）
 *   · 显式同步：EGL_ANDROID_native_fence_sync 导出 sync_fd
 * 注意：仅 Linux 编译；本文件在 mac 开发机为盲写，板上验证。
 * ============================================================ */

#include "internal.h"   /* 先引入：后端宏按目标平台自推导（见 internal.h） */
#if defined(SC_GPU_GL) && defined(__linux__)

#include "gl_egl.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(SC_GPU_GLES)
  #include <GLES3/gl31.h>   /* 入库 Khronos 头（khr/），交叉编译免 sysroot */
#else
  #include <GL/gl.h>
#endif

/* eglext.h 老版本可能缺的常量兜底 */
#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif
#ifndef EGL_NO_NATIVE_FENCE_FD_ANDROID
#define EGL_NO_NATIVE_FENCE_FD_ANDROID -1
#endif

typedef struct gl_memimg {
    struct gbm_bo* bo;      /* GBM 模式 */
    int            dma_fd;  /* dma-heap 模式（bo 为 NULL 时有效） */
    EGLImageKHR    image;
    int            width, height;
    uint32_t       fourcc;
    int            planes;
    int            fd[4];
    uint32_t       stride[4], offset[4];
    size_t         size;    /* CPU 映射大小（plane0 起整段） */
    void*          map_ptr;
} gl_memimg;

static struct {
    bool               inited;
    int                drm_fd;
    int                dma_fd;      /* dma-heap（懒开） */
    struct gbm_device* gbm;
    EGLDisplay         dpy;
    EGLContext         ctx;
    bool               has_fence;

    PFNEGLGETPLATFORMDISPLAYEXTPROC   pGetPlatformDisplay;
    PFNEGLCREATEIMAGEKHRPROC          pCreateImage;
    PFNEGLDESTROYIMAGEKHRPROC         pDestroyImage;
    PFNEGLCREATESYNCKHRPROC           pCreateSync;
    PFNEGLDESTROYSYNCKHRPROC          pDestroySync;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC pDupFenceFd;
} egl;

/* ---- 初始化 ------------------------------------------------ */

bool gl_egl_init(void) {
    if (egl.inited) return true;

    egl.drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (egl.drm_fd < 0) egl.drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (egl.drm_fd < 0) {
        gpu_log("egl: 打开 DRM 设备失败(%d)", errno);
        return false;
    }
    egl.gbm = gbm_create_device(egl.drm_fd);
    if (!egl.gbm) {
        gpu_log("egl: 创建 GBM 设备失败(%d)", errno);
        close(egl.drm_fd); egl.drm_fd = -1;
        return false;
    }

    egl.pGetPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    egl.pCreateImage  = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    egl.pDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    egl.pCreateSync   = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    egl.pDestroySync  = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    egl.pDupFenceFd   = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)
        eglGetProcAddress("eglDupNativeFenceFDANDROID");
    if (!egl.pGetPlatformDisplay || !egl.pCreateImage || !egl.pDestroyImage) {
        gpu_log("egl: 缺少必需扩展入口");
        goto fail;
    }

    egl.dpy = egl.pGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, egl.gbm, NULL);
    if (egl.dpy == EGL_NO_DISPLAY || !eglInitialize(egl.dpy, NULL, NULL)) {
        gpu_log("egl: display 初始化失败(%d)", eglGetError());
        goto fail;
    }

    const char* exts = eglQueryString(egl.dpy, EGL_EXTENSIONS);
    bool surfaceless = exts && strstr(exts, "EGL_KHR_surfaceless_context");
    egl.has_fence = exts && strstr(exts, "EGL_ANDROID_native_fence_sync") &&
                    egl.pCreateSync && egl.pDupFenceFd;
    if (!surfaceless)
        gpu_log("egl: 无 surfaceless_context 扩展（尝试继续）");

    /* 桌面 GL 优先（对应 scc glcore410），失败回落 GLES */
    EGLint cfg_attribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLConfig cfg; EGLint ncfg = 0;
    EGLint api = EGL_OPENGL_API;
    if (!eglChooseConfig(egl.dpy, cfg_attribs, &cfg, 1, &ncfg) || ncfg < 1) {
        cfg_attribs[9] = EGL_OPENGL_ES2_BIT;
        api = EGL_OPENGL_ES_API;
        if (!eglChooseConfig(egl.dpy, cfg_attribs, &cfg, 1, &ncfg) || ncfg < 1) {
            gpu_log("egl: 无可用 config");
            goto fail;
        }
    }
    eglBindAPI((EGLenum)api);

    EGLint ctx_attribs_gl[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    EGLint ctx_attribs_es[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    egl.ctx = eglCreateContext(egl.dpy, cfg, EGL_NO_CONTEXT,
                               api == EGL_OPENGL_API ? ctx_attribs_gl : ctx_attribs_es);
    if (egl.ctx == EGL_NO_CONTEXT && api == EGL_OPENGL_API) {
        /* 4.1 core 不可用 → 3.3 core */
        ctx_attribs_gl[1] = 3; ctx_attribs_gl[3] = 3;
        egl.ctx = eglCreateContext(egl.dpy, cfg, EGL_NO_CONTEXT, ctx_attribs_gl);
    }
    if (egl.ctx == EGL_NO_CONTEXT) {
        gpu_log("egl: 创建上下文失败(%d)", eglGetError());
        goto fail;
    }
    if (!eglMakeCurrent(egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl.ctx)) {
        gpu_log("egl: surfaceless make current 失败(%d)", eglGetError());
        eglDestroyContext(egl.dpy, egl.ctx);
        goto fail;
    }

    egl.dma_fd = -1;
    egl.inited = true;
    return true;

fail:
    if (egl.dpy != EGL_NO_DISPLAY) { eglTerminate(egl.dpy); egl.dpy = EGL_NO_DISPLAY; }
    if (egl.gbm) { gbm_device_destroy(egl.gbm); egl.gbm = NULL; }
    if (egl.drm_fd >= 0) { close(egl.drm_fd); egl.drm_fd = -1; }
    return false;
}

void gl_egl_shutdown(void) {
    if (!egl.inited) return;
    eglMakeCurrent(egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl.ctx != EGL_NO_CONTEXT) eglDestroyContext(egl.dpy, egl.ctx);
    eglTerminate(egl.dpy);
    if (egl.gbm) gbm_device_destroy(egl.gbm);
    if (egl.drm_fd >= 0) close(egl.drm_fd);
    if (egl.dma_fd >= 0) close(egl.dma_fd);
    memset(&egl, 0, sizeof(egl));
    egl.drm_fd = egl.dma_fd = -1;
    egl.dpy = EGL_NO_DISPLAY;
}

void gl_egl_make_current(void) {
    if (egl.inited)
        eglMakeCurrent(egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl.ctx);
}

/* ---- memimg ------------------------------------------------ */

/* fourcc → 每像素字节（单平面 RGB 系；YUV 仅支持导入路径） */
static int fourccBpp(uint32_t fourcc) {
    switch (fourcc) {
        case SC_GPU_FOURCC('A','R','2','4'): case SC_GPU_FOURCC('X','R','2','4'):
        case SC_GPU_FOURCC('A','B','2','4'): case SC_GPU_FOURCC('X','B','2','4'):
        case SC_GPU_FOURCC('A','B','3','0'): return 4;
        default: return 0;
    }
}

static EGLImageKHR createDmaBufImage(int w, int h, uint32_t fourcc, int planes,
                                     const int fd[], const uint32_t stride[],
                                     const uint32_t offset[]) {
    EGLint attribs[32];
    int i = 0;
    attribs[i++] = EGL_WIDTH;  attribs[i++] = w;
    attribs[i++] = EGL_HEIGHT; attribs[i++] = h;
    attribs[i++] = EGL_LINUX_DRM_FOURCC_EXT; attribs[i++] = (EGLint)fourcc;
    static const EGLint planeAttrs[3][3] = {
        { EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE0_PITCH_EXT },
        { EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT },
        { EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT },
    };
    for (int p = 0; p < planes && p < 3; p++) {
        attribs[i++] = planeAttrs[p][0]; attribs[i++] = fd[p];
        attribs[i++] = planeAttrs[p][1]; attribs[i++] = (EGLint)offset[p];
        attribs[i++] = planeAttrs[p][2]; attribs[i++] = (EGLint)stride[p];
    }
    attribs[i] = EGL_NONE;
    return egl.pCreateImage(egl.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                            NULL, attribs);
}

gl_memimg* gl_memimg_alloc(int w, int h, uint32_t fourcc,
                                   sc_gpu_memory_kind memory, uint64_t modifier,
                                   int renderable) {
    if (!gl_egl_init()) return NULL;
    (void)modifier;   /* 预留：modifier 协商；首版 LINEAR */
    int bpp = fourccBpp(fourcc);
    if (!bpp) { gpu_log("egl: memimg 分配暂仅支持 RGB 系 fourcc"); return NULL; }

    gl_memimg* m = (gl_memimg*)calloc(1, sizeof(gl_memimg));
    if (!m) return NULL;
    m->dma_fd = -1;
    m->width = w; m->height = h; m->fourcc = fourcc;
    m->planes = 1;

    if (memory == SC_GPU_MEMORY_DMA_HEAP) {
        /* CMA dma-heap（物理连续，编码器要求时） */
        if (egl.dma_fd < 0) {
            egl.dma_fd = open("/dev/dma_heap/linux,cma", O_RDWR | O_CLOEXEC);
            if (egl.dma_fd < 0) {
                gpu_log("egl: 打开 dma-heap 失败(%d)", errno);
                free(m);
                return NULL;
            }
        }
        m->stride[0] = (uint32_t)(w * bpp);
        m->size = (size_t)m->stride[0] * (size_t)h;
        struct dma_heap_allocation_data ad = {
            .len = m->size, .fd_flags = O_RDWR | O_CLOEXEC, .heap_flags = 0,
        };
        if (ioctl(egl.dma_fd, DMA_HEAP_IOCTL_ALLOC, &ad) < 0) {
            gpu_log("egl: dma-heap 分配失败(%d)", errno);
            free(m);
            return NULL;
        }
        m->dma_fd = (int)ad.fd;
        m->fd[0] = m->dma_fd;
        m->offset[0] = 0;
    } else {
        /* GBM BO（默认；LINEAR 便于编码器直读） */
        uint32_t flags = GBM_BO_USE_LINEAR |
                         (renderable ? GBM_BO_USE_RENDERING : 0);
        m->bo = gbm_bo_create(egl.gbm, (uint32_t)w, (uint32_t)h, fourcc, flags);
        if (!m->bo) {
            gpu_log("egl: gbm_bo_create 失败(%d)", errno);
            free(m);
            return NULL;
        }
        m->planes = gbm_bo_get_plane_count(m->bo);
        if (m->planes > 4) m->planes = 4;
        for (int p = 0; p < m->planes; p++) {
            m->fd[p]     = gbm_bo_get_fd_for_plane(m->bo, p);
            m->stride[p] = gbm_bo_get_stride_for_plane(m->bo, p);
            m->offset[p] = gbm_bo_get_offset(m->bo, p);
        }
        m->size = (size_t)m->stride[0] * (size_t)h;
    }

    m->image = createDmaBufImage(w, h, fourcc, m->planes, m->fd, m->stride, m->offset);
    if (m->image == EGL_NO_IMAGE_KHR) {
        gpu_log("egl: eglCreateImage 失败(%d)", eglGetError());
        gl_memimg_free(m);
        return NULL;
    }
    return m;
}

gl_memimg* gl_memimg_import(const sc_gpu_memory_frame* src) {
    if (!gl_egl_init()) return NULL;
    gl_memimg* m = (gl_memimg*)calloc(1, sizeof(gl_memimg));
    if (!m) return NULL;
    m->dma_fd = -1;
    m->width = src->width; m->height = src->height;
    m->fourcc = src->fourcc;
    m->planes = src->planes > 4 ? 4 : src->planes;
    for (int p = 0; p < m->planes; p++) {
        m->fd[p] = src->fd[p];          /* 借用（不 dup、不 close） */
        m->stride[p] = src->stride[p];
        m->offset[p] = src->offset[p];
    }
    m->image = createDmaBufImage(m->width, m->height, m->fourcc,
                                 m->planes, m->fd, m->stride, m->offset);
    if (m->image == EGL_NO_IMAGE_KHR) {
        gpu_log("egl: 导入 eglCreateImage 失败(%d)", eglGetError());
        free(m);
        return NULL;
    }
    m->bo = NULL;   /* 导入：不拥有底层内存 */
    return m;
}

bool gl_memimg_export(gl_memimg* m, sc_gpu_memory_frame* out) {
    if (!m) return false;
    out->planes = m->planes;
    for (int p = 0; p < m->planes; p++) {
        out->fd[p] = m->fd[p];          /* 借用语义 */
        out->stride[p] = m->stride[p];
        out->offset[p] = m->offset[p];
    }
    out->fourcc = m->fourcc;
    out->width = m->width;
    out->height = m->height;
    out->native = NULL;
    return true;
}

void* gl_memimg_egl_image(gl_memimg* m) {
    return m ? (void*)m->image : NULL;
}

void* gl_memimg_map(gl_memimg* m, uint32_t* out_stride) {
    if (!m || m->fd[0] < 0) return NULL;
    if (!m->map_ptr) {
        m->map_ptr = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          m->fd[0], 0);
        if (m->map_ptr == MAP_FAILED) {
            gpu_log("egl: mmap 失败(%d)", errno);
            m->map_ptr = NULL;
            return NULL;
        }
    }
    if (out_stride) *out_stride = m->stride[0];
    return m->map_ptr;
}

void gl_memimg_unmap(gl_memimg* m) {
    if (m && m->map_ptr) {
        munmap(m->map_ptr, m->size);
        m->map_ptr = NULL;
    }
}

void gl_memimg_free(gl_memimg* m) {
    if (!m) return;
    gl_memimg_unmap(m);
    if (m->image != EGL_NO_IMAGE_KHR && egl.pDestroyImage)
        egl.pDestroyImage(egl.dpy, m->image);
    if (m->bo) {
        /* GBM：fd 是 get_fd_for_plane 拿到的 dup，须关闭 */
        for (int p = 0; p < m->planes; p++)
            if (m->fd[p] >= 0) close(m->fd[p]);
        gbm_bo_destroy(m->bo);
    } else if (m->dma_fd >= 0) {
        close(m->dma_fd);
    }
    /* 导入对象：fd 借用，不关闭 */
    free(m);
}

/* ---- fence ------------------------------------------------- */

int gl_egl_fence_fd(void) {
    if (!egl.inited || !egl.has_fence) return -1;
    static const EGLint attribs[] = { EGL_NONE };
    EGLSyncKHR sync = egl.pCreateSync(egl.dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);
    if (sync == EGL_NO_SYNC_KHR) return -1;
    glFlush();   /* fence 随命令流提交 */
    int fd = egl.pDupFenceFd(egl.dpy, sync);
    egl.pDestroySync(egl.dpy, sync);
    return fd;   /* -1 = 失败 */
}

/* ============================================================
 * window surface —— EGL 窗口路径（Wayland/X11/嵌入式）
 * ============================================================
 * 与 headless（GBM 平台 display）分属不同 EGLDisplay：窗口路径用
 * native_display（wl_display* / X11 Display*）。同进程多窗口共享
 * display（引用计数）；上下文暂不共享对象（同 gl_ctx TODO）。
 * SC_GPU_GLES 建 ES3（回落 ES2）；否则桌面 GL 4.1→3.3 core。
 * ============================================================ */

struct gl_egl_win {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surf;
};

static struct {
    EGLDisplay dpy;
    void*      native;
    int        refs;
} eglwin;

gl_egl_win* gl_egl_win_create(void* native_display, void* native_window,
                              int swap_interval) {
    if (!native_window) return NULL;
    if (eglwin.refs > 0 && eglwin.native != native_display) {
        gpu_log("egl: 多 native display 暂不支持");
        return NULL;
    }

    if (eglwin.refs == 0) {
        eglwin.dpy = eglGetDisplay((EGLNativeDisplayType)native_display);
        if (eglwin.dpy == EGL_NO_DISPLAY ||
            !eglInitialize(eglwin.dpy, NULL, NULL)) {
            gpu_log("egl: 窗口 display 初始化失败(%d)", eglGetError());
            return NULL;
        }
        eglwin.native = native_display;
    }

    EGLint cfg_attribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
#if defined(SC_GPU_GLES)
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
#else
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
#endif
        EGL_NONE
    };
    EGLConfig cfg; EGLint ncfg = 0;
#if defined(SC_GPU_GLES)
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(eglwin.dpy, cfg_attribs, &cfg, 1, &ncfg) || ncfg < 1) {
        cfg_attribs[15] = EGL_OPENGL_ES2_BIT;   /* ES3 config 无 → ES2 */
        if (!eglChooseConfig(eglwin.dpy, cfg_attribs, &cfg, 1, &ncfg) || ncfg < 1) {
            gpu_log("egl: 无可用窗口 config");
            goto fail_dpy;
        }
    }
#else
    eglBindAPI(EGL_OPENGL_API);
    if (!eglChooseConfig(eglwin.dpy, cfg_attribs, &cfg, 1, &ncfg) || ncfg < 1) {
        gpu_log("egl: 无可用窗口 config");
        goto fail_dpy;
    }
#endif

    gl_egl_win* w = (gl_egl_win*)calloc(1, sizeof(gl_egl_win));
    if (!w) goto fail_dpy;
    w->dpy = eglwin.dpy;

#if defined(SC_GPU_GLES)
    EGLint ctx3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLint ctx2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    w->ctx = eglCreateContext(w->dpy, cfg, EGL_NO_CONTEXT, ctx3);
    if (w->ctx == EGL_NO_CONTEXT)
        w->ctx = eglCreateContext(w->dpy, cfg, EGL_NO_CONTEXT, ctx2);
#else
    EGLint ctxgl[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    w->ctx = eglCreateContext(w->dpy, cfg, EGL_NO_CONTEXT, ctxgl);
    if (w->ctx == EGL_NO_CONTEXT) {
        ctxgl[1] = 3; ctxgl[3] = 3;   /* 4.1 不可用 → 3.3 core */
        w->ctx = eglCreateContext(w->dpy, cfg, EGL_NO_CONTEXT, ctxgl);
    }
#endif
    if (w->ctx == EGL_NO_CONTEXT) {
        gpu_log("egl: 窗口上下文创建失败(%d)", eglGetError());
        goto fail_w;
    }

    w->surf = eglCreateWindowSurface(w->dpy, cfg,
                                     (EGLNativeWindowType)native_window, NULL);
    if (w->surf == EGL_NO_SURFACE) {
        gpu_log("egl: 窗口 surface 创建失败(%d)", eglGetError());
        eglDestroyContext(w->dpy, w->ctx);
        goto fail_w;
    }

    if (!eglMakeCurrent(w->dpy, w->surf, w->surf, w->ctx)) {
        gpu_log("egl: 窗口 make current 失败(%d)", eglGetError());
        eglDestroySurface(w->dpy, w->surf);
        eglDestroyContext(w->dpy, w->ctx);
        goto fail_w;
    }
    eglSwapInterval(w->dpy, swap_interval);
    eglwin.refs++;
    return w;

fail_w:
    free(w);
fail_dpy:
    if (eglwin.refs == 0) {
        eglTerminate(eglwin.dpy);
        eglwin.dpy = EGL_NO_DISPLAY;
        eglwin.native = NULL;
    }
    return NULL;
}

void gl_egl_win_destroy(gl_egl_win* w) {
    if (!w) return;
    if (eglGetCurrentContext() == w->ctx)
        eglMakeCurrent(w->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(w->dpy, w->surf);
    eglDestroyContext(w->dpy, w->ctx);
    free(w);
    if (--eglwin.refs == 0) {
        eglTerminate(eglwin.dpy);
        eglwin.dpy = EGL_NO_DISPLAY;
        eglwin.native = NULL;
    }
}

void gl_egl_win_make_current(gl_egl_win* w) {
    if (w) eglMakeCurrent(w->dpy, w->surf, w->surf, w->ctx);
    else if (eglwin.dpy != EGL_NO_DISPLAY)
        eglMakeCurrent(eglwin.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void gl_egl_win_swap(gl_egl_win* w) {
    if (w) eglSwapBuffers(w->dpy, w->surf);
}

#endif /* SC_GPU_GL && __linux__ */
