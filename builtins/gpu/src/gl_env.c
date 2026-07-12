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

#define COBJMACROS       /* D3D11/DXGI 的 C 风格接口宏（ID3D11Device_XXX，WGL_NV_DX_interop2 用） */
#include "gl_ctx.h"
#include <stdlib.h>
#include <string.h>

#if P_DARWIN
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
  #include <OpenGL/OpenGL.h>          /* CGL：当前上下文 */
  #include <OpenGL/CGLIOSurface.h>    /* CGLTexImageIOSurface2D */
  #include <IOSurface/IOSurfaceRef.h> /* IOSurface C API */
  #include <CoreFoundation/CoreFoundation.h>
  /* mac 无 EGL 原生 fence：dequeue 前 frame_end 已 glFinish，sync_fd 恒 -1 */
#elif P_LINUX
  #if defined(SC_GPU_GLES)
    #include <GLES3/gl31.h>           /* 入库 Khronos 头（khr/） */
    #include <GLES2/gl2ext.h>
  #else
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
    #include <GL/glext.h>
  #endif
  #include <EGL/egl.h>                /* eglGetProcAddress（headless 路径的标准加载器） */
  #include "gl_egl.h"
  #include <unistd.h>
  /* GL_OES_EGL_image（Mesa 桌面 GL 亦暴露） */
  typedef void (*PFN_scEGLImageTargetTexture2DOES)(GLenum target, void* image);
#elif P_WIN
  #include "gl_win.h"                 /* 桌面 GL：windows.h + GL/gl.h + GL 1.2+ 加载器 */
  #include <d3d11.h>                  /* WGL_NV_DX_interop2 零拷贝导出：D3D11 共享纹理 */
  #include <d3d11_1.h>                /* ID3D11Device1::OpenSharedResource1（导入 NT 句柄） */
  #include <dxgi1_2.h>                /* IDXGIResource1::CreateSharedHandle（NT 句柄） */
  /* WGL_NV_DX_interop2 常量 + 入口点（经 wglGetProcAddress 装载） */
  #ifndef WGL_ACCESS_READ_WRITE_NV
  #define WGL_ACCESS_READ_ONLY_NV     0x00000000
  #define WGL_ACCESS_READ_WRITE_NV    0x00000001
  #define WGL_ACCESS_WRITE_DISCARD_NV 0x00000002
  #endif
  typedef HANDLE (WINAPI *PFN_wglDXOpenDeviceNV)(void* dxDevice);
  typedef BOOL   (WINAPI *PFN_wglDXCloseDeviceNV)(HANDLE hDevice);
  typedef HANDLE (WINAPI *PFN_wglDXRegisterObjectNV)(HANDLE hDevice, void* dxObject,
                                                     GLuint name, GLenum type, GLenum access);
  typedef BOOL   (WINAPI *PFN_wglDXUnregisterObjectNV)(HANDLE hDevice, HANDLE hObject);
  typedef BOOL   (WINAPI *PFN_wglDXLockObjectsNV)(HANDLE hDevice, GLint count, HANDLE* hObjects);
  typedef BOOL   (WINAPI *PFN_wglDXUnlockObjectsNV)(HANDLE hDevice, GLint count, HANDLE* hObjects);
#endif

/* GL 无表面渲染（MEMORY surface / memimg）支持面：
 *   linux 桌面 = GBM/dma-heap→EGLImage；Android(API26+) = AHardwareBuffer→EGLImage；
 *   mac = IOSurface；win = WGL+D3D interop。
 * SC_GPU_GL_EGL_HEADLESS = gl_egl.c 的 headless 段（gl_egl_init/shutdown/make_current
 *   + gl_memimg_*）是否编入：非-Android Linux 恒真；Android 需 API 26+（AHB 全套
 *   API 的最低要求），否则 Android 仅窗口交换链（gl_egl_win），memimg 优雅返回未支持。 */
#if P_LINUX && (!defined(__ANDROID__) || __ANDROID_API__ >= 26)
#define SC_GPU_GL_EGL_HEADLESS 1
#endif
#if SC_GPU_GL_EGL_HEADLESS || P_DARWIN || P_WIN
#define SC_GPU_GL_MEMIMG 1
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
     * linux = EGLImage→GL_TEXTURE_2D；mac = IOSurface→GL_TEXTURE_RECTANGLE；
     * win  = memimg 自身 GL_TEXTURE_2D（借用，不由 surface 释放） */
#if P_LINUX || P_DARWIN || P_WIN
    GLuint ringTex[SC_GPU_MAX_MEMORY_IMAGES];
    GLuint ringFbo[SC_GPU_MAX_MEMORY_IMAGES];
    GLuint depthRbo;
#endif
#if P_LINUX
    int    ringFence[SC_GPU_MAX_MEMORY_IMAGES];   /* frame_end 存的 sync_fd */
#endif
} GlSurface;

static struct {
    gpu_surface_t* acquired[GL_MAX_ACQUIRED];
    int acquiredCount;
#if P_LINUX || P_DARWIN || P_WIN
    GLuint headlessVao;   /* headless 上下文的全局 VAO */
#endif
#if P_DARWIN || P_WIN
    gl_ctx* headlessCtx;  /* 无屏上下文（mac NSGL view=NULL / win 隐藏窗口） */
#endif
#if P_LINUX
    PFN_scEGLImageTargetTexture2DOES pImageTarget;
#endif
#if P_WIN
    /* WGL_NV_DX_interop2 零拷贝导出：D3D11 设备 + 互操作设备 + 入口点（懒初始化） */
    ID3D11Device*        d3dDev;
    ID3D11DeviceContext* d3dCtx;
    HANDLE               interopDev;
    bool                 interopTried;
    PFN_wglDXOpenDeviceNV       pDXOpen;
    PFN_wglDXCloseDeviceNV      pDXClose;
    PFN_wglDXRegisterObjectNV   pDXReg;
    PFN_wglDXUnregisterObjectNV pDXUnreg;
    PFN_wglDXLockObjectsNV      pDXLock;
    PFN_wglDXUnlockObjectsNV    pDXUnlock;
#endif
} env;

/* ---- init/shutdown ----------------------------------------- */

static bool glInit(const sc_gpu_desc* desc) {
    (void)desc;
    memset(&env, 0, sizeof(env));
    return true;   /* 实际 GL 初始化随首个 surface 创建 */
}

static void glShutdown(void) {
#if SC_GPU_GL_EGL_HEADLESS
    gl_egl_shutdown();
#endif
#if P_WIN
    if (env.interopDev && env.pDXClose) {
        if (env.headlessCtx) gl_ctx_make_current(env.headlessCtx);
        env.pDXClose(env.interopDev);
    }
    if (env.d3dCtx) ID3D11DeviceContext_Release(env.d3dCtx);
    if (env.d3dDev) ID3D11Device_Release(env.d3dDev);
#endif
#if P_DARWIN || P_WIN
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

#if SC_GPU_GL_EGL_HEADLESS
/* MEMORY surface（linux GBM / Android AHB）：headless 上下文 + 每槽 EGLImage→tex+FBO */
static bool glMemorySurfaceCreate(gpu_surface_t* surf, GlSurface* s) {
    if (!gl_egl_init()) return false;
    gl_egl_make_current();
    if (!env.headlessVao) glGenVertexArrays(1, &env.headlessVao);
    glBindVertexArray(env.headlessVao);
    if (!env.pImageTarget) {
        /* EGL headless 路径的标准加载器：eglGetProcAddress（GLES 形态下
         * gl_ctx 整体空化，gl_get_proc 不存在；桌面 Mesa 亦支持） */
        env.pImageTarget = (PFN_scEGLImageTargetTexture2DOES)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");
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

#if P_DARWIN
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
#endif /* P_DARWIN */

/* ---- MEMORY surface / memimg（win：headless WGL + GL_TEXTURE_2D 回读） ---- */
#if P_WIN
typedef struct GlWinMemimg {
    GLuint tex;      /* GL_TEXTURE_2D，BGRA8 渲染目标 */
    GLuint roFbo;    /* 回读用 FBO（懒建） */
    int    w, h;
    void*  cpu;      /* 回读缓冲（w*h*4，BGRA） */
    /* WGL_NV_DX_interop2 零拷贝导出（互操作可用时；否则全 NULL，退化 CPU 回读） */
    ID3D11Texture2D* d3dTex;   /* 共享 D3D11 纹理（与 tex 互操作，GL 渲染即写入） */
    HANDLE           interop;  /* wglDXRegisterObjectNV 句柄 */
    HANDLE           shared;   /* 导出的共享 NT 句柄（借用；free 时 CloseHandle） */
    bool             locked;   /* 当前是否 lock 给 GL（渲染/回读需 locked，导出需 unlocked） */
} GlWinMemimg;

/* headless 上下文（首次需要时创建：隐藏窗口 WGL）+ make current */
static bool winHeadlessCurrent(void) {
    if (!env.headlessCtx) {
        env.headlessCtx = gl_ctx_create(NULL, NULL, 4, 1, 0);
        if (!env.headlessCtx) {
            gpu_log("gl: 无屏 WGL 上下文创建失败");
            return false;
        }
        scgl_win_load();   /* 上下文已 current（gl_ctx_create 末尾 make current）：装载 GL 1.2+ 指针 */
        glGenVertexArrays(1, &env.headlessVao);
    }
    gl_ctx_make_current(env.headlessCtx);
    glBindVertexArray(env.headlessVao);
    return true;
}

/* WGL_NV_DX_interop2 懒初始化：D3D11 设备 + 互操作设备 + 入口点装载。
 * 首次 memimg alloc 时调用；不可用则退化为普通 GL 纹理（仅 CPU 回读，native=NULL）。 */
static bool winInteropEnsure(void) {
    if (env.interopTried) return env.interopDev != NULL;
    env.interopTried = true;
    env.pDXOpen   = (PFN_wglDXOpenDeviceNV)wglGetProcAddress("wglDXOpenDeviceNV");
    env.pDXClose  = (PFN_wglDXCloseDeviceNV)wglGetProcAddress("wglDXCloseDeviceNV");
    env.pDXReg    = (PFN_wglDXRegisterObjectNV)wglGetProcAddress("wglDXRegisterObjectNV");
    env.pDXUnreg  = (PFN_wglDXUnregisterObjectNV)wglGetProcAddress("wglDXUnregisterObjectNV");
    env.pDXLock   = (PFN_wglDXLockObjectsNV)wglGetProcAddress("wglDXLockObjectsNV");
    env.pDXUnlock = (PFN_wglDXUnlockObjectsNV)wglGetProcAddress("wglDXUnlockObjectsNV");
    if (!env.pDXOpen || !env.pDXReg || !env.pDXUnreg || !env.pDXLock || !env.pDXUnlock) {
        gpu_log("gl: WGL_NV_DX_interop2 不可用，memimg 退化为 CPU 回读（无零拷贝导出）");
        return false;
    }
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                   D3D11_SDK_VERSION, &env.d3dDev, &fl, &env.d3dCtx);
    if (FAILED(hr) || !env.d3dDev) { gpu_log("gl: D3D11CreateDevice 失败"); return false; }
    env.interopDev = env.pDXOpen(env.d3dDev);
    if (!env.interopDev) { gpu_log("gl: wglDXOpenDeviceNV 失败"); return false; }
    gpu_log("gl: memimg 零拷贝导出可用（WGL_NV_DX_interop2 + D3D11 共享句柄）");
    return true;
}

/* interop 对象锁：GL 渲染/回读需 locked；导出给消费者前 unlock（flush GL→D3D 一致）。 */
static void winMemimgLock(GlWinMemimg* m) {
    if (m && m->interop && !m->locked) {
        env.pDXLock(env.interopDev, 1, &m->interop);
        m->locked = true;
    }
}
static void winMemimgUnlock(GlWinMemimg* m) {
    if (m && m->interop && m->locked) {
        glFlush();
        env.pDXUnlock(env.interopDev, 1, &m->interop);
        m->locked = false;
    }
}

/* MEMORY surface（win）：headless 上下文 + 每槽 memimg tex+FBO（+ 共享深度 rbo） */
static bool glMemorySurfaceCreate(gpu_surface_t* surf, GlSurface* s) {
    if (!winHeadlessCurrent()) return false;
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
        GlWinMemimg* m = img ? (GlWinMemimg*)img->backend : NULL;
        if (!m || !m->tex) return false;
        s->ringTex[i] = m->tex;   /* 借用 memimg 纹理（surface_destroy 不删） */
        glGenFramebuffers(1, &s->ringFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, s->ringFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m->tex, 0);
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
#endif /* P_WIN */

static bool glSurfaceCreate(gpu_surface_t* surf) {
    GlSurface* s = (GlSurface*)calloc(1, sizeof(GlSurface));
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if SC_GPU_GL_MEMIMG
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
#if P_WIN
    scgl_win_load();   /* 桌面 GL：上下文已 current，装载 GL 1.2+ 函数指针 */
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
#if SC_GPU_GL_MEMIMG
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
#if P_LINUX
        gl_egl_make_current();
#else
        if (env.headlessCtx) gl_ctx_make_current(env.headlessCtx);
#endif
        for (int i = 0; i < surf->desc.image_count; i++) {
            if (s->ringFbo[i]) glDeleteFramebuffers(1, &s->ringFbo[i]);
#if !P_WIN
            if (s->ringTex[i]) glDeleteTextures(1, &s->ringTex[i]);   /* win：借用 memimg 纹理，不删 */
#endif
#if P_LINUX
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
#if SC_GPU_GL_EGL_HEADLESS
            gl_egl_make_current();
            glBindVertexArray(env.headlessVao);
#elif P_DARWIN
            macHeadlessCurrent();
#elif P_WIN
            winHeadlessCurrent();
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
#if SC_GPU_GL_MEMIMG
        if (surf->ring_cur < 0) return false;
        f->gl_fbo = s->ringFbo[surf->ring_cur];
#if P_WIN
        {   /* interop 环槽：渲染前锁给 GL（可能被上帧 export 解锁） */
            gpu_memimg_t* mi = gpu_lookup_memimg(surf->ring_imgs[surf->ring_cur]);
            if (mi) winMemimgLock((GlWinMemimg*)mi->backend);
        }
#endif
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
#if SC_GPU_GL_EGL_HEADLESS
            /* 栓栏随命令流提交，存入本帧槽位（dequeue 时交付） */
            if (surf->ring_cur >= 0) {
                if (s->ringFence[surf->ring_cur] >= 0)
                    close(s->ringFence[surf->ring_cur]);
                s->ringFence[surf->ring_cur] = gl_egl_fence_fd();
                if (s->ringFence[surf->ring_cur] < 0)
                    glFinish();   /* 无 fence 扩展 → CPU 同步退化 */
            }
#elif P_DARWIN
            glFinish();   /* NSGL 无导出 fence：CPU 同步，dequeue 即可消费 */
#elif P_WIN
            glFinish();   /* WGL 无导出 fence：CPU 同步，dequeue 即可消费 */
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

/* ---- memimg（linux GBM/dma-buf / Android AHB → EGLImage） ---------------------- */

#if SC_GPU_GL_EGL_HEADLESS

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

#endif /* SC_GPU_GL_EGL_HEADLESS */

/* ---- memimg（mac：IOSurface） ------------------------------ */

#if P_DARWIN

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

#endif /* P_DARWIN */

/* ---- memimg（win：GL_TEXTURE_2D 分配 + glReadPixels 回读） --- */

#if P_WIN

static bool glMemimgAlloc(gpu_memimg_t* img) {
    const sc_gpu_memimg_desc* d = &img->desc;
    if (d->format != SC_GPU_PIXELFORMAT_BGRA8 &&
        d->format != SC_GPU_PIXELFORMAT_DEFAULT) {
        gpu_log("gl: win memimg 暂仅支持 BGRA8");
        return false;
    }
    if (!winHeadlessCurrent()) return false;
    GlWinMemimg* m = (GlWinMemimg*)calloc(1, sizeof(GlWinMemimg));
    if (!m) return false;
    m->w = d->width; m->h = d->height;

    /* 零拷贝路径：D3D11 共享纹理 ↔ GL 纹理（WGL_NV_DX_interop2）。
     * GL 纹理存储来自 D3D（不调 glTexImage2D）；register 后锁给 GL 供首次渲染。 */
    if (winInteropEnsure()) {
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof(td));
        td.Width = (UINT)m->w; td.Height = (UINT)m->h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        if (SUCCEEDED(ID3D11Device_CreateTexture2D(env.d3dDev, &td, NULL, &m->d3dTex)) && m->d3dTex) {
            glGenTextures(1, &m->tex);
            m->interop = env.pDXReg(env.interopDev, m->d3dTex, m->tex,
                                    GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV);
            if (m->interop) {
                IDXGIResource1* res = NULL;
                if (SUCCEEDED(ID3D11Texture2D_QueryInterface(m->d3dTex, &IID_IDXGIResource1,
                                                             (void**)&res)) && res) {
                    IDXGIResource1_CreateSharedHandle(res, NULL,
                        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m->shared);
                    IDXGIResource1_Release(res);
                }
                winMemimgLock(m);   /* 锁给 GL：首次渲染即可用 */
                img->backend = m;
                return true;
            }
            gpu_log("gl: wglDXRegisterObjectNV 失败，退化普通纹理");
            if (m->tex) { glDeleteTextures(1, &m->tex); m->tex = 0; }
            ID3D11Texture2D_Release(m->d3dTex); m->d3dTex = NULL;
        }
    }

    /* 退化：普通 GL 纹理（仅 CPU 回读，native=NULL） */
    glGenTextures(1, &m->tex);
    glBindTexture(GL_TEXTURE_2D, m->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m->w, m->h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    img->backend = m;
    return true;
}

static bool glMemimgImport(gpu_memimg_t* img, const sc_gpu_memory_frame* src) {
    if (!src->native) {
        gpu_log("gl: win memimg import 无原生共享句柄");
        return false;
    }
    if (!winHeadlessCurrent()) return false;
    if (!winInteropEnsure()) {
        gpu_log("gl: win memimg import 需 WGL_NV_DX_interop2（不可用）");
        return false;
    }

    /* 外部共享 NT 句柄 → D3D11 纹理（OpenSharedResource1 需 ID3D11Device1）。
     * 借用语义：不 CloseHandle（OpenSharedResource1 对底层共享资源独立持引用，
     * 原句柄可先释放）；d3dTex 归本 memimg 所有，free 时 Release。 */
    ID3D11Device1* dev1 = NULL;
    if (FAILED(ID3D11Device_QueryInterface(env.d3dDev, &IID_ID3D11Device1, (void**)&dev1)) || !dev1) {
        gpu_log("gl: 取 ID3D11Device1 失败（导入需 D3D11.1）");
        return false;
    }
    ID3D11Texture2D* tex = NULL;
    HRESULT hr = ID3D11Device1_OpenSharedResource1(dev1, (HANDLE)src->native,
                                                   &IID_ID3D11Texture2D, (void**)&tex);
    ID3D11Device1_Release(dev1);
    if (FAILED(hr) || !tex) {
        gpu_log("gl: OpenSharedResource1 失败 (hr=0x%08lx)", (unsigned long)hr);
        return false;
    }
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D_GetDesc(tex, &td);

    GlWinMemimg* m = (GlWinMemimg*)calloc(1, sizeof(GlWinMemimg));
    if (!m) { ID3D11Texture2D_Release(tex); return false; }
    m->w = (int)td.Width; m->h = (int)td.Height;
    m->d3dTex = tex;      /* 拥有：free 时 Release（保活底层共享资源） */
    m->shared = NULL;     /* 借用外部句柄，free 不 CloseHandle */

    /* D3D 纹理 ↔ GL 纹理互操作注册 + 锁给 GL（map 的 glReadPixels 可读其内容） */
    glGenTextures(1, &m->tex);
    m->interop = env.pDXReg(env.interopDev, m->d3dTex, m->tex,
                            GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV);
    if (!m->interop) {
        gpu_log("gl: 导入纹理 wglDXRegisterObjectNV 失败");
        glDeleteTextures(1, &m->tex);
        ID3D11Texture2D_Release(m->d3dTex);
        free(m);
        return false;
    }
    winMemimgLock(m);
    img->desc.width = m->w;
    img->desc.height = m->h;
    img->backend = m;
    return true;
}

static bool glMemimgExport(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence) {
    GlWinMemimg* m = (GlWinMemimg*)img->backend;
    if (!m) return false;
    if (with_fence) glFinish();   /* 无导出 fence → CPU 同步 */
    winMemimgUnlock(m);           /* flush GL→D3D 并解锁：共享句柄内容一致供消费者 */
    out->planes = 1;
    out->fd[0] = -1;
    out->stride[0] = (uint32_t)(m->w * 4);
    out->offset[0] = 0;
    out->fourcc = img->desc.fourcc;
    out->width = m->w;
    out->height = m->h;
    out->sync_fd = -1;
    out->native = m->shared;   /* 共享 NT 句柄（可导入 D3D / 送编码器）；无互操作则 NULL */
    if (m->shared)
        gpu_log("gl: memimg 导出共享 NT 句柄 %p (%dx%d 零拷贝)", m->shared, m->w, m->h);
    return true;
}

static void* glMemimgNative(gpu_memimg_t* img) {
    GlWinMemimg* m = (GlWinMemimg*)img->backend;
    return m ? (void*)(uintptr_t)m->tex : NULL;   /* gfx Mode B 直接绑定此 GL 纹理 */
}

static void* glMemimgMap(gpu_memimg_t* img, int plane, uint32_t* out_stride) {
    (void)plane;
    GlWinMemimg* m = (GlWinMemimg*)img->backend;
    if (!m) return NULL;
    if (!m->cpu) m->cpu = malloc((size_t)m->w * (size_t)m->h * 4);
    if (!m->cpu) return NULL;
    winMemimgLock(m);   /* glReadPixels 需 GL 拥有（可能被 export 解锁过） */
    if (!m->roFbo) glGenFramebuffers(1, &m->roFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m->roFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m->tex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m->w, m->h, GL_BGRA, GL_UNSIGNED_BYTE, m->cpu);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (out_stride) *out_stride = (uint32_t)(m->w * 4);
    return m->cpu;
}

static void glMemimgUnmap(gpu_memimg_t* img, int plane) {
    (void)img; (void)plane;   /* CPU 缓冲随 memimg 生命期，unmap 空操作 */
}

static void glMemimgFree(gpu_memimg_t* img) {
    GlWinMemimg* m = (GlWinMemimg*)img->backend;
    if (!m) return;
    if (m->interop) {
        if (m->locked) env.pDXUnlock(env.interopDev, 1, &m->interop);
        env.pDXUnreg(env.interopDev, m->interop);
    }
    if (m->shared) CloseHandle(m->shared);
    if (m->d3dTex) ID3D11Texture2D_Release(m->d3dTex);
    if (m->roFbo) glDeleteFramebuffers(1, &m->roFbo);
    if (m->tex) glDeleteTextures(1, &m->tex);
    free(m->cpu);
    free(m);
    img->backend = NULL;
}

static bool glSurfaceDequeue(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out) {
    gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[slot]);
    if (!img) return false;
    return glMemimgExport(img, out, false);   /* frame_end 已 glFinish，sync_fd=-1 */
}

#endif /* P_WIN */

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
#if SC_GPU_GL_MEMIMG
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
