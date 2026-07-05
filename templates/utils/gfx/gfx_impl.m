/* gfx_impl.m — GL Context: macOS NSOpenGL / Win WGL / Linux GLX. void* sigs. */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#include <dlfcn.h>
struct gfx_s { NSOpenGLContext* ctx; NSOpenGLPixelFormat* pf; NSView* view; };

void* sc_gfx_init(void* nwh, int w, int h, int ma, int mi) {
    (void)w;(void)h;
    gfx_t* c=(gfx_t*)calloc(1,sizeof(gfx_t)); if(!c)return NULL;
    c->view=(__bridge NSView*)nwh; if(!c->view){free(c);return NULL;}
    NSOpenGLPixelFormatAttribute a[16]; int i=0;
    a[i++]=NSOpenGLPFAAccelerated; a[i++]=NSOpenGLPFADoubleBuffer;
    a[i++]=NSOpenGLPFAClosestPolicy; a[i++]=NSOpenGLPFAOpenGLProfile;
    int gv=ma*10+mi;
    switch(gv){case 32:a[i++]=NSOpenGLProfileVersion3_2Core;break;
    case 41:a[i++]=NSOpenGLProfileVersion4_1Core;break;
    default:a[i++]=gv>=41?NSOpenGLProfileVersion4_1Core:NSOpenGLProfileVersionLegacy;break;}
    a[i++]=NSOpenGLPFAColorSize;a[i++]=24; a[i++]=NSOpenGLPFAAlphaSize;a[i++]=8;
    a[i++]=NSOpenGLPFADepthSize;a[i++]=24; a[i++]=0;
    c->pf=[[NSOpenGLPixelFormat alloc] initWithAttributes:a];
    if(!c->pf){free(c);return NULL;}
    c->ctx=[[NSOpenGLContext alloc] initWithFormat:c->pf shareContext:nil];
    if(!c->ctx){[c->pf release];free(c);return NULL;}
    [c->ctx setView:c->view]; [c->ctx makeCurrentContext];
    GLint si=1; [c->ctx setValues:&si forParameter:NSOpenGLContextParameterSwapInterval];
    return c;
}
void sc_gfx_swap(void* x){gfx_t* c=(gfx_t*)x; if(c&&c->ctx)[c->ctx flushBuffer];}
void sc_gfx_make_current(void* x){gfx_t* c=(gfx_t*)x;
    if(c&&c->ctx)[c->ctx makeCurrentContext]; else [NSOpenGLContext clearCurrentContext];}
void sc_gfx_destroy(void* x){gfx_t* c=(gfx_t*)x; if(!c)return;
    if(c->ctx){[NSOpenGLContext clearCurrentContext];[c->ctx release];} if(c->pf)[c->pf release]; free(c);}
void* sc_gfx_get_proc(const char* n){static void* fw=NULL;
    if(!fw)fw=dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",RTLD_LAZY);
    return fw?dlsym(fw,n):NULL;}
#endif
#endif

#if defined(_WIN32)
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
struct gfx_s{ HGLRC rc; HDC dc; HWND hwnd; HMODULE gl;
    BOOL(WINAPI*wmc)(HDC,HGLRC);BOOL(WINAPI*wdc)(HGLRC);HGLRC(WINAPI*wcc)(HDC);
    void*(WINAPI*wgpa)(const char*);HGLRC(WINAPI*wccaa)(HDC,HGLRC,const int*);};

void* sc_gfx_init(void* nwh, int w, int h, int ma, int mi){
    (void)w;(void)h;
    gfx_t* c=(gfx_t*)calloc(1,sizeof(gfx_t)); if(!c)return NULL;
    c->hwnd=(HWND)nwh; if(!c->hwnd){free(c);return NULL;}
    c->gl=LoadLibraryA("opengl32.dll"); if(!c->gl){free(c);return NULL;}
#define L(f,n) c->f=(typeof(c->f))GetProcAddress(c->gl,n)
    L(wgpa,"wglGetProcAddress");L(wmc,"wglMakeCurrent");L(wdc,"wglDeleteContext");L(wcc,"wglCreateContext");
#undef L
    if(!c->wgpa||!c->wmc||!c->wdc||!c->wcc){FreeLibrary(c->gl);free(c);return NULL;}
    c->dc=GetDC(c->hwnd); if(!c->dc){FreeLibrary(c->gl);free(c);return NULL;}
    PIXELFORMATDESCRIPTOR p={0}; p.nSize=sizeof(p);p.nVersion=1;
    p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=24;p.cDepthBits=24;
    int pf=ChoosePixelFormat(c->dc,&p);
    if(!pf||!SetPixelFormat(c->dc,pf,&p)){ReleaseDC(c->hwnd,c->dc);FreeLibrary(c->gl);free(c);return NULL;}
    HGLRC tmp=c->wcc(c->dc); if(!tmp){ReleaseDC(c->hwnd,c->dc);FreeLibrary(c->gl);free(c);return NULL;}
    c->wmc(c->dc,tmp);
    c->wccaa=(HGLRC(WINAPI*)(HDC,HGLRC,const int*))c->wgpa("wglCreateContextAttribsARB");
    int arb=0,prf=0;{typedef const char*(WINAPI*P)(HDC);P fn=(P)c->wgpa("wglGetExtensionsStringARB");
    const char* e=fn?fn(c->dc):NULL; if(e){if(strstr(e,"WGL_ARB_create_context"))arb=1;
    if(strstr(e,"WGL_ARB_create_context_profile"))prf=1;}}
    c->wmc(NULL,NULL);c->wdc(tmp);
    if(arb&&prf&&c->wccaa){int at[]={0x2091,ma,0x2092,mi,0x2094,1,0x9126,1,0,0};
        c->rc=c->wccaa(c->dc,NULL,at);}
    if(!c->rc)c->rc=c->wcc(c->dc);
    if(!c->rc){ReleaseDC(c->hwnd,c->dc);FreeLibrary(c->gl);free(c);return NULL;}
    c->wmc(c->dc,c->rc);return c;
}
void sc_gfx_swap(void* x){gfx_t* c=(gfx_t*)x; if(c&&c->dc)SwapBuffers(c->dc);}
void sc_gfx_make_current(void* x){gfx_t* c=(gfx_t*)x;
    if(c&&c->dc&&c->rc)c->wmc(c->dc,c->rc);else if(c)c->wmc(NULL,NULL);}
void sc_gfx_destroy(void* x){gfx_t* c=(gfx_t*)x; if(!c)return;
    if(c->rc){c->wmc(NULL,NULL);c->wdc(c->rc);}if(c->dc)ReleaseDC(c->hwnd,c->dc);
    if(c->gl)FreeLibrary(c->gl);free(c);}
void* sc_gfx_get_proc(const char* n){static HMODULE gl=NULL;
    if(!gl)gl=LoadLibraryA("opengl32.dll");if(!gl)return NULL;
    void* p=NULL;void*(WINAPI*fn)(const char*)=(void*(WINAPI*)(const char*))GetProcAddress(gl,"wglGetProcAddress");
    if(fn)p=fn(n);if(!p)p=(void*)GetProcAddress(gl,n);return p;}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <stdint.h>
#include <dlfcn.h>
struct gfx_s{Display*dpy;GLXContext ctx;GLXWindow glxw;Window xwin;};

void* sc_gfx_init(void* nwh, int w, int h, int ma, int mi){
    (void)w;(void)h;
    gfx_t* c=(gfx_t*)calloc(1,sizeof(gfx_t)); if(!c)return NULL;
    c->xwin=(Window)(uintptr_t)nwh; if(!c->xwin){free(c);return NULL;}
    c->dpy=XOpenDisplay(NULL); if(!c->dpy){free(c);return NULL;}
    int at[]={GLX_RGBA,GLX_DOUBLEBUFFER,GLX_DEPTH_SIZE,24,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,None};
    XVisualInfo* vi=glXChooseVisual(c->dpy,DefaultScreen(c->dpy),at);
    if(!vi){XCloseDisplay(c->dpy);free(c);return NULL;}
    typedef GLXContext(*PFN)(Display*,GLXFBConfig,GLXContext,Bool,const int*);
    PFN cca=NULL; void* lg=dlopen("libGL.so.1",RTLD_LAZY);
    if(lg)cca=(PFN)dlsym(lg,"glXCreateContextAttribsARB");
    if(cca&&ma>=3){int fa[]={GLX_RENDER_TYPE,GLX_RGBA_BIT,GLX_DOUBLEBUFFER,True,GLX_DEPTH_SIZE,24,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,None};
        int n=0; GLXFBConfig* fb=glXChooseFBConfig(c->dpy,DefaultScreen(c->dpy),fa,&n);
        if(fb&&n>0){int ca[]={GLX_CONTEXT_MAJOR_VERSION_ARB,ma,GLX_CONTEXT_MINOR_VERSION_ARB,mi,GLX_CONTEXT_PROFILE_MASK_ARB,GLX_CONTEXT_CORE_PROFILE_BIT_ARB,None};
            c->ctx=cca(c->dpy,fb[0],NULL,True,ca);XFree(fb);}}
    if(!c->ctx)c->ctx=glXCreateContext(c->dpy,vi,NULL,True);XFree(vi);
    if(!c->ctx){if(lg)dlclose(lg);XCloseDisplay(c->dpy);free(c);return NULL;}
    c->glxw=c->xwin;glXMakeCurrent(c->dpy,c->glxw,c->ctx); if(lg)dlclose(lg);return c;
}
void sc_gfx_swap(void* x){gfx_t* c=(gfx_t*)x; if(c&&c->dpy)glXSwapBuffers(c->dpy,c->glxw);}
void sc_gfx_make_current(void* x){gfx_t* c=(gfx_t*)x;
    if(c&&c->dpy)glXMakeCurrent(c->dpy,c->glxw,c->ctx);else glXMakeCurrent(NULL,None,NULL);}
void sc_gfx_destroy(void* x){gfx_t* c=(gfx_t*)x; if(!c)return;
    if(c->ctx){glXMakeCurrent(c->dpy,None,NULL);glXDestroyContext(c->dpy,c->ctx);}
    if(c->dpy)XCloseDisplay(c->dpy);free(c);}
void* sc_gfx_get_proc(const char* n){return(void*)glXGetProcAddress((const GLubyte*)n);}
#endif

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__)
#error "gfx_impl.m: unsupported platform."
#endif
