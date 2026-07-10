/* ============================================================
 * d3d11_env.c —— Direct3D 11 运行环境后端（env 层）
 * ============================================================
 * gpu_env_api 的 D3D11 实现：ID3D11Device + 立即上下文、DXGI 交换链
 * （CreateSwapChainForHwnd，FLIP_DISCARD）、每帧 RTV/DSV 交付与 Present。
 * MEMORY surface / memimg：D3D11 共享纹理（MISC_SHARED_NTHANDLE 零拷贝导出）
 * + staging 回读。仅 SC_GPU_D3D11 编入时有效（Windows）。
 *
 * 与 gfx 的契约见 gpu_d3d.h：sc_gpu_device() 返回 sc_gpu_d3d_device*；
 * 每帧 sc_gpu_frame.color = ID3D11RenderTargetView*、.depth = ID3D11DepthStencilView*。
 * ============================================================ */
#include "internal.h"

#ifdef SC_GPU_D3D11

#include "../gpu_d3d.h"     /* COBJMACROS + d3d11.h + dxgi1_2.h */
#include <stdlib.h>
#include <string.h>

/* ---- 像素格式映射 ---------------------------------------- */
static DXGI_FORMAT d3d_color_format(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_RGBA8:    return DXGI_FORMAT_R8G8B8A8_UNORM;
        case SC_GPU_PIXELFORMAT_SRGB8A8:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case SC_GPU_PIXELFORMAT_BGRA8:    return DXGI_FORMAT_B8G8R8A8_UNORM;
        case SC_GPU_PIXELFORMAT_RGB10A2:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        case SC_GPU_PIXELFORMAT_RGBA16F:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case SC_GPU_PIXELFORMAT_RGBA32F:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case SC_GPU_PIXELFORMAT_R8:       return DXGI_FORMAT_R8_UNORM;
        case SC_GPU_PIXELFORMAT_RG8:      return DXGI_FORMAT_R8G8_UNORM;
        case SC_GPU_PIXELFORMAT_DEFAULT:  return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:                          return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
}
static DXGI_FORMAT d3d_depth_format(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_NONE:          return DXGI_FORMAT_UNKNOWN;
        case SC_GPU_PIXELFORMAT_DEPTH:         return DXGI_FORMAT_D32_FLOAT;
        case SC_GPU_PIXELFORMAT_DEPTH_STENCIL: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:                               return DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
}

/* ---- memimg 私有：共享 D3D11 纹理 + staging 回读 ---------- */
typedef struct D3dMemimg {
    ID3D11Texture2D*        tex;      /* BGRA8 渲染目标（可共享导出） */
    ID3D11RenderTargetView* rtv;
    ID3D11Texture2D*        staging;  /* CPU 回读（懒建，USAGE_STAGING） */
    HANDLE                  shared;   /* 导出的共享 NT 句柄（借用；free 时 CloseHandle） */
    int                     w, h;
    void*                   mapped;   /* map 期间 staging 的映射指针 */
    bool                    exportable;
} D3dMemimg;

/* ---- 每 surface：交换链或 MEMORY 环 ---------------------- */
typedef struct D3dSurface {
    bool                     memory;
    /* WINDOW */
    IDXGISwapChain1*         swap;
    ID3D11RenderTargetView*  rtv;      /* backbuffer RTV */
    int                      syncInterval;
    /* 共享深度（WINDOW 与 MEMORY 环共用一张） */
    ID3D11Texture2D*         depthTex;
    ID3D11DepthStencilView*  dsv;
    bool                     hasDepth;
    DXGI_FORMAT              colorFmt;
    DXGI_FORMAT              depthFmt;
    int                      w, h;
} D3dSurface;

/* ---- 全局 env 状态 ---------------------------------------- */
typedef struct {
    bool               valid;
    sc_gpu_d3d_device  dev;      /* device() 返回本结构指针 */
    IDXGIFactory2*     factory;
    D3dSurface*        cur;      /* 当前 surface（frame_end 呈现它） */
} D3dEnv;
static D3dEnv g_d3d;

/* ============================================================
 * 初始化 / 收尾
 * ============================================================ */
static bool d3dInit(const sc_gpu_desc* desc) {
    (void)desc;
    memset(&g_d3d, 0, sizeof(g_d3d));

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
                                   want, 2, D3D11_SDK_VERSION,
                                   &g_d3d.dev.device, &got, &g_d3d.dev.context);
    if (FAILED(hr) || !g_d3d.dev.device) {
        gpu_log("d3d11: D3D11CreateDevice 失败 (hr=0x%08lX)", (unsigned long)hr);
        return false;
    }

    /* 取 IDXGIFactory2（device → IDXGIDevice → adapter → parent factory） */
    IDXGIDevice* dxgiDev = NULL;
    if (SUCCEEDED(ID3D11Device_QueryInterface(g_d3d.dev.device, &IID_IDXGIDevice, (void**)&dxgiDev)) && dxgiDev) {
        IDXGIAdapter* adapter = NULL;
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgiDev, &adapter)) && adapter) {
            IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void**)&g_d3d.factory);
            IDXGIAdapter_Release(adapter);
        }
        IDXGIDevice_Release(dxgiDev);
    }
    if (!g_d3d.factory) {
        gpu_log("d3d11: 取 IDXGIFactory2 失败");
        ID3D11DeviceContext_Release(g_d3d.dev.context);
        ID3D11Device_Release(g_d3d.dev.device);
        memset(&g_d3d, 0, sizeof(g_d3d));
        return false;
    }

    D3D11_FEATURE_DATA_D3D11_OPTIONS opt; memset(&opt, 0, sizeof(opt));
    gpu_log("d3d11: 设备就绪 (feature level %d.%d)",
            (got >> 12) & 0xF, (got >> 8) & 0xF);
    g_d3d.valid = true;
    return true;
}

static void d3dShutdown(void) {
    if (!g_d3d.valid) return;
    if (g_d3d.dev.context) ID3D11DeviceContext_ClearState(g_d3d.dev.context);
    if (g_d3d.factory)     IDXGIFactory2_Release(g_d3d.factory);
    if (g_d3d.dev.context) ID3D11DeviceContext_Release(g_d3d.dev.context);
    if (g_d3d.dev.device)  ID3D11Device_Release(g_d3d.dev.device);
    memset(&g_d3d, 0, sizeof(g_d3d));
}

static void* d3dDeviceFn(void) { return &g_d3d.dev; }

/* ---- 深度缓冲创建（WINDOW/MEMORY 共用） ------------------- */
static bool d3d_make_depth(D3dSurface* s, int w, int h) {
    ID3D11Device* d = g_d3d.dev.device;
    D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof(td));
    td.Width = (UINT)w; td.Height = (UINT)h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = s->depthFmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &s->depthTex))) return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC dvd; memset(&dvd, 0, sizeof(dvd));
    dvd.Format = s->depthFmt;
    dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(ID3D11Device_CreateDepthStencilView(d, (ID3D11Resource*)s->depthTex, &dvd, &s->dsv)))
        return false;
    return true;
}

/* ---- 交换链 backbuffer RTV（创建/重建） ------------------ */
static bool d3d_make_backbuffer_rtv(D3dSurface* s) {
    ID3D11Device* d = g_d3d.dev.device;
    ID3D11Texture2D* back = NULL;
    if (FAILED(IDXGISwapChain1_GetBuffer(s->swap, 0, &IID_ID3D11Texture2D, (void**)&back)) || !back)
        return false;
    HRESULT hr = ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource*)back, NULL, &s->rtv);
    ID3D11Texture2D_Release(back);
    return SUCCEEDED(hr);
}

/* ============================================================
 * memimg
 * ============================================================ */
static bool d3dMemimgAlloc(gpu_memimg_t* img) {
    const sc_gpu_memimg_desc* de = &img->desc;
    ID3D11Device* d = g_d3d.dev.device;
    D3dMemimg* m = (D3dMemimg*)calloc(1, sizeof(D3dMemimg));
    if (!m) return false;
    m->w = de->width; m->h = de->height;

    D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof(td));
    td.Width = (UINT)m->w; td.Height = (UINT)m->h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = d3d_color_format(de->format);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &m->tex))) {
        /* 退化：不可共享（部分格式/驱动） */
        td.MiscFlags = 0;
        if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &m->tex))) { free(m); return false; }
    } else {
        m->exportable = true;
    }
    if (FAILED(ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource*)m->tex, NULL, &m->rtv))) {
        ID3D11Texture2D_Release(m->tex); free(m); return false;
    }
    img->backend = m;
    return true;
}

static bool d3dMemimgImport(gpu_memimg_t* img, const sc_gpu_memory_frame* src) {
    (void)img; (void)src;
    gpu_log("d3d11: memimg import 暂不支持（需 OpenSharedResource1 路径）");
    return false;
}

static bool d3dMemimgExport(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence) {
    D3dMemimg* m = (D3dMemimg*)img->backend;
    if (!m) return false;
    if (with_fence) ID3D11DeviceContext_Flush(g_d3d.dev.context);
    if (m->exportable && !m->shared) {
        IDXGIResource1* res = NULL;
        if (SUCCEEDED(ID3D11Texture2D_QueryInterface(m->tex, &IID_IDXGIResource1, (void**)&res)) && res) {
            IDXGIResource1_CreateSharedHandle(res, NULL,
                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &m->shared);
            IDXGIResource1_Release(res);
            if (m->shared) gpu_log("d3d11: memimg 导出共享 NT 句柄 %p (%dx%d 零拷贝)", m->shared, m->w, m->h);
        }
    }
    out->planes = 1;
    out->fd[0] = -1;
    out->stride[0] = (uint32_t)(m->w * 4);
    out->offset[0] = 0;
    out->fourcc = img->desc.fourcc;
    out->width = m->w;
    out->height = m->h;
    out->sync_fd = -1;
    out->native = m->shared;   /* 共享 NT 句柄（可导入 D3D / 送编码器）；无则 NULL */
    return true;
}

static void* d3dMemimgNative(gpu_memimg_t* img) {
    D3dMemimg* m = (D3dMemimg*)img->backend;
    return m ? (void*)m->tex : NULL;   /* gfx Mode B 直接绑此 ID3D11Texture2D 建 RTV */
}

static void* d3dMemimgMap(gpu_memimg_t* img, int plane, uint32_t* out_stride) {
    (void)plane;
    D3dMemimg* m = (D3dMemimg*)img->backend;
    if (!m) return NULL;
    ID3D11Device* d = g_d3d.dev.device;
    ID3D11DeviceContext* ctx = g_d3d.dev.context;
    if (!m->staging) {
        D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof(td));
        td.Width = (UINT)m->w; td.Height = (UINT)m->h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &m->staging))) return NULL;
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource*)m->staging, (ID3D11Resource*)m->tex);
    D3D11_MAPPED_SUBRESOURCE ms; memset(&ms, 0, sizeof(ms));
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource*)m->staging, 0, D3D11_MAP_READ, 0, &ms)))
        return NULL;
    m->mapped = ms.pData;
    if (out_stride) *out_stride = ms.RowPitch;
    return ms.pData;
}

static void d3dMemimgUnmap(gpu_memimg_t* img, int plane) {
    (void)plane;
    D3dMemimg* m = (D3dMemimg*)img->backend;
    if (!m || !m->mapped) return;
    ID3D11DeviceContext_Unmap(g_d3d.dev.context, (ID3D11Resource*)m->staging, 0);
    m->mapped = NULL;
}

static void d3dMemimgFree(gpu_memimg_t* img) {
    D3dMemimg* m = (D3dMemimg*)img->backend;
    if (!m) return;
    if (m->shared)  CloseHandle(m->shared);
    if (m->staging) ID3D11Texture2D_Release(m->staging);
    if (m->rtv)     ID3D11RenderTargetView_Release(m->rtv);
    if (m->tex)     ID3D11Texture2D_Release(m->tex);
    free(m);
    img->backend = NULL;
}

/* ============================================================
 * surface 生命周期
 * ============================================================ */
static bool d3dMemorySurfaceCreate(gpu_surface_t* surf, D3dSurface* s) {
    s->memory = true;
    s->w = surf->desc.width; s->h = surf->desc.height;
    s->depthFmt = d3d_depth_format(surf->desc.depth_format);
    s->hasDepth = (surf->desc.depth_format != SC_GPU_PIXELFORMAT_NONE) &&
                  (s->depthFmt != DXGI_FORMAT_UNKNOWN);
    if (s->hasDepth && !d3d_make_depth(s, s->w, s->h)) {
        gpu_log("d3d11: MEMORY 深度创建失败");
        s->hasDepth = false;
    }
    gpu_log("d3d11: MEMORY surface 就绪 (%dx%d, %d 镜像环)", s->w, s->h, surf->desc.image_count);
    return true;
}

static bool d3dSurfaceCreate(gpu_surface_t* surf) {
    D3dSurface* s = (D3dSurface*)calloc(1, sizeof(D3dSurface));
    if (!s) return false;

    if (surf->desc.kind == SC_GPU_SURFACE_MEMORY) {
        if (!d3dMemorySurfaceCreate(surf, s)) { free(s); return false; }
        surf->backend = s;
        if (!g_d3d.cur) g_d3d.cur = s;
        return true;
    }

    if (!surf->desc.native_window) {
        gpu_log("d3d11: WINDOW surface 缺 native_window (HWND)");
        free(s); return false;
    }
    s->w = surf->desc.width; s->h = surf->desc.height;
    s->colorFmt = d3d_color_format(surf->desc.color_format);
    /* FLIP 交换链不支持 sRGB backbuffer 格式，用 UNORM（sRGB 经 RTV 视图处理，暂用 UNORM） */
    if (s->colorFmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) s->colorFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    s->syncInterval = surf->desc.swap_interval;

    DXGI_SWAP_CHAIN_DESC1 sd; memset(&sd, 0, sizeof(sd));
    sd.Width = (UINT)s->w; sd.Height = (UINT)s->h;
    sd.Format = s->colorFmt;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    HRESULT hr = IDXGIFactory2_CreateSwapChainForHwnd(
        g_d3d.factory, (IUnknown*)g_d3d.dev.device, (HWND)surf->desc.native_window,
        &sd, NULL, NULL, &s->swap);
    if (FAILED(hr) || !s->swap) {
        gpu_log("d3d11: CreateSwapChainForHwnd 失败 (hr=0x%08lX)", (unsigned long)hr);
        free(s); return false;
    }
    /* 禁用 DXGI 的 Alt+Enter 全屏切换（交给窗口层） */
    IDXGIFactory2_MakeWindowAssociation(g_d3d.factory, (HWND)surf->desc.native_window,
                                        DXGI_MWA_NO_ALT_ENTER);
    if (!d3d_make_backbuffer_rtv(s)) {
        gpu_log("d3d11: backbuffer RTV 创建失败");
        IDXGISwapChain1_Release(s->swap); free(s); return false;
    }
    s->depthFmt = d3d_depth_format(surf->desc.depth_format);
    s->hasDepth = (surf->desc.depth_format != SC_GPU_PIXELFORMAT_NONE) &&
                  (s->depthFmt != DXGI_FORMAT_UNKNOWN);
    if (s->hasDepth && !d3d_make_depth(s, s->w, s->h)) s->hasDepth = false;

    surf->backend = s;
    if (!g_d3d.cur) g_d3d.cur = s;
    gpu_log("d3d11: WINDOW surface 就绪 (%dx%d)", s->w, s->h);
    return true;
}

static void d3d_release_targets(D3dSurface* s) {
    if (s->dsv)      { ID3D11DepthStencilView_Release(s->dsv); s->dsv = NULL; }
    if (s->depthTex) { ID3D11Texture2D_Release(s->depthTex); s->depthTex = NULL; }
    if (s->rtv)      { ID3D11RenderTargetView_Release(s->rtv); s->rtv = NULL; }
}

static void d3dSurfaceDestroy(gpu_surface_t* surf) {
    D3dSurface* s = (D3dSurface*)surf->backend;
    if (!s) return;
    if (g_d3d.dev.context) ID3D11DeviceContext_ClearState(g_d3d.dev.context);
    d3d_release_targets(s);
    if (s->swap) IDXGISwapChain1_Release(s->swap);
    if (g_d3d.cur == s) g_d3d.cur = NULL;
    free(s);
    surf->backend = NULL;
}

static void d3dSurfaceActivate(gpu_surface_t* surf) {
    g_d3d.cur = surf ? (D3dSurface*)surf->backend : NULL;
}

static void d3dSurfaceResize(gpu_surface_t* surf, int w, int h) {
    D3dSurface* s = (D3dSurface*)surf->backend;
    if (!s || w <= 0 || h <= 0) return;
    surf->desc.width = w; surf->desc.height = h;
    s->w = w; s->h = h;
    if (s->memory) { d3d_release_targets(s); if (s->hasDepth) d3d_make_depth(s, w, h); return; }
    if (g_d3d.dev.context) ID3D11DeviceContext_ClearState(g_d3d.dev.context);
    d3d_release_targets(s);
    IDXGISwapChain1_ResizeBuffers(s->swap, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    d3d_make_backbuffer_rtv(s);
    if (s->hasDepth) d3d_make_depth(s, w, h);
}

/* ============================================================
 * 帧交付
 * ============================================================ */
static bool d3dFrameAcquire(gpu_surface_t* surf, sc_gpu_frame* out) {
    D3dSurface* s = (D3dSurface*)surf->backend;
    if (!s) return false;
    g_d3d.cur = s;
    memset(out, 0, sizeof(*out));

    if (s->memory) {
        if (surf->ring_cur < 0) return false;
        gpu_memimg_t* mi = gpu_lookup_memimg(surf->ring_imgs[surf->ring_cur]);
        D3dMemimg* m = mi ? (D3dMemimg*)mi->backend : NULL;
        if (!m) return false;
        out->color = m->rtv;
        out->depth = s->hasDepth ? (void*)s->dsv : NULL;
        out->width = m->w; out->height = m->h;
        out->sample_count = 1;
        out->color_format = surf->desc.color_format;
        out->depth_format = s->hasDepth ? surf->desc.depth_format : SC_GPU_PIXELFORMAT_NONE;
        return true;
    }

    out->color = s->rtv;
    out->depth = s->hasDepth ? (void*)s->dsv : NULL;
    out->width = s->w; out->height = s->h;
    out->sample_count = 1;
    out->color_format = surf->desc.color_format;
    out->depth_format = s->hasDepth ? surf->desc.depth_format : SC_GPU_PIXELFORMAT_NONE;
    return true;
}

static void d3dFrameEnd(void) {
    D3dSurface* s = g_d3d.cur;
    if (!s) return;
    if (s->memory) { ID3D11DeviceContext_Flush(g_d3d.dev.context); return; }
    if (s->swap) IDXGISwapChain1_Present(s->swap, s->syncInterval > 0 ? 1 : 0, 0);
}

static bool d3dSurfaceDequeue(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out) {
    gpu_memimg_t* img = gpu_lookup_memimg(surf->ring_imgs[slot]);
    if (!img) return false;
    return d3dMemimgExport(img, out, false);
}

/* ============================================================
 * vtable
 * ============================================================ */
static const gpu_env_api d3dApi = {
    .name = "d3d11",
    .kind = SC_GPU_BACKEND_D3D11,
    .init = d3dInit,
    .shutdown = d3dShutdown,
    .device = d3dDeviceFn,
    .surface_create = d3dSurfaceCreate,
    .surface_destroy = d3dSurfaceDestroy,
    .surface_activate = d3dSurfaceActivate,
    .surface_resize = d3dSurfaceResize,
    .frame_acquire = d3dFrameAcquire,
    .frame_end = d3dFrameEnd,
    .memimg_alloc = d3dMemimgAlloc,
    .memimg_import = d3dMemimgImport,
    .memimg_export = d3dMemimgExport,
    .memimg_native = d3dMemimgNative,
    .memimg_map = d3dMemimgMap,
    .memimg_unmap = d3dMemimgUnmap,
    .memimg_free = d3dMemimgFree,
    .surface_dequeue = d3dSurfaceDequeue,
};

const gpu_env_api* gpu_env_d3d11(void) { return &d3dApi; }

#endif /* SC_GPU_D3D11 */
