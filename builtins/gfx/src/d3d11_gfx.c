/* ============================================================
 * d3d11_gfx.c —— Direct3D 11 渲染后端（gfx 层）
 * ============================================================
 * gfx_backend_api 的 D3D11 实现：消费 HLSL（运行时 D3DCompile→DXBC）+
 * 反射清单，建资源/管线，立即上下文录制并绘制，交换链 pass 呈现。
 *
 * 与 env 的契约（gpu_d3d.h）：sc_gpu_device()=sc_gpu_d3d_device*（device+context）；
 * begin_pass 内 sc_gpu_frame_acquire() 取 RTV/DSV；commit 末尾 sc_gpu_frame_end() 呈现。
 * 仅 SC_GPU_D3D11 编入时有效（Windows）。
 * ============================================================ */
#include "internal.h"

#ifdef SC_GPU_D3D11

#include "../../gpu/gpu_d3d.h"   /* COBJMACROS + d3d11.h + dxgi1_2.h */
#include <d3dcompiler.h>          /* D3DCompile：HLSL→DXBC 运行时编译 */
#include <stdlib.h>
#include <string.h>

#define GD3D_UBO_SLOTS 8          /* 每 stage 常量缓冲槽上限 */

/* ---- 后端私有资源体 --------------------------------------- */
typedef struct {
    ID3D11Buffer* buf;
    UINT          size;
    bool          dynamic;        /* host 可写（USAGE_DYNAMIC + CPU_WRITE） */
} D3dBuffer;

typedef struct {
    ID3D11Texture2D*          tex;
    ID3D11ShaderResourceView* srv;
    ID3D11RenderTargetView*   rtv;    /* 渲染目标（memimg 绑定/离屏） */
    DXGI_FORMAT               format;
    int                       width, height;
    bool                      borrowed; /* memimg 绑定：tex 借自 env，仅 own view */
} D3dImage;

typedef struct {
    ID3D11SamplerState* smp;
} D3dSampler;

typedef struct {
    ID3D11VertexShader* vs;
    ID3D11PixelShader*  ps;
    ID3D11ComputeShader* cs;
    void*  vsBlob;      /* VS DXBC（建 input layout 需签名）；ID3DBlob* */
    void*  psBlob;
    void*  csBlob;
} D3dShader;

typedef struct {
    ID3D11InputLayout*        layout;
    ID3D11VertexShader*       vs;
    ID3D11PixelShader*        ps;
    ID3D11ComputeShader*      cs;
    ID3D11RasterizerState*    raster;
    ID3D11BlendState*         blend;
    ID3D11DepthStencilState*  depth;
    D3D11_PRIMITIVE_TOPOLOGY  topo;
    UINT                      strides[SC_GFX_MAX_VERTEX_BUFFERS];
    DXGI_FORMAT               idxFormat;
    bool                      indexed;
    bool                      compute;
} D3dPipeline;

/* ---- 全局状态 --------------------------------------------- */
typedef struct {
    bool                  valid;
    sc_gpu_d3d_device*    dev;      /* device + 立即 context */
    /* 帧内状态 */
    D3dPipeline*          curPipe;
    ID3D11RenderTargetView* curRtv;
    ID3D11DepthStencilView* curDsv;
    int                   curW, curH;
    bool                  inPass;
} GfxD3d;
static GfxD3d g;

#define D3D_DEV() (g.dev->device)
#define D3D_CTX() (g.dev->context)

/* ============================================================
 * 格式 / 状态映射
 * ============================================================ */
static DXGI_FORMAT gd3d_pixfmt(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_R8:       return DXGI_FORMAT_R8_UNORM;
        case SC_GPU_PIXELFORMAT_RG8:      return DXGI_FORMAT_R8G8_UNORM;
        case SC_GPU_PIXELFORMAT_RGBA8:    return DXGI_FORMAT_R8G8B8A8_UNORM;
        case SC_GPU_PIXELFORMAT_SRGB8A8:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case SC_GPU_PIXELFORMAT_BGRA8:    return DXGI_FORMAT_B8G8R8A8_UNORM;
        case SC_GPU_PIXELFORMAT_RGBA16F:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case SC_GPU_PIXELFORMAT_RGBA32F:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case SC_GPU_PIXELFORMAT_R32F:     return DXGI_FORMAT_R32_FLOAT;
        case SC_GPU_PIXELFORMAT_RG32F:    return DXGI_FORMAT_R32G32_FLOAT;
        case SC_GPU_PIXELFORMAT_DEFAULT:  return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:                          return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

static DXGI_FORMAT gd3d_vertfmt(sc_gfx_vertex_format f) {
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:   return DXGI_FORMAT_R32_FLOAT;
        case SC_GFX_VERTEXFORMAT_FLOAT2:  return DXGI_FORMAT_R32G32_FLOAT;
        case SC_GFX_VERTEXFORMAT_FLOAT3:  return DXGI_FORMAT_R32G32B32_FLOAT;
        case SC_GFX_VERTEXFORMAT_FLOAT4:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case SC_GFX_VERTEXFORMAT_BYTE4:   return DXGI_FORMAT_R8G8B8A8_SINT;
        case SC_GFX_VERTEXFORMAT_BYTE4N:  return DXGI_FORMAT_R8G8B8A8_SNORM;
        case SC_GFX_VERTEXFORMAT_UBYTE4:  return DXGI_FORMAT_R8G8B8A8_UINT;
        case SC_GFX_VERTEXFORMAT_UBYTE4N: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case SC_GFX_VERTEXFORMAT_SHORT2:  return DXGI_FORMAT_R16G16_SINT;
        case SC_GFX_VERTEXFORMAT_SHORT2N: return DXGI_FORMAT_R16G16_SNORM;
        case SC_GFX_VERTEXFORMAT_SHORT4:  return DXGI_FORMAT_R16G16B16A16_SINT;
        case SC_GFX_VERTEXFORMAT_SHORT4N: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case SC_GFX_VERTEXFORMAT_HALF2:   return DXGI_FORMAT_R16G16_FLOAT;
        case SC_GFX_VERTEXFORMAT_HALF4:   return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case SC_GFX_VERTEXFORMAT_UINT:    return DXGI_FORMAT_R32_UINT;
        default:                          return DXGI_FORMAT_R32G32B32_FLOAT;
    }
}

static int gd3d_vertfmt_size(sc_gfx_vertex_format f) {
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:   return 4;
        case SC_GFX_VERTEXFORMAT_FLOAT2:  return 8;
        case SC_GFX_VERTEXFORMAT_FLOAT3:  return 12;
        case SC_GFX_VERTEXFORMAT_FLOAT4:  return 16;
        case SC_GFX_VERTEXFORMAT_BYTE4:
        case SC_GFX_VERTEXFORMAT_BYTE4N:
        case SC_GFX_VERTEXFORMAT_UBYTE4:
        case SC_GFX_VERTEXFORMAT_UBYTE4N:
        case SC_GFX_VERTEXFORMAT_SHORT2:
        case SC_GFX_VERTEXFORMAT_SHORT2N:
        case SC_GFX_VERTEXFORMAT_HALF2:
        case SC_GFX_VERTEXFORMAT_UINT:    return 4;
        case SC_GFX_VERTEXFORMAT_SHORT4:
        case SC_GFX_VERTEXFORMAT_SHORT4N:
        case SC_GFX_VERTEXFORMAT_HALF4:   return 8;
        default:                          return 12;
    }
}

static D3D11_PRIMITIVE_TOPOLOGY gd3d_topo(sc_gfx_primitive p) {
    switch (p) {
        case SC_GFX_PRIMITIVE_POINTS:         return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        case SC_GFX_PRIMITIVE_LINES:          return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case SC_GFX_PRIMITIVE_LINE_STRIP:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case SC_GFX_PRIMITIVE_TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        default:                              return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static D3D11_CULL_MODE gd3d_cull(sc_gfx_cull c) {
    switch (c) {
        case SC_GFX_CULL_FRONT: return D3D11_CULL_FRONT;
        case SC_GFX_CULL_BACK:  return D3D11_CULL_BACK;
        default:                return D3D11_CULL_NONE;
    }
}

static D3D11_COMPARISON_FUNC gd3d_compare(sc_gfx_compare c) {
    switch (c) {
        case SC_GFX_COMPARE_NEVER:         return D3D11_COMPARISON_NEVER;
        case SC_GFX_COMPARE_LESS:          return D3D11_COMPARISON_LESS;
        case SC_GFX_COMPARE_EQUAL:         return D3D11_COMPARISON_EQUAL;
        case SC_GFX_COMPARE_LESS_EQUAL:    return D3D11_COMPARISON_LESS_EQUAL;
        case SC_GFX_COMPARE_GREATER:       return D3D11_COMPARISON_GREATER;
        case SC_GFX_COMPARE_NOT_EQUAL:     return D3D11_COMPARISON_NOT_EQUAL;
        case SC_GFX_COMPARE_GREATER_EQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
        default:                           return D3D11_COMPARISON_ALWAYS;
    }
}

static D3D11_BLEND gd3d_blend(sc_gfx_blend_factor f) {
    switch (f) {
        case SC_GFX_BLEND_ZERO:                  return D3D11_BLEND_ZERO;
        case SC_GFX_BLEND_ONE:                   return D3D11_BLEND_ONE;
        case SC_GFX_BLEND_SRC_COLOR:             return D3D11_BLEND_SRC_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_SRC_COLOR:   return D3D11_BLEND_INV_SRC_COLOR;
        case SC_GFX_BLEND_SRC_ALPHA:             return D3D11_BLEND_SRC_ALPHA;
        case SC_GFX_BLEND_ONE_MINUS_SRC_ALPHA:   return D3D11_BLEND_INV_SRC_ALPHA;
        case SC_GFX_BLEND_DST_COLOR:             return D3D11_BLEND_DEST_COLOR;
        case SC_GFX_BLEND_ONE_MINUS_DST_COLOR:   return D3D11_BLEND_INV_DEST_COLOR;
        case SC_GFX_BLEND_DST_ALPHA:             return D3D11_BLEND_DEST_ALPHA;
        case SC_GFX_BLEND_ONE_MINUS_DST_ALPHA:   return D3D11_BLEND_INV_DEST_ALPHA;
        case SC_GFX_BLEND_SRC_ALPHA_SATURATED:   return D3D11_BLEND_SRC_ALPHA_SAT;
        case SC_GFX_BLEND_BLEND_COLOR:           return D3D11_BLEND_BLEND_FACTOR;
        case SC_GFX_BLEND_ONE_MINUS_BLEND_COLOR: return D3D11_BLEND_INV_BLEND_FACTOR;
        default:                                 return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND_OP gd3d_blendop(sc_gfx_blend_op o) {
    switch (o) {
        case SC_GFX_BLENDOP_SUBTRACT:         return D3D11_BLEND_OP_SUBTRACT;
        case SC_GFX_BLENDOP_REVERSE_SUBTRACT: return D3D11_BLEND_OP_REV_SUBTRACT;
        case SC_GFX_BLENDOP_MIN:              return D3D11_BLEND_OP_MIN;
        case SC_GFX_BLENDOP_MAX:              return D3D11_BLEND_OP_MAX;
        default:                              return D3D11_BLEND_OP_ADD;
    }
}

/* ============================================================
 * 生命周期
 * ============================================================ */
static bool gd3dInit(const sc_gfx_desc* desc) {
    (void)desc;
    memset(&g, 0, sizeof(g));
    g.dev = (sc_gpu_d3d_device*)sc_gpu_device();
    if (!g.dev || !g.dev->device || !g.dev->context) {
        gfx_log("d3d11-gfx: 无 D3D11 设备");
        return false;
    }
    g.valid = true;
    return true;
}

static void gd3dShutdown(void) {
    if (!g.valid) return;
    if (g.dev && g.dev->context) ID3D11DeviceContext_ClearState(g.dev->context);
    memset(&g, 0, sizeof(g));
}

static void gd3dFinish(void) {
    if (g.valid) ID3D11DeviceContext_Flush(g.dev->context);
}

/* ============================================================
 * 资源：buffer / image / sampler
 * ============================================================ */
static bool gd3dBufferCreate(gfx_buffer_t* buf) {
    D3dBuffer* b = (D3dBuffer*)calloc(1, sizeof(D3dBuffer));
    if (!b) return false;
    UINT bind = D3D11_BIND_VERTEX_BUFFER;
    switch (buf->desc.kind) {
        case SC_GFX_BUFFERKIND_INDEX:   bind = D3D11_BIND_INDEX_BUFFER; break;
        case SC_GFX_BUFFERKIND_STORAGE: bind = D3D11_BIND_SHADER_RESOURCE; break;
        default:                        bind = D3D11_BIND_VERTEX_BUFFER; break;
    }
    size_t sz = buf->desc.size ? buf->desc.size : buf->desc.data.size;
    if (sz == 0) sz = 16;
    b->size = (UINT)sz;

    D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = (UINT)((sz + 15) & ~(size_t)15);  /* 16 对齐 */
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA init; memset(&init, 0, sizeof(init));
    const D3D11_SUBRESOURCE_DATA* pInit = NULL;
    if (buf->desc.data.ptr && buf->desc.data.size) { init.pSysMem = buf->desc.data.ptr; pInit = &init; }
    if (FAILED(ID3D11Device_CreateBuffer(D3D_DEV(), &bd, pInit, &b->buf))) { free(b); return false; }
    buf->backend = b;
    return true;
}

static void gd3dBufferDestroy(gfx_buffer_t* buf) {
    D3dBuffer* b = (D3dBuffer*)buf->backend;
    if (!b) return;
    if (b->buf) ID3D11Buffer_Release(b->buf);
    free(b);
    buf->backend = NULL;
}

static void gd3dBufferUpdate(gfx_buffer_t* buf, const sc_gfx_range* data, int offset) {
    D3dBuffer* b = (D3dBuffer*)buf->backend;
    if (!b || !b->buf || !data || !data->ptr) return;
    /* DEFAULT 用途：整块或子区更新经 UpdateSubresource（无 box=整块） */
    if (offset == 0) {
        ID3D11DeviceContext_UpdateSubresource(D3D_CTX(), (ID3D11Resource*)b->buf, 0, NULL,
                                              data->ptr, 0, 0);
    } else {
        D3D11_BOX box; memset(&box, 0, sizeof(box));
        box.left = (UINT)offset; box.right = (UINT)(offset + data->size);
        box.bottom = 1; box.back = 1;
        ID3D11DeviceContext_UpdateSubresource(D3D_CTX(), (ID3D11Resource*)b->buf, 0, &box,
                                              data->ptr, 0, 0);
    }
}

static bool gd3dImageCreate(gfx_image_t* img) {
    D3dImage* im = (D3dImage*)calloc(1, sizeof(D3dImage));
    if (!im) return false;
    ID3D11Device* d = D3D_DEV();
    im->width = img->desc.width; im->height = img->desc.height;
    im->format = gd3d_pixfmt(img->desc.format ? img->desc.format : SC_GPU_PIXELFORMAT_RGBA8);

    /* Mode B：绑定 env 的 memimg ID3D11Texture2D，仅建自有 RTV/SRV（不 own tex） */
    if (img->desc.memimg) {
        im->tex = (ID3D11Texture2D*)sc_gpu_memimg_native(img->desc.memimg);
        if (!im->tex) { gfx_log("d3d11-gfx: memimg %u 无效", img->desc.memimg); free(im); return false; }
        im->borrowed = true;
        ID3D11Texture2D_AddRef(im->tex);   /* 借用期持引用，destroy 时 Release 平衡 */
        D3D11_TEXTURE2D_DESC td; ID3D11Texture2D_GetDesc(im->tex, &td);
        im->format = td.Format;
        if (td.BindFlags & D3D11_BIND_RENDER_TARGET)
            ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource*)im->tex, NULL, &im->rtv);
        if (td.BindFlags & D3D11_BIND_SHADER_RESOURCE)
            ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource*)im->tex, NULL, &im->srv);
        img->backend = im;
        return true;
    }

    UINT bind = D3D11_BIND_SHADER_RESOURCE;
    if (img->desc.render_target) bind |= D3D11_BIND_RENDER_TARGET;
    D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof(td));
    td.Width = (UINT)(im->width > 0 ? im->width : 1);
    td.Height = (UINT)(im->height > 0 ? im->height : 1);
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = im->format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = bind;
    if (FAILED(ID3D11Device_CreateTexture2D(d, &td, NULL, &im->tex))) { free(im); return false; }
    ID3D11Device_CreateShaderResourceView(d, (ID3D11Resource*)im->tex, NULL, &im->srv);
    if (img->desc.render_target)
        ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource*)im->tex, NULL, &im->rtv);
    img->backend = im;
    return true;
}

static void gd3dImageDestroy(gfx_image_t* img) {
    D3dImage* im = (D3dImage*)img->backend;
    if (!im) return;
    if (im->srv) ID3D11ShaderResourceView_Release(im->srv);
    if (im->rtv) ID3D11RenderTargetView_Release(im->rtv);
    if (im->tex) ID3D11Texture2D_Release(im->tex);   /* borrowed 亦 Release（create 时 AddRef 过） */
    free(im);
    img->backend = NULL;
}

static void gd3dImageUpdate(gfx_image_t* img, const sc_gfx_image_data* data) {
    D3dImage* im = (D3dImage*)img->backend;
    if (!im || !im->tex || !data) return;
    /* 单 mip 单层上传（薄层）：pixels[0][0] + 行距 */
    const void* px = data->subimage[0][0].ptr;
    if (!px) return;
    UINT rowPitch = (UINT)im->width * 4;
    ID3D11DeviceContext_UpdateSubresource(D3D_CTX(), (ID3D11Resource*)im->tex, 0, NULL, px, rowPitch, 0);
}

static D3D11_FILTER gd3d_filter(sc_gfx_filter minf, sc_gfx_filter magf, sc_gfx_filter mipf) {
    bool lin = (minf == SC_GFX_FILTER_LINEAR) || (magf == SC_GFX_FILTER_LINEAR) ||
               (mipf == SC_GFX_FILTER_LINEAR);
    return lin ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
}
static D3D11_TEXTURE_ADDRESS_MODE gd3d_wrap(sc_gfx_wrap w) {
    switch (w) {
        case SC_GFX_WRAP_CLAMP:  return D3D11_TEXTURE_ADDRESS_CLAMP;
        case SC_GFX_WRAP_MIRROR: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case SC_GFX_WRAP_BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
        default:                 return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

static bool gd3dSamplerCreate(gfx_sampler_t* smp) {
    D3dSampler* s = (D3dSampler*)calloc(1, sizeof(D3dSampler));
    if (!s) return false;
    D3D11_SAMPLER_DESC sd; memset(&sd, 0, sizeof(sd));
    sd.Filter = gd3d_filter(smp->desc.min_filter, smp->desc.mag_filter, smp->desc.mipmap_filter);
    sd.AddressU = gd3d_wrap(smp->desc.wrap_u);
    sd.AddressV = gd3d_wrap(smp->desc.wrap_v);
    sd.AddressW = gd3d_wrap(smp->desc.wrap_w);
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (FAILED(ID3D11Device_CreateSamplerState(D3D_DEV(), &sd, &s->smp))) { free(s); return false; }
    smp->backend = s;
    return true;
}

static void gd3dSamplerDestroy(gfx_sampler_t* smp) {
    D3dSampler* s = (D3dSampler*)smp->backend;
    if (!s) return;
    if (s->smp) ID3D11SamplerState_Release(s->smp);
    free(s);
    smp->backend = NULL;
}

/* ============================================================
 * shader / pipeline
 * ============================================================ */
/* HLSL 文本运行时编译为 DXBC（D3DCompile）；失败打印编译器诊断。 */
static ID3DBlob* compile_hlsl(const void* code, size_t size, const char* entry, const char* profile) {
    if (!code || size < 4) return NULL;
    ID3DBlob* blob = NULL;
    ID3DBlob* err = NULL;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    const char* e = (entry && entry[0]) ? entry : "main";
    HRESULT hr = D3DCompile(code, size, NULL, NULL, NULL, e, profile, flags, 0, &blob, &err);
    if (FAILED(hr)) {
        gfx_log("d3d11-gfx: HLSL 编译失败 (%s/%s): %s", e, profile,
                err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err) ID3D10Blob_Release(err);
        if (blob) ID3D10Blob_Release(blob);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    return blob;
}

static bool gd3dShaderCreate(gfx_shader_t* shd, const sc_gfx_shader_desc* desc) {
    D3dShader* s = (D3dShader*)calloc(1, sizeof(D3dShader));
    if (!s) return false;
    ID3D11Device* d = D3D_DEV();
    /* SPIRV-Cross HLSL 入口恒为 "main"（同 MSL/Vulkan 约定） */
    if (desc->cs.code.ptr) {
        ID3DBlob* cb = compile_hlsl(desc->cs.code.ptr, desc->cs.code.size, "main", "cs_5_0");
        if (!cb) { free(s); return false; }
        ID3D11Device_CreateComputeShader(d, ID3D10Blob_GetBufferPointer(cb),
                                         ID3D10Blob_GetBufferSize(cb), NULL, &s->cs);
        s->csBlob = cb;
    } else {
        ID3DBlob* vb = compile_hlsl(desc->vs.code.ptr, desc->vs.code.size, "main", "vs_5_0");
        ID3DBlob* pb = compile_hlsl(desc->fs.code.ptr, desc->fs.code.size, "main", "ps_5_0");
        if (!vb || !pb) {
            if (vb) ID3D10Blob_Release(vb);
            if (pb) ID3D10Blob_Release(pb);
            free(s); return false;
        }
        ID3D11Device_CreateVertexShader(d, ID3D10Blob_GetBufferPointer(vb),
                                        ID3D10Blob_GetBufferSize(vb), NULL, &s->vs);
        ID3D11Device_CreatePixelShader(d, ID3D10Blob_GetBufferPointer(pb),
                                       ID3D10Blob_GetBufferSize(pb), NULL, &s->ps);
        s->vsBlob = vb;   /* 建 input layout 需 VS 签名 */
        s->psBlob = pb;
    }
    shd->backend = s;
    return true;
}

static void gd3dShaderDestroy(gfx_shader_t* shd) {
    D3dShader* s = (D3dShader*)shd->backend;
    if (!s) return;
    if (s->vs) ID3D11VertexShader_Release(s->vs);
    if (s->ps) ID3D11PixelShader_Release(s->ps);
    if (s->cs) ID3D11ComputeShader_Release(s->cs);
    if (s->vsBlob) ID3D10Blob_Release((ID3DBlob*)s->vsBlob);
    if (s->psBlob) ID3D10Blob_Release((ID3DBlob*)s->psBlob);
    if (s->csBlob) ID3D10Blob_Release((ID3DBlob*)s->csBlob);
    free(s);
    shd->backend = NULL;
}

static bool gd3dPipelineCreate(gfx_pipeline_t* pip) {
    D3dShader* sh = (D3dShader*)pip->shader->backend;
    if (!sh) return false;
    ID3D11Device* d = D3D_DEV();
    D3dPipeline* p = (D3dPipeline*)calloc(1, sizeof(D3dPipeline));
    if (!p) return false;

    if (pip->desc.compute && sh->cs) {
        p->compute = true;
        p->cs = sh->cs; ID3D11ComputeShader_AddRef(sh->cs);
        pip->backend = p;
        return true;
    }

    p->vs = sh->vs; if (sh->vs) ID3D11VertexShader_AddRef(sh->vs);
    p->ps = sh->ps; if (sh->ps) ID3D11PixelShader_AddRef(sh->ps);
    p->topo = gd3d_topo(pip->desc.primitive);

    /* 输入布局：顶点属性 → D3D11_INPUT_ELEMENT_DESC（SPIRV-Cross HLSL 用 TEXCOORD<loc> 语义） */
    D3D11_INPUT_ELEMENT_DESC elems[SC_GFX_MAX_VERTEX_ATTRS];
    UINT nElem = 0;
    int autoOff[SC_GFX_MAX_VERTEX_BUFFERS]; memset(autoOff, 0, sizeof(autoOff));
    for (int i = 0; i < SC_GFX_MAX_VERTEX_ATTRS; i++) {
        sc_gfx_vertex_format vf = pip->desc.attrs[i].format;
        if (vf == SC_GFX_VERTEXFORMAT_INVALID) continue;
        int bi = pip->desc.attrs[i].buffer_index;
        int off = pip->desc.attrs[i].offset;
        if (off == 0 && nElem > 0) off = autoOff[bi];
        memset(&elems[nElem], 0, sizeof(elems[nElem]));
        elems[nElem].SemanticName = "TEXCOORD";
        elems[nElem].SemanticIndex = (UINT)i;
        elems[nElem].Format = gd3d_vertfmt(vf);
        elems[nElem].InputSlot = (UINT)bi;
        elems[nElem].AlignedByteOffset = (UINT)off;
        elems[nElem].InputSlotClass = pip->desc.buffers[bi].step_per_instance
            ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
        elems[nElem].InstanceDataStepRate = pip->desc.buffers[bi].step_per_instance ? 1 : 0;
        autoOff[bi] = off + gd3d_vertfmt_size(vf);
        nElem++;
    }
    for (int b = 0; b < SC_GFX_MAX_VERTEX_BUFFERS; b++) {
        int stride = pip->desc.buffers[b].stride;
        if (stride == 0) stride = autoOff[b];
        p->strides[b] = (UINT)stride;
    }
    if (nElem > 0 && sh->vsBlob) {
        ID3D11Device_CreateInputLayout(d, elems, nElem,
            ID3D10Blob_GetBufferPointer((ID3DBlob*)sh->vsBlob),
            ID3D10Blob_GetBufferSize((ID3DBlob*)sh->vsBlob), &p->layout);
    }

    /* 光栅化状态 */
    D3D11_RASTERIZER_DESC rd; memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = gd3d_cull(pip->desc.cull);
    rd.FrontCounterClockwise = (pip->desc.winding == SC_GFX_WINDING_CCW) ? TRUE : FALSE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    if (pip->desc.depth.bias != 0.0f || pip->desc.depth.bias_slope_scale != 0.0f) {
        rd.DepthBias = (INT)pip->desc.depth.bias;
        rd.SlopeScaledDepthBias = pip->desc.depth.bias_slope_scale;
        rd.DepthBiasClamp = pip->desc.depth.bias_clamp;
    }
    ID3D11Device_CreateRasterizerState(d, &rd, &p->raster);

    /* 混合状态（单目标） */
    D3D11_BLEND_DESC bd; memset(&bd, 0, sizeof(bd));
    const sc_gfx_blend_state* bs = &pip->desc.colors[0].blend;
    D3D11_RENDER_TARGET_BLEND_DESC* rt = &bd.RenderTarget[0];
    rt->RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (bs->enabled) {
        rt->BlendEnable = TRUE;
        rt->SrcBlend = gd3d_blend(bs->src_factor_rgb);
        rt->DestBlend = gd3d_blend(bs->dst_factor_rgb);
        rt->BlendOp = gd3d_blendop(bs->op_rgb);
        rt->SrcBlendAlpha = gd3d_blend(bs->src_factor_alpha);
        rt->DestBlendAlpha = gd3d_blend(bs->dst_factor_alpha);
        rt->BlendOpAlpha = gd3d_blendop(bs->op_alpha);
    }
    ID3D11Device_CreateBlendState(d, &bd, &p->blend);

    /* 深度模板状态 */
    D3D11_DEPTH_STENCIL_DESC dd; memset(&dd, 0, sizeof(dd));
    bool wantDepth = (pip->desc.depth.compare != SC_GFX_COMPARE_DEFAULT &&
                      pip->desc.depth.compare != SC_GFX_COMPARE_ALWAYS) ||
                     pip->desc.depth.write_enabled;
    dd.DepthEnable = wantDepth ? TRUE : FALSE;
    dd.DepthWriteMask = pip->desc.depth.write_enabled ? D3D11_DEPTH_WRITE_MASK_ALL
                                                      : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = gd3d_compare(pip->desc.depth.compare);
    ID3D11Device_CreateDepthStencilState(d, &dd, &p->depth);

    p->idxFormat = (pip->desc.index_type == SC_GFX_INDEXTYPE_UINT32)
        ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    p->indexed = (pip->desc.index_type == SC_GFX_INDEXTYPE_UINT16 ||
                  pip->desc.index_type == SC_GFX_INDEXTYPE_UINT32);
    pip->backend = p;
    return true;
}

static void gd3dPipelineDestroy(gfx_pipeline_t* pip) {
    D3dPipeline* p = (D3dPipeline*)pip->backend;
    if (!p) return;
    if (p->layout) ID3D11InputLayout_Release(p->layout);
    if (p->vs) ID3D11VertexShader_Release(p->vs);
    if (p->ps) ID3D11PixelShader_Release(p->ps);
    if (p->cs) ID3D11ComputeShader_Release(p->cs);
    if (p->raster) ID3D11RasterizerState_Release(p->raster);
    if (p->blend) ID3D11BlendState_Release(p->blend);
    if (p->depth) ID3D11DepthStencilState_Release(p->depth);
    free(p);
    pip->backend = NULL;
}

/* ============================================================
 * 帧：pass / draw / commit
 * ============================================================ */
/* 常量缓冲池：[stage 0=vs 1=ps][slot]，惰性建 DYNAMIC，apply_uniforms 更新+绑定 */
static ID3D11Buffer* g_cbuf[2][GD3D_UBO_SLOTS];
static UINT          g_cbufSize[2][GD3D_UBO_SLOTS];

static void gd3dBeginPass(const sc_gfx_pass* pass, gfx_image_t* colors[], int color_count,
                          gfx_image_t* resolve[], gfx_image_t* depth) {
    (void)resolve;
    if (pass->compute) return;
    ID3D11DeviceContext* ctx = D3D_CTX();

    ID3D11RenderTargetView* rtv = NULL;
    ID3D11DepthStencilView* dsv = NULL;
    int w = 0, h = 0;

    if (color_count > 0) {
        /* Mode B：显式离屏 color 附件（memimg 绑定的 gfx image） */
        D3dImage* ci = (D3dImage*)colors[0]->backend;
        if (!ci || !ci->rtv) { gfx_log("d3d11-gfx: 离屏 color 无 RTV"); return; }
        rtv = ci->rtv; w = ci->width; h = ci->height;
        if (depth) { D3dImage* di = (D3dImage*)depth->backend; /* 离屏深度暂不建 DSV */ (void)di; }
    } else {
        /* Mode A / 窗口：从 env 取当前帧 RTV/DSV */
        sc_gpu_frame frame;
        if (!sc_gpu_frame_acquire(&frame)) { gfx_log("d3d11-gfx: frame_acquire 失败"); return; }
        rtv = (ID3D11RenderTargetView*)frame.color;
        dsv = (ID3D11DepthStencilView*)frame.depth;
        w = frame.width; h = frame.height;
    }

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);
    if (pass->action.colors[0].load == SC_GFX_LOADACTION_CLEAR ||
        pass->action.colors[0].load == 0) {
        float c[4] = { pass->action.colors[0].clear[0], pass->action.colors[0].clear[1],
                       pass->action.colors[0].clear[2], pass->action.colors[0].clear[3] };
        if (rtv) ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, c);
    }
    if (dsv) {
        float cd = pass->action.depth.clear_depth;
        ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv,
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            (cd == 0.0f) ? 1.0f : cd, (UINT8)pass->action.depth.clear_stencil);
    }

    D3D11_VIEWPORT vp; memset(&vp, 0, sizeof(vp));
    vp.Width = (FLOAT)w; vp.Height = (FLOAT)h; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    D3D11_RECT sc = { 0, 0, w, h };
    ID3D11DeviceContext_RSSetScissorRects(ctx, 1, &sc);

    g.curRtv = rtv; g.curDsv = dsv; g.curW = w; g.curH = h;
    g.inPass = true; g.curPipe = NULL;
}

static void gd3dApplyViewport(int x, int y, int w, int h, bool top_left) {
    (void)top_left;
    if (!g.inPass) return;
    D3D11_VIEWPORT vp; memset(&vp, 0, sizeof(vp));
    vp.TopLeftX = (FLOAT)x; vp.TopLeftY = (FLOAT)y;
    vp.Width = (FLOAT)w; vp.Height = (FLOAT)h; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(D3D_CTX(), 1, &vp);
}

static void gd3dApplyScissor(int x, int y, int w, int h, bool top_left) {
    (void)top_left;
    if (!g.inPass) return;
    D3D11_RECT r = { x, y, x + w, y + h };
    ID3D11DeviceContext_RSSetScissorRects(D3D_CTX(), 1, &r);
}

static void gd3dApplyPipeline(gfx_pipeline_t* pip) {
    D3dPipeline* p = (D3dPipeline*)pip->backend;
    if (!g.inPass || !p) return;
    g.curPipe = p;
    ID3D11DeviceContext* ctx = D3D_CTX();
    if (p->compute) { ID3D11DeviceContext_CSSetShader(ctx, p->cs, NULL, 0); return; }
    ID3D11DeviceContext_IASetInputLayout(ctx, p->layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, p->topo);
    ID3D11DeviceContext_VSSetShader(ctx, p->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, p->ps, NULL, 0);
    ID3D11DeviceContext_RSSetState(ctx, p->raster);
    float bf[4] = { 1, 1, 1, 1 };
    ID3D11DeviceContext_OMSetBlendState(ctx, p->blend, bf, 0xFFFFFFFF);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, p->depth, 0);
}

static void gd3dApplyBindings(gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                              gfx_buffer_t* vbufs[], gfx_buffer_t* ibuf,
                              gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                              gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                              gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]) {
    (void)sbufs;
    if (!g.inPass || !g.curPipe) return;
    D3dPipeline* p = g.curPipe;
    ID3D11DeviceContext* ctx = D3D_CTX();

    ID3D11Buffer* vb[SC_GFX_MAX_VERTEX_BUFFERS];
    UINT strides[SC_GFX_MAX_VERTEX_BUFFERS], offsets[SC_GFX_MAX_VERTEX_BUFFERS];
    UINT nvb = 0;
    for (int i = 0; i < SC_GFX_MAX_VERTEX_BUFFERS; i++) {
        if (!vbufs[i]) break;
        D3dBuffer* b = (D3dBuffer*)vbufs[i]->backend;
        if (!b) break;
        vb[nvb] = b->buf;
        strides[nvb] = p->strides[i] ? p->strides[i] : 0;
        offsets[nvb] = (UINT)bnd->vertex_buffer_offsets[i];
        nvb++;
    }
    if (nvb > 0) ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, nvb, vb, strides, offsets);

    if (ibuf) {
        D3dBuffer* b = (D3dBuffer*)ibuf->backend;
        if (b) ID3D11DeviceContext_IASetIndexBuffer(ctx, b->buf, p->idxFormat,
                                                    (UINT)bnd->index_buffer_offset);
    }

    /* 纹理 + 采样器（片段 stage；按 slot 0 起） */
    for (int st = 0; st < 3; st++) {
        for (int i = 0; i < SC_GFX_MAX_IMAGES; i++) {
            if (!imgs || !imgs[st] || !imgs[st][i]) continue;
            D3dImage* im = (D3dImage*)imgs[st][i]->backend;
            if (im && im->srv) ID3D11DeviceContext_PSSetShaderResources(ctx, (UINT)i, 1, &im->srv);
        }
        for (int i = 0; i < SC_GFX_MAX_SAMPLERS; i++) {
            if (!smps || !smps[st] || !smps[st][i]) continue;
            D3dSampler* sm = (D3dSampler*)smps[st][i]->backend;
            if (sm && sm->smp) ID3D11DeviceContext_PSSetSamplers(ctx, (UINT)i, 1, &sm->smp);
        }
    }
}

static void gd3dApplyUniforms(int stage, int slot, const void* data, size_t size) {
    if (!g.inPass || slot < 0 || slot >= GD3D_UBO_SLOTS || !data || size == 0) return;
    int st = (stage == SC_GFX_STAGE_FRAGMENT) ? 1 : 0;
    ID3D11Device* d = D3D_DEV();
    ID3D11DeviceContext* ctx = D3D_CTX();
    UINT need = (UINT)((size + 15) & ~(size_t)15);
    if (!g_cbuf[st][slot] || g_cbufSize[st][slot] < need) {
        if (g_cbuf[st][slot]) { ID3D11Buffer_Release(g_cbuf[st][slot]); g_cbuf[st][slot] = NULL; }
        D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = need;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateBuffer(d, &bd, NULL, &g_cbuf[st][slot]))) return;
        g_cbufSize[st][slot] = need;
    }
    D3D11_MAPPED_SUBRESOURCE ms; memset(&ms, 0, sizeof(ms));
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource*)g_cbuf[st][slot], 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, data, size);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource*)g_cbuf[st][slot], 0);
    }
    if (st == 1) ID3D11DeviceContext_PSSetConstantBuffers(ctx, (UINT)slot, 1, &g_cbuf[st][slot]);
    else         ID3D11DeviceContext_VSSetConstantBuffers(ctx, (UINT)slot, 1, &g_cbuf[st][slot]);
}

static void gd3dDraw(int base, int count, int instances) {
    if (!g.inPass || !g.curPipe) return;
    ID3D11DeviceContext* ctx = D3D_CTX();
    if (instances <= 1) {
        if (g.curPipe->indexed) ID3D11DeviceContext_DrawIndexed(ctx, (UINT)count, (UINT)base, 0);
        else                    ID3D11DeviceContext_Draw(ctx, (UINT)count, (UINT)base);
    } else {
        if (g.curPipe->indexed) ID3D11DeviceContext_DrawIndexedInstanced(ctx, (UINT)count, (UINT)instances, (UINT)base, 0, 0);
        else                    ID3D11DeviceContext_DrawInstanced(ctx, (UINT)count, (UINT)instances, (UINT)base, 0);
    }
}

static void gd3dDispatch(int gx, int gy, int gz) {
    if (!g.curPipe || !g.curPipe->compute) return;
    ID3D11DeviceContext_Dispatch(D3D_CTX(), (UINT)gx, (UINT)gy, (UINT)gz);
}

static void gd3dEndPass(void) {
    g.inPass = false;   /* D3D11 立即模式：无 renderpass 收尾 */
}

static void gd3dCommit(void) {
    sc_gpu_frame_end();   /* 呈现（窗口 Present / MEMORY flush） */
    g.curPipe = NULL; g.curRtv = NULL; g.curDsv = NULL;
}

static void gd3dQueryPixelformat(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out) {
    memset(out, 0, sizeof(*out));
    DXGI_FORMAT vf = gd3d_pixfmt(fmt);
    bool isDepth = (fmt == SC_GPU_PIXELFORMAT_DEPTH || fmt == SC_GPU_PIXELFORMAT_DEPTH_STENCIL);
    UINT sup = 0;
    if (g.dev && SUCCEEDED(ID3D11Device_CheckFormatSupport(g.dev->device, vf, &sup))) {
        out->sample = (sup & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ? 1 : 0;
        out->filter = out->sample;
        out->render = (sup & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ? 1 : 0;
        out->blend  = (sup & D3D11_FORMAT_SUPPORT_BLENDABLE) ? 1 : 0;
        out->msaa   = (sup & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) ? 1 : 0;
    }
    out->depth = isDepth ? 1 : 0;
}

/* ============================================================
 * vtable
 * ============================================================ */
static const gfx_backend_api d3dApi = {
    .name = "d3d11",
    .init = gd3dInit,
    .shutdown = gd3dShutdown,
    .finish = gd3dFinish,
    .buffer_create = gd3dBufferCreate,
    .buffer_destroy = gd3dBufferDestroy,
    .buffer_update = gd3dBufferUpdate,
    .image_create = gd3dImageCreate,
    .image_destroy = gd3dImageDestroy,
    .image_update = gd3dImageUpdate,
    .sampler_create = gd3dSamplerCreate,
    .sampler_destroy = gd3dSamplerDestroy,
    .shader_create = gd3dShaderCreate,
    .shader_destroy = gd3dShaderDestroy,
    .pipeline_create = gd3dPipelineCreate,
    .pipeline_destroy = gd3dPipelineDestroy,
    .begin_pass = gd3dBeginPass,
    .apply_viewport = gd3dApplyViewport,
    .apply_scissor = gd3dApplyScissor,
    .apply_pipeline = gd3dApplyPipeline,
    .apply_bindings = gd3dApplyBindings,
    .apply_uniforms = gd3dApplyUniforms,
    .draw = gd3dDraw,
    .dispatch = gd3dDispatch,
    .end_pass = gd3dEndPass,
    .commit = gd3dCommit,
    .query_pixelformat = gd3dQueryPixelformat,
};

const gfx_backend_api* gfx_backend_d3d11(void) { return &d3dApi; }

/* ==== PART4_MARKER ==== */

#endif /* SC_GPU_D3D11 */
