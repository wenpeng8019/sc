/* ============================================================
 * gl_ctx.c —— OpenGL 上下文创建（平台层）
 * ============================================================
 * macOS = NSOpenGL（ARC，须以 -x objective-c 编译）
 * Windows = WGL（含 ARB create_context 尝试）
 * Linux = GLX（X11；Wayland/EGL 待补）
 *
 * 多 surface：每个 surface 一个 GL 上下文，同组共享对象
 * （shareContext = 首个上下文）。VAO/FBO 不跨上下文共享，
 * 由 gl_dev.c 按上下文各自持有。
 * ============================================================ */

#ifdef SC_GPU_GL

#include "gl_ctx.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * macOS —— NSOpenGL
 * ============================================================ */
#if defined(__APPLE__)

#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#include <dlfcn.h>

/* NSOpenGL 自 10.14 起整体废弃（苹果推 Metal）——GL 后端本就是
 * 兼容路径，静默弃用告警 */
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

struct gl_ctx {
    NSOpenGLContext*     ctx;
    NSOpenGLPixelFormat* pf;
    NSView*              view;
};

gl_ctx* gl_ctx_create(void* native_window, void* native_display,
                              int major, int minor, int swap_interval) {
    (void)native_display;
    gl_ctx* c = (gl_ctx*)calloc(1, sizeof(gl_ctx));
    if (!c) return NULL;
    c->view = (__bridge NSView*)native_window;   /* NULL = 无屏上下文（离屏 FBO 渲染） */

    NSOpenGLPixelFormatAttribute a[16];
    int i = 0;
    a[i++] = NSOpenGLPFAAccelerated;
    if (c->view) a[i++] = NSOpenGLPFADoubleBuffer;   /* 无屏不需要交换链 */
    a[i++] = NSOpenGLPFAClosestPolicy;
    a[i++] = NSOpenGLPFAOpenGLProfile;
    int gv = major * 10 + minor;
    if (gv >= 41)      a[i++] = NSOpenGLProfileVersion4_1Core;
    else if (gv >= 32) a[i++] = NSOpenGLProfileVersion3_2Core;
    else               a[i++] = NSOpenGLProfileVersionLegacy;
    a[i++] = NSOpenGLPFAColorSize; a[i++] = 24;
    a[i++] = NSOpenGLPFAAlphaSize; a[i++] = 8;
    a[i++] = NSOpenGLPFADepthSize; a[i++] = 24;
    a[i++] = NSOpenGLPFAStencilSize; a[i++] = 8;
    a[i++] = 0;

    c->pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:a];
    if (!c->pf) { free(c); return NULL; }
    c->ctx = [[NSOpenGLContext alloc] initWithFormat:c->pf shareContext:nil];
    if (!c->ctx) { c->pf = nil; free(c); return NULL; }

    if (c->view) {
        NSOpenGLContext* ctx = c->ctx;
        NSView* view = c->view;
        void (^attach)(void) = ^{
            view.wantsBestResolutionOpenGLSurface = YES;   /* retina：像素级帧缓冲 */
            [ctx setView:view];
        };
        if ([NSThread isMainThread]) attach();
        else dispatch_sync(dispatch_get_main_queue(), attach);
    }

    [c->ctx makeCurrentContext];
    GLint si = swap_interval;
    [c->ctx setValues:&si forParameter:NSOpenGLContextParameterSwapInterval];
    return c;
}

void gl_ctx_destroy(gl_ctx* c) {
    if (!c) return;
    if (c->ctx == [NSOpenGLContext currentContext])
        [NSOpenGLContext clearCurrentContext];
    c->ctx = nil;
    c->pf = nil;
    c->view = nil;
    free(c);
}

void gl_ctx_make_current(gl_ctx* c) {
    if (c && c->ctx) [c->ctx makeCurrentContext];
    else [NSOpenGLContext clearCurrentContext];
}

void gl_ctx_swap(gl_ctx* c) {
    if (c && c->ctx && c->view) [c->ctx flushBuffer];
}

void gl_ctx_resize(gl_ctx* c) {
    if (!c || !c->ctx) return;
    NSOpenGLContext* ctx = c->ctx;
    void (^upd)(void) = ^{ [ctx update]; };
    if ([NSThread isMainThread]) upd();
    else dispatch_sync(dispatch_get_main_queue(), upd);
}

void* gl_get_proc(const char* name) {
    static void* fw = NULL;
    if (!fw)
        fw = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
    return fw ? dlsym(fw, name) : NULL;
}

/* ============================================================
 * Windows —— WGL
 * ============================================================ */
#elif defined(_WIN32)

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

struct gl_ctx {
    HGLRC rc;
    HDC   dc;
    HWND  hwnd;
    HMODULE gl;
    BOOL (WINAPI* wmc)(HDC, HGLRC);
    BOOL (WINAPI* wdc)(HGLRC);
    HGLRC (WINAPI* wcc)(HDC);
    void* (WINAPI* wgpa)(const char*);
    HGLRC (WINAPI* wccaa)(HDC, HGLRC, const int*);
    BOOL (WINAPI* wsi_)(int);
};

gl_ctx* gl_ctx_create(void* native_window, void* native_display,
                              int major, int minor, int swap_interval) {
    (void)native_display;
    gl_ctx* c = (gl_ctx*)calloc(1, sizeof(gl_ctx));
    if (!c) return NULL;
    c->hwnd = (HWND)native_window;
    if (!c->hwnd) { free(c); return NULL; }
    c->gl = LoadLibraryA("opengl32.dll");
    if (!c->gl) { free(c); return NULL; }
#define L(f, n) c->f = (void*)GetProcAddress(c->gl, n)
    L(wgpa, "wglGetProcAddress"); L(wmc, "wglMakeCurrent");
    L(wdc, "wglDeleteContext");   L(wcc, "wglCreateContext");
#undef L
    if (!c->wgpa || !c->wmc || !c->wdc || !c->wcc) { FreeLibrary(c->gl); free(c); return NULL; }
    c->dc = GetDC(c->hwnd);
    if (!c->dc) { FreeLibrary(c->gl); free(c); return NULL; }

    PIXELFORMATDESCRIPTOR p; memset(&p, 0, sizeof(p));
    p.nSize = sizeof(p); p.nVersion = 1;
    p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    p.iPixelType = PFD_TYPE_RGBA; p.cColorBits = 24; p.cDepthBits = 24; p.cStencilBits = 8;
    int pf = ChoosePixelFormat(c->dc, &p);
    if (!pf || !SetPixelFormat(c->dc, pf, &p)) {
        ReleaseDC(c->hwnd, c->dc); FreeLibrary(c->gl); free(c); return NULL;
    }
    HGLRC tmp = c->wcc(c->dc);
    if (!tmp) { ReleaseDC(c->hwnd, c->dc); FreeLibrary(c->gl); free(c); return NULL; }
    c->wmc(c->dc, tmp);
    c->wccaa = (HGLRC(WINAPI*)(HDC, HGLRC, const int*))c->wgpa("wglCreateContextAttribsARB");
    c->wsi_ = (BOOL(WINAPI*)(int))c->wgpa("wglSwapIntervalEXT");
    c->wmc(NULL, NULL);
    c->wdc(tmp);
    if (c->wccaa) {
        /* 0x2091/0x2092=major/minor 0x2094=flags 0x9126=core profile */
        int at[] = { 0x2091, major, 0x2092, minor, 0x9126, 1, 0, 0 };
        c->rc = c->wccaa(c->dc, NULL, at);
    }
    if (!c->rc) c->rc = c->wcc(c->dc);
    if (!c->rc) { ReleaseDC(c->hwnd, c->dc); FreeLibrary(c->gl); free(c); return NULL; }
    c->wmc(c->dc, c->rc);
    if (c->wsi_) c->wsi_(swap_interval);
    return c;
}

void gl_ctx_destroy(gl_ctx* c) {
    if (!c) return;
    if (c->rc) { c->wmc(NULL, NULL); c->wdc(c->rc); }
    if (c->dc) ReleaseDC(c->hwnd, c->dc);
    if (c->gl) FreeLibrary(c->gl);
    free(c);
}

void gl_ctx_make_current(gl_ctx* c) {
    if (c && c->dc && c->rc) c->wmc(c->dc, c->rc);
    else if (c) c->wmc(NULL, NULL);
}

void gl_ctx_swap(gl_ctx* c) { if (c && c->dc) SwapBuffers(c->dc); }
void gl_ctx_resize(gl_ctx* c) { (void)c; }

void* gl_get_proc(const char* name) {
    static HMODULE gl = NULL;
    if (!gl) gl = LoadLibraryA("opengl32.dll");
    if (!gl) return NULL;
    void* p = NULL;
    void* (WINAPI* fn)(const char*) =
        (void* (WINAPI*)(const char*))GetProcAddress(gl, "wglGetProcAddress");
    if (fn) p = fn(name);
    if (!p) p = (void*)GetProcAddress(gl, name);
    return p;
}

/* ============================================================
 * Linux —— GLX（X11）
 * ============================================================ */
#elif defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <stdint.h>
#include <dlfcn.h>

struct gl_ctx {
    Display*   dpy;
    int        own_dpy;
    GLXContext ctx;
    GLXWindow  glxw;
    Window     xwin;
};

gl_ctx* gl_ctx_create(void* native_window, void* native_display,
                              int major, int minor, int swap_interval) {
    gl_ctx* c = (gl_ctx*)calloc(1, sizeof(gl_ctx));
    if (!c) return NULL;
    c->xwin = (Window)(uintptr_t)native_window;
    if (!c->xwin) { free(c); return NULL; }
    if (native_display) { c->dpy = (Display*)native_display; }
    else { c->dpy = XOpenDisplay(NULL); c->own_dpy = 1; }
    if (!c->dpy) { free(c); return NULL; }

    typedef GLXContext (*PFN)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    PFN cca = (PFN)glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB");
    if (cca) {
        int fa[] = { GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DOUBLEBUFFER, True,
                     GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8,
                     GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
                     GLX_ALPHA_SIZE, 8, None };
        int n = 0;
        GLXFBConfig* fb = glXChooseFBConfig(c->dpy, DefaultScreen(c->dpy), fa, &n);
        if (fb && n > 0) {
            int ca[] = { 0x2091 /*MAJOR*/, major, 0x2092 /*MINOR*/, minor,
                         0x9126 /*PROFILE_MASK*/, 1 /*CORE*/, None };
            c->ctx = cca(c->dpy, fb[0], NULL, True, ca);
            XFree(fb);
        }
    }
    if (!c->ctx) {
        int at[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24,
                     GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
                     GLX_ALPHA_SIZE, 8, None };
        XVisualInfo* vi = glXChooseVisual(c->dpy, DefaultScreen(c->dpy), at);
        if (vi) { c->ctx = glXCreateContext(c->dpy, vi, NULL, True); XFree(vi); }
    }
    if (!c->ctx) {
        if (c->own_dpy) XCloseDisplay(c->dpy);
        free(c);
        return NULL;
    }
    c->glxw = c->xwin;
    glXMakeCurrent(c->dpy, c->glxw, c->ctx);
    typedef void (*PFNSI)(Display*, GLXDrawable, int);
    PFNSI si = (PFNSI)glXGetProcAddress((const GLubyte*)"glXSwapIntervalEXT");
    if (si) si(c->dpy, c->glxw, swap_interval);
    return c;
}

void gl_ctx_destroy(gl_ctx* c) {
    if (!c) return;
    if (c->ctx) {
        glXMakeCurrent(c->dpy, None, NULL);
        glXDestroyContext(c->dpy, c->ctx);
    }
    if (c->own_dpy && c->dpy) XCloseDisplay(c->dpy);
    free(c);
}

void gl_ctx_make_current(gl_ctx* c) {
    if (c && c->dpy) glXMakeCurrent(c->dpy, c->glxw, c->ctx);
}

void gl_ctx_swap(gl_ctx* c) {
    if (c && c->dpy) glXSwapBuffers(c->dpy, c->glxw);
}

void gl_ctx_resize(gl_ctx* c) { (void)c; }

void* gl_get_proc(const char* name) {
    return (void*)glXGetProcAddress((const GLubyte*)name);
}

#else
#error "gl_ctx.c: unsupported platform"
#endif

#endif /* SC_GPU_GL */
