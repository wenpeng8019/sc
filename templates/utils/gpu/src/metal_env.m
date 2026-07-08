/* ============================================================
 * metal_env.m —— Metal 运行环境后端（macOS）
 * ============================================================
 * env 层职责（渲染命令翻译在 gfx 模块的 metal_gfx.m）：
 *   · MTLDevice 创建（经 sc_gpu_device() 交付 gfx）
 *   · surface = CAMetalLayer 交换链（挂 wsi NSView），含交换链
 *     深度纹理 / MSAA 颜色纹理（随 resize 重建）
 *   · frame_acquire：懒取 nextDrawable，交付本帧渲染目标句柄；
 *     resize 竞态在此校验（附属纹理与 drawable 尺寸不一致即重建）
 *   · frame_end：释放本帧 drawable 引用（呈现由 gfx 把 drawable
 *     挂到命令缓冲——命令缓冲默认持有引用，此处释放安全）
 * ============================================================ */

#ifdef SC_GPU_METAL

#include "internal.h"
#include <string.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

#define MTL_MAX_ACQUIRED 16   /* 单帧最多触达的 surface 数 */

/* ---- 后端私有体 -------------------------------------------- */

typedef struct MtlSurface {
    CAMetalLayer*       layer;
    id<CAMetalDrawable> drawable;   /* 本帧已取的 drawable（frame_end 后清） */
    id<MTLTexture>      depthTex;
    id<MTLTexture>      msaaTex;    /* sample_count>1 时的 MSAA 颜色目标 */
} MtlSurface;

static struct {
    id<MTLDevice> device;
    _sc_gpu_surface_t* acquired[MTL_MAX_ACQUIRED];   /* 本帧已取 drawable 的 surface */
    int acquiredCount;
} env;

static MTLPixelFormat toMtlFormat(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_RGBA8:         return MTLPixelFormatRGBA8Unorm;
        case SC_GPU_PIXELFORMAT_SRGB8A8:       return MTLPixelFormatRGBA8Unorm_sRGB;
        case SC_GPU_PIXELFORMAT_RGB10A2:       return MTLPixelFormatRGB10A2Unorm;
        case SC_GPU_PIXELFORMAT_RGBA16F:       return MTLPixelFormatRGBA16Float;
        case SC_GPU_PIXELFORMAT_DEPTH:         return MTLPixelFormatDepth32Float;
        case SC_GPU_PIXELFORMAT_DEPTH_STENCIL: return MTLPixelFormatDepth32Float_Stencil8;
        case SC_GPU_PIXELFORMAT_DEFAULT:
        case SC_GPU_PIXELFORMAT_BGRA8:
        default:                               return MTLPixelFormatBGRA8Unorm;
    }
}

/* 交换链附属纹理（深度 / MSAA），随 resize 重建。
 * 直接重赋值即可：旧纹理若仍被在飞命令缓冲引用，命令缓冲持有
 * 引用（Metal 默认 retained references），ARC 释放安全。 */
static void surfaceCreateTargets(MtlSurface* s, const sc_gpu_surface_desc* d) {
    if (d->depth_format != SC_GPU_PIXELFORMAT_NONE) {
        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.textureType = d->sample_count > 1 ? MTLTextureType2DMultisample
                                             : MTLTextureType2D;
        td.pixelFormat = toMtlFormat(d->depth_format);
        td.width  = (NSUInteger)d->width;
        td.height = (NSUInteger)d->height;
        td.sampleCount  = (NSUInteger)(d->sample_count > 1 ? d->sample_count : 1);
        td.usage        = MTLTextureUsageRenderTarget;
        td.storageMode  = MTLStorageModePrivate;
        s->depthTex = [env.device newTextureWithDescriptor:td];
    } else {
        s->depthTex = nil;
    }
    if (d->sample_count > 1) {
        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.textureType = MTLTextureType2DMultisample;
        td.pixelFormat = toMtlFormat(d->color_format);
        td.width  = (NSUInteger)d->width;
        td.height = (NSUInteger)d->height;
        td.sampleCount  = (NSUInteger)d->sample_count;
        td.usage        = MTLTextureUsageRenderTarget;
        td.storageMode  = MTLStorageModePrivate;
        s->msaaTex = [env.device newTextureWithDescriptor:td];
    } else {
        s->msaaTex = nil;
    }
}

/* ---- init/shutdown ----------------------------------------- */

static bool mtlInit(const sc_gpu_desc* desc) {
    (void)desc;
    memset((void*)&env, 0, sizeof(env));
    env.device = MTLCreateSystemDefaultDevice();
    if (!env.device) { _sc_gpu_log("metal: 无可用设备"); return false; }
    return true;
}

static void mtlShutdown(void) {
    env.device = nil;
    memset((void*)&env, 0, sizeof(env));
}

static void* mtlDevice(void) {
    return (__bridge void*)env.device;
}

/* ---- surface ----------------------------------------------- */

static bool mtlSurfaceCreate(_sc_gpu_surface_t* surf) {
    MtlSurface* s = (MtlSurface*)calloc(1, sizeof(MtlSurface));
    if (!s) return false;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = env.device;
    layer.pixelFormat = toMtlFormat(surf->desc.color_format);
    layer.framebufferOnly = YES;
    layer.drawableSize = CGSizeMake(surf->desc.width, surf->desc.height);
    if (@available(macOS 10.13, *))
        layer.displaySyncEnabled = surf->desc.swap_interval != 0;
    s->layer = layer;

    NSView* view = (__bridge NSView*)surf->desc.native_window;
    void (^attach)(void) = ^{
        view.wantsLayer = YES;
        view.layer = layer;
    };
    if ([NSThread isMainThread]) attach();
    else dispatch_sync(dispatch_get_main_queue(), attach);

    surfaceCreateTargets(s, &surf->desc);
    surf->backend = s;
    return true;
}

static void mtlSurfaceDestroy(_sc_gpu_surface_t* surf) {
    MtlSurface* s = (MtlSurface*)surf->backend;
    if (!s) return;
    for (int i = 0; i < env.acquiredCount; i++) {
        if (env.acquired[i] == surf) {
            env.acquired[i] = env.acquired[--env.acquiredCount];
            break;
        }
    }
    s->layer = nil; s->drawable = nil; s->depthTex = nil; s->msaaTex = nil;
    free(s);
    surf->backend = NULL;
}

static void mtlSurfaceActivate(_sc_gpu_surface_t* surf) {
    (void)surf;   /* Metal 无"当前上下文"概念；当前 surface 由公共层持有 */
}

static void mtlSurfaceResize(_sc_gpu_surface_t* surf, int w, int h) {
    MtlSurface* s = (MtlSurface*)surf->backend;
    if (!s) return;
    (void)w; (void)h;   /* surf->desc 已由公共层更新 */
    s->layer.drawableSize = CGSizeMake(surf->desc.width, surf->desc.height);
    surfaceCreateTargets(s, &surf->desc);
}

/* ---- 帧交付 ------------------------------------------------ */

static bool mtlFrameAcquire(_sc_gpu_surface_t* surf, sc_gpu_frame* f) {
    MtlSurface* s = (MtlSurface*)surf->backend;
    if (!s) return false;

    if (!s->drawable) s->drawable = [s->layer nextDrawable];
    if (!s->drawable) { _sc_gpu_log("metal: nextDrawable 失败"); return false; }

    id<MTLTexture> target = s->drawable.texture;
    int w = (int)target.width;
    int h = (int)target.height;

    /* resize 竞态：附属纹理与 drawable 尺寸不一致 → 立刻重建 */
    if ((s->depthTex && ((int)s->depthTex.width != w || (int)s->depthTex.height != h)) ||
        (s->msaaTex && ((int)s->msaaTex.width != w || (int)s->msaaTex.height != h))) {
        sc_gpu_surface_desc d = surf->desc;
        d.width = w;
        d.height = h;
        surfaceCreateTargets(s, &d);
    }

    f->color = (__bridge void*)target;
    f->msaa_color = (__bridge void*)s->msaaTex;
    f->depth = (__bridge void*)s->depthTex;
    f->drawable = (__bridge void*)s->drawable;
    f->gl_fbo = 0;
    f->width = w;
    f->height = h;
    f->sample_count = surf->desc.sample_count;
    f->color_format = surf->desc.color_format;
    f->depth_format = s->depthTex ? surf->desc.depth_format : SC_GPU_PIXELFORMAT_NONE;

    /* 记入本帧列表（去重），frame_end 统一清 drawable 引用 */
    bool listed = false;
    for (int i = 0; i < env.acquiredCount; i++)
        if (env.acquired[i] == surf) { listed = true; break; }
    if (!listed && env.acquiredCount < MTL_MAX_ACQUIRED)
        env.acquired[env.acquiredCount++] = surf;
    return true;
}

static void mtlFrameEnd(void) {
    for (int i = 0; i < env.acquiredCount; i++) {
        MtlSurface* s = (MtlSurface*)env.acquired[i]->backend;
        if (s) s->drawable = nil;   /* 呈现已挂命令缓冲（其持有引用） */
    }
    env.acquiredCount = 0;
}

/* ---- vtable ------------------------------------------------ */

static const _sc_gpu_env_api mtlApi = {
    .name = "metal",
    .kind = SC_GPU_BACKEND_METAL,
    .init = mtlInit,
    .shutdown = mtlShutdown,
    .device = mtlDevice,
    .surface_create = mtlSurfaceCreate,
    .surface_destroy = mtlSurfaceDestroy,
    .surface_activate = mtlSurfaceActivate,
    .surface_resize = mtlSurfaceResize,
    .frame_acquire = mtlFrameAcquire,
    .frame_end = mtlFrameEnd,
};

const _sc_gpu_env_api* _sc_gpu_env_metal(void) { return &mtlApi; }

#endif /* SC_GPU_METAL */
