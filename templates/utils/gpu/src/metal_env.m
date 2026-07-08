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
#import <IOSurface/IOSurface.h>

#define MTL_MAX_ACQUIRED 16   /* 单帧最多触达的 surface 数 */

/* ---- 后端私有体 -------------------------------------------- */

typedef struct MtlSurface {
    /* WINDOW */
    CAMetalLayer*       layer;
    id<CAMetalDrawable> drawable;   /* 本帧已取的 drawable（frame_end 后清） */
    /* MEMORY：环槽纹理（来自 ring memimg 的 IOSurface） */
    id<MTLTexture>      ringTex[SC_GPU_MAX_MEMORY_IMAGES];
    /* 共用附属目标 */
    id<MTLTexture>      depthTex;
    id<MTLTexture>      msaaTex;    /* sample_count>1 时的 MSAA 颜色目标 */
} MtlSurface;

/* memimg：IOSurface + 包装纹理 */
typedef struct MtlMemimg {
    IOSurfaceRef   iosurf;
    id<MTLTexture> tex;
} MtlMemimg;

static struct {
    id<MTLDevice> device;
    gpu_surface_t* acquired[MTL_MAX_ACQUIRED];   /* 本帧已取 drawable 的 surface */
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
    if (!env.device) { gpu_log("metal: 无可用设备"); return false; }
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

static bool mtlSurfaceCreate(gpu_surface_t* surf) {
    MtlSurface* s = (MtlSurface*)calloc(1, sizeof(MtlSurface));
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        /* 内存交换链：环成员 = memimg 的 IOSurface 纹理 */
        for (int i = 0; i < surf->desc.image_count; i++) {
            gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[i]);
            MtlMemimg* m = img ? (MtlMemimg*)img->backend : NULL;
            if (!m || !m->tex) { free(s); return false; }
            s->ringTex[i] = m->tex;
        }
        surfaceCreateTargets(s, &surf->desc);
        surf->backend = s;
        return true;
    }

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

static void mtlSurfaceDestroy(gpu_surface_t* surf) {
    MtlSurface* s = (MtlSurface*)surf->backend;
    if (!s) return;
    for (int i = 0; i < env.acquiredCount; i++) {
        if (env.acquired[i] == surf) {
            env.acquired[i] = env.acquired[--env.acquiredCount];
            break;
        }
    }
    for (int i = 0; i < SC_GPU_MAX_MEMORY_IMAGES; i++) s->ringTex[i] = nil;
    s->layer = nil; s->drawable = nil; s->depthTex = nil; s->msaaTex = nil;
    free(s);
    surf->backend = NULL;
}

static void mtlSurfaceActivate(gpu_surface_t* surf) {
    (void)surf;   /* Metal 无"当前上下文"概念；当前 surface 由公共层持有 */
}

static void mtlSurfaceResize(gpu_surface_t* surf, int w, int h) {
    MtlSurface* s = (MtlSurface*)surf->backend;
    if (!s) return;
    (void)w; (void)h;   /* surf->desc 已由公共层更新 */
    s->layer.drawableSize = CGSizeMake(surf->desc.width, surf->desc.height);
    surfaceCreateTargets(s, &surf->desc);
}

/* ---- 帧交付 ------------------------------------------------ */

static bool mtlFrameAcquire(gpu_surface_t* surf, sc_gpu_frame* f) {
    MtlSurface* s = (MtlSurface*)surf->backend;
    if (!s) return false;

    /* MEMORY：目标 = 公共层调度的环槽纹理（无 drawable） */
    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        if (surf->ring_cur < 0) return false;
        id<MTLTexture> target = s->ringTex[surf->ring_cur];
        f->color = (__bridge void*)target;
        f->msaa_color = (__bridge void*)s->msaaTex;
        f->depth = (__bridge void*)s->depthTex;
        f->drawable = NULL;
        f->width = surf->desc.width;
        f->height = surf->desc.height;
        f->sample_count = surf->desc.sample_count;
        f->color_format = surf->desc.color_format;
        f->depth_format = s->depthTex ? surf->desc.depth_format : SC_GPU_PIXELFORMAT_NONE;
        return true;
    }

    if (!s->drawable) s->drawable = [s->layer nextDrawable];
    if (!s->drawable) { gpu_log("metal: nextDrawable 失败"); return false; }

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

/* ---- memimg（IOSurface） ------------------------------------ */

static bool mtlMemimgAlloc(gpu_memimg_t* img) {
    MtlMemimg* m = (MtlMemimg*)calloc(1, sizeof(MtlMemimg));
    if (!m) return false;
    const sc_gpu_memimg_desc* d = &img->desc;

    /* IOSurface：32 位 BGRA（'BGRA'）；YUV 导入路径待扩 */
    MTLPixelFormat mfmt = toMtlFormat(d->format);
    if (mfmt != MTLPixelFormatBGRA8Unorm && mfmt != MTLPixelFormatRGBA8Unorm) {
        gpu_log("metal: memimg 暂仅支持 BGRA8/RGBA8");
        free(m);
        return false;
    }
    size_t bpr = (size_t)d->width * 4;
    bpr = IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, bpr);
    NSDictionary* props = @{
        (id)kIOSurfaceWidth:           @(d->width),
        (id)kIOSurfaceHeight:          @(d->height),
        (id)kIOSurfaceBytesPerElement: @4,
        (id)kIOSurfaceBytesPerRow:     @(bpr),
        (id)kIOSurfacePixelFormat:     @((uint32_t)'BGRA'),
    };
    m->iosurf = IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (!m->iosurf) {
        gpu_log("metal: IOSurfaceCreate 失败");
        free(m);
        return false;
    }

    MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
    td.textureType = MTLTextureType2D;
    td.pixelFormat = mfmt;
    td.width  = (NSUInteger)d->width;
    td.height = (NSUInteger)d->height;
    td.usage = MTLTextureUsageShaderRead |
               (d->render_target ? MTLTextureUsageRenderTarget : 0);
    m->tex = [env.device newTextureWithDescriptor:td iosurface:m->iosurf plane:0];
    if (!m->tex) {
        gpu_log("metal: IOSurface 纹理创建失败");
        CFRelease(m->iosurf);
        free(m);
        return false;
    }
    img->backend = m;
    return true;
}

static bool mtlMemimgImport(gpu_memimg_t* img, const sc_gpu_memory_frame* src) {
    /* mac 导入源 = IOSurfaceRef（native 字段） */
    if (!src->native) {
        gpu_log("metal: memimg_import 需 native=IOSurfaceRef");
        return false;
    }
    MtlMemimg* m = (MtlMemimg*)calloc(1, sizeof(MtlMemimg));
    if (!m) return false;
    m->iosurf = (IOSurfaceRef)src->native;
    CFRetain(m->iosurf);
    MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
    td.textureType = MTLTextureType2D;
    td.pixelFormat = MTLPixelFormatBGRA8Unorm;
    td.width  = (NSUInteger)src->width;
    td.height = (NSUInteger)src->height;
    td.usage = MTLTextureUsageShaderRead;
    m->tex = [env.device newTextureWithDescriptor:td iosurface:m->iosurf plane:0];
    if (!m->tex) {
        CFRelease(m->iosurf);
        free(m);
        return false;
    }
    img->backend = m;
    return true;
}

static bool mtlMemimgExport(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence) {
    (void)with_fence;   /* mac 验证版：无 fence（sync_fd=-1，消费前 sc_gfx_finish） */
    MtlMemimg* m = (MtlMemimg*)img->backend;
    if (!m) return false;
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

static void* mtlMemimgNative(gpu_memimg_t* img) {
    MtlMemimg* m = (MtlMemimg*)img->backend;
    return m ? (__bridge void*)m->tex : NULL;   /* gfx 后端消费 MTLTexture */
}

static void* mtlMemimgMap(gpu_memimg_t* img, int plane, uint32_t* out_stride) {
    (void)plane;
    MtlMemimg* m = (MtlMemimg*)img->backend;
    if (!m) return NULL;
    IOSurfaceLock(m->iosurf, kIOSurfaceLockReadOnly, NULL);
    if (out_stride) *out_stride = (uint32_t)IOSurfaceGetBytesPerRow(m->iosurf);
    return IOSurfaceGetBaseAddress(m->iosurf);
}

static void mtlMemimgUnmap(gpu_memimg_t* img, int plane) {
    (void)plane;
    MtlMemimg* m = (MtlMemimg*)img->backend;
    if (m) IOSurfaceUnlock(m->iosurf, kIOSurfaceLockReadOnly, NULL);
}

static void mtlMemimgFree(gpu_memimg_t* img) {
    MtlMemimg* m = (MtlMemimg*)img->backend;
    if (!m) return;
    m->tex = nil;
    if (m->iosurf) CFRelease(m->iosurf);
    free(m);
    img->backend = NULL;
}

static bool mtlSurfaceDequeue(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out) {
    gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[slot]);
    if (!img) return false;
    return mtlMemimgExport(img, out, false);
}

/* ---- vtable ------------------------------------------------ */

static const gpu_env_api mtlApi = {
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
    .memimg_alloc = mtlMemimgAlloc,
    .memimg_import = mtlMemimgImport,
    .memimg_export = mtlMemimgExport,
    .memimg_native = mtlMemimgNative,
    .memimg_map = mtlMemimgMap,
    .memimg_unmap = mtlMemimgUnmap,
    .memimg_free = mtlMemimgFree,
    .surface_dequeue = mtlSurfaceDequeue,
};

const gpu_env_api* gpu_env_metal(void) { return &mtlApi; }

#endif /* SC_GPU_METAL */
