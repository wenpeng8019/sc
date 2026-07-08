/* ============================================================
 * metal_gfx.m —— Metal 渲染后端（macOS）
 * ============================================================
 * 参照 sokol_gfx 的 _sg_mtl_* 实现对齐其已解决的问题：
 *   · 双帧并行（in-flight=2）+ 信号量节流
 *   · 延迟释放队列：资源销毁时入队，等 GPU 用完该帧再真正释放
 *   · DYNAMIC/STREAM 缓冲双副本轮转（每帧首次更新时切换）
 *   · 每帧独立 uniform 环缓冲（256 对齐追加）
 *   · MSAA：离屏 pass 支持 resolve 附件（STOREACTION_RESOLVE）；
 *     交换链 MSAA 由 gpu env 提供 msaa 纹理，此处 resolve 到 drawable
 *   · viewport/scissor 钳制到当前 pass 边界（Metal 校验层要求）
 *
 * 与 gpu（env 层）的衔接：
 *   · init：设备经 sc_gpu_device() 取（env 已创建）
 *   · 交换链 pass：sc_gpu_frame_acquire() 取本帧渲染目标（含
 *     resize 竞态校验后的真实尺寸）
 *   · commit：presentDrawable 挂命令缓冲（最佳呈现节拍）→ 提交
 *     → sc_gpu_frame_end() 收尾
 *
 * 绑定约定（与 scc 反射清单对应）：
 *   uniform 块  → [[buffer(binding)]]（vs/fs/cs 各自槽位空间）
 *   顶点数据    → [[buffer(MTL_VBUF_BASE + 顶点缓冲槽)]]
 *   纹理/采样器 → [[texture(binding)]] / [[sampler(binding)]]
 *   storage     → [[buffer(binding)]]
 *   入口名默认 "main0"（scc MSL 产物为改名后的 .sg 阶段函数名）。
 * ============================================================ */

#include "internal.h"   /* 先引入：后端宏按目标平台自推导（见 internal.h） */
#ifdef SC_GPU_METAL

#include <float.h>
#include <string.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#define MTL_MAX_INFLIGHT   SC_GFX_MAX_INFLIGHT_FRAMES   /* 2 */
#define MTL_UB_RING_SIZE   (4 * 1024 * 1024)
#define MTL_VBUF_BASE      8       /* 顶点缓冲起始槽（0..7 留给 uniform） */
#define MTL_MAX_PRESENT    16      /* 单帧最多呈现的 drawable 数 */

/* ---- 后端私有体 -------------------------------------------- */

typedef struct MtlBuffer {
    id<MTLBuffer> buf[MTL_MAX_INFLIGHT];
    int           numSlots;     /* IMMUTABLE=1，DYNAMIC/STREAM=2 */
    int           active;
    uint32_t      updFrame;     /* 上次轮转所在帧（每帧只转一次） */
} MtlBuffer;

typedef struct MtlImage {
    id<MTLTexture> tex;
} MtlImage;

typedef struct MtlSampler {
    id<MTLSamplerState> smp;
} MtlSampler;

typedef struct MtlShader {
    id<MTLLibrary>  vsLib, fsLib, csLib;
    id<MTLFunction> vsFn, fsFn, csFn;
} MtlShader;

typedef struct MtlPipeline {
    id<MTLRenderPipelineState>  rps;
    id<MTLComputePipelineState> cps;
    id<MTLDepthStencilState>    dss;
    MTLPrimitiveType primitive;
    MTLCullMode      cull;
    MTLWinding       winding;
    MTLIndexType     indexType;
    int              indexSize;
    MTLSize          threadsPerGroup;
    /* 编码器级状态（Metal 中不属于 PSO） */
    float   depthBias, depthBiasSlope, depthBiasClamp;
    float   blendColor[4];
    uint8_t stencilRef;
    bool    hasStencil;
} MtlPipeline;

/* ---- 全局状态 ---------------------------------------------- */

static struct {
    id<MTLDevice>       device;   /* 借自 gpu env（sc_gpu_device） */
    id<MTLCommandQueue> queue;
    dispatch_semaphore_t sem;

    id<MTLCommandBuffer>        cmd;
    id<MTLCommandBuffer>        lastCmd;   /* 最后提交的命令缓冲（finish 用） */
    id<MTLRenderCommandEncoder> renc;
    id<MTLComputeCommandEncoder> cenc;

    /* 每帧槽位：uniform 环 + 延迟释放队列 */
    id<MTLBuffer>   ubRing[MTL_MAX_INFLIGHT];
    NSMutableArray* releaseQueue[MTL_MAX_INFLIGHT];
    int             ubPos;
    uint32_t        frameIndex;

    id<CAMetalDrawable> present[MTL_MAX_PRESENT];   /* 本帧待呈现 */
    int                 presentCount;

    id<MTLBuffer> curIndexBuf;
    int           curIndexOffset;
    MtlPipeline*  curPip;
    int           curPassWidth, curPassHeight;
} mtl;

static int mtlFrameSlot(void) { return (int)(mtl.frameIndex % MTL_MAX_INFLIGHT); }

/* 延迟释放：入队保持引用，等该帧槽位复用（GPU 必已用完）再清 */
static void mtlDeferRelease(id obj) {
    if (!obj) return;
    [mtl.releaseQueue[mtlFrameSlot()] addObject:obj];
}

/* ---- 格式映射 ---------------------------------------------- */

static MTLPixelFormat toMtlFormat(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_R8:            return MTLPixelFormatR8Unorm;
        case SC_GPU_PIXELFORMAT_R16F:          return MTLPixelFormatR16Float;
        case SC_GPU_PIXELFORMAT_R32F:          return MTLPixelFormatR32Float;
        case SC_GPU_PIXELFORMAT_RG8:           return MTLPixelFormatRG8Unorm;
        case SC_GPU_PIXELFORMAT_RG16F:         return MTLPixelFormatRG16Float;
        case SC_GPU_PIXELFORMAT_RG32F:         return MTLPixelFormatRG32Float;
        case SC_GPU_PIXELFORMAT_RGBA8:         return MTLPixelFormatRGBA8Unorm;
        case SC_GPU_PIXELFORMAT_SRGB8A8:       return MTLPixelFormatRGBA8Unorm_sRGB;
        case SC_GPU_PIXELFORMAT_DEFAULT:
        case SC_GPU_PIXELFORMAT_BGRA8:         return MTLPixelFormatBGRA8Unorm;
        case SC_GPU_PIXELFORMAT_RGB10A2:       return MTLPixelFormatRGB10A2Unorm;
        case SC_GPU_PIXELFORMAT_RGBA16F:       return MTLPixelFormatRGBA16Float;
        case SC_GPU_PIXELFORMAT_RGBA32F:       return MTLPixelFormatRGBA32Float;
        case SC_GPU_PIXELFORMAT_DEPTH:         return MTLPixelFormatDepth32Float;
        case SC_GPU_PIXELFORMAT_DEPTH_STENCIL: return MTLPixelFormatDepth32Float_Stencil8;
        default:                               return MTLPixelFormatInvalid;
    }
}

static int formatByteSize(sc_gpu_pixel_format f) {
    switch (f) {
        case SC_GPU_PIXELFORMAT_R8:      return 1;
        case SC_GPU_PIXELFORMAT_R16F:
        case SC_GPU_PIXELFORMAT_RG8:     return 2;
        case SC_GPU_PIXELFORMAT_R32F:
        case SC_GPU_PIXELFORMAT_RG16F:
        case SC_GPU_PIXELFORMAT_RGBA8:
        case SC_GPU_PIXELFORMAT_SRGB8A8:
        case SC_GPU_PIXELFORMAT_DEFAULT:
        case SC_GPU_PIXELFORMAT_BGRA8:
        case SC_GPU_PIXELFORMAT_RGB10A2: return 4;
        case SC_GPU_PIXELFORMAT_RG32F:
        case SC_GPU_PIXELFORMAT_RGBA16F: return 8;
        case SC_GPU_PIXELFORMAT_RGBA32F: return 16;
        default:                         return 4;
    }
}

static MTLVertexFormat toMtlVertexFormat(sc_gfx_vertex_format f) {
    switch (f) {
        case SC_GFX_VERTEXFORMAT_FLOAT:    return MTLVertexFormatFloat;
        case SC_GFX_VERTEXFORMAT_FLOAT2:   return MTLVertexFormatFloat2;
        case SC_GFX_VERTEXFORMAT_FLOAT3:   return MTLVertexFormatFloat3;
        case SC_GFX_VERTEXFORMAT_FLOAT4:   return MTLVertexFormatFloat4;
        case SC_GFX_VERTEXFORMAT_BYTE4:    return MTLVertexFormatChar4;
        case SC_GFX_VERTEXFORMAT_BYTE4N:   return MTLVertexFormatChar4Normalized;
        case SC_GFX_VERTEXFORMAT_UBYTE4:   return MTLVertexFormatUChar4;
        case SC_GFX_VERTEXFORMAT_UBYTE4N:  return MTLVertexFormatUChar4Normalized;
        case SC_GFX_VERTEXFORMAT_SHORT2:   return MTLVertexFormatShort2;
        case SC_GFX_VERTEXFORMAT_SHORT2N:  return MTLVertexFormatShort2Normalized;
        case SC_GFX_VERTEXFORMAT_SHORT4:   return MTLVertexFormatShort4;
        case SC_GFX_VERTEXFORMAT_SHORT4N:  return MTLVertexFormatShort4Normalized;
        case SC_GFX_VERTEXFORMAT_USHORT2:  return MTLVertexFormatUShort2;
        case SC_GFX_VERTEXFORMAT_USHORT2N: return MTLVertexFormatUShort2Normalized;
        case SC_GFX_VERTEXFORMAT_USHORT4:  return MTLVertexFormatUShort4;
        case SC_GFX_VERTEXFORMAT_USHORT4N: return MTLVertexFormatUShort4Normalized;
        case SC_GFX_VERTEXFORMAT_HALF2:    return MTLVertexFormatHalf2;
        case SC_GFX_VERTEXFORMAT_HALF4:    return MTLVertexFormatHalf4;
        case SC_GFX_VERTEXFORMAT_UINT10N2: return MTLVertexFormatUInt1010102Normalized;
        case SC_GFX_VERTEXFORMAT_UINT:     return MTLVertexFormatUInt;
        default:                           return MTLVertexFormatInvalid;
    }
}

static MTLCompareFunction toMtlCompare(sc_gfx_compare c) {
    switch (c) {
        case SC_GFX_COMPARE_NEVER:         return MTLCompareFunctionNever;
        case SC_GFX_COMPARE_LESS:          return MTLCompareFunctionLess;
        case SC_GFX_COMPARE_EQUAL:         return MTLCompareFunctionEqual;
        case SC_GFX_COMPARE_LESS_EQUAL:    return MTLCompareFunctionLessEqual;
        case SC_GFX_COMPARE_GREATER:       return MTLCompareFunctionGreater;
        case SC_GFX_COMPARE_NOT_EQUAL:     return MTLCompareFunctionNotEqual;
        case SC_GFX_COMPARE_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
        default:                           return MTLCompareFunctionAlways;
    }
}

static MTLStencilOperation toMtlStencilOp(sc_gfx_stencil_op op) {
    switch (op) {
        case SC_GFX_STENCILOP_ZERO:       return MTLStencilOperationZero;
        case SC_GFX_STENCILOP_REPLACE:    return MTLStencilOperationReplace;
        case SC_GFX_STENCILOP_INCR_CLAMP: return MTLStencilOperationIncrementClamp;
        case SC_GFX_STENCILOP_DECR_CLAMP: return MTLStencilOperationDecrementClamp;
        case SC_GFX_STENCILOP_INVERT:     return MTLStencilOperationInvert;
        case SC_GFX_STENCILOP_INCR_WRAP:  return MTLStencilOperationIncrementWrap;
        case SC_GFX_STENCILOP_DECR_WRAP:  return MTLStencilOperationDecrementWrap;
        default:                          return MTLStencilOperationKeep;
    }
}

static MTLBlendFactor toMtlBlendFactor(sc_gfx_blend_factor f) {
    switch (f) {
        case SC_GFX_BLEND_ZERO:                  return MTLBlendFactorZero;
        case SC_GFX_BLEND_SRC_COLOR:             return MTLBlendFactorSourceColor;
        case SC_GFX_BLEND_ONE_MINUS_SRC_COLOR:   return MTLBlendFactorOneMinusSourceColor;
        case SC_GFX_BLEND_SRC_ALPHA:             return MTLBlendFactorSourceAlpha;
        case SC_GFX_BLEND_ONE_MINUS_SRC_ALPHA:   return MTLBlendFactorOneMinusSourceAlpha;
        case SC_GFX_BLEND_DST_COLOR:             return MTLBlendFactorDestinationColor;
        case SC_GFX_BLEND_ONE_MINUS_DST_COLOR:   return MTLBlendFactorOneMinusDestinationColor;
        case SC_GFX_BLEND_DST_ALPHA:             return MTLBlendFactorDestinationAlpha;
        case SC_GFX_BLEND_ONE_MINUS_DST_ALPHA:   return MTLBlendFactorOneMinusDestinationAlpha;
        case SC_GFX_BLEND_SRC_ALPHA_SATURATED:   return MTLBlendFactorSourceAlphaSaturated;
        case SC_GFX_BLEND_BLEND_COLOR:           return MTLBlendFactorBlendColor;
        case SC_GFX_BLEND_ONE_MINUS_BLEND_COLOR: return MTLBlendFactorOneMinusBlendColor;
        default:                                 return MTLBlendFactorOne;
    }
}

static MTLBlendOperation toMtlBlendOp(sc_gfx_blend_op op) {
    switch (op) {
        case SC_GFX_BLENDOP_SUBTRACT:         return MTLBlendOperationSubtract;
        case SC_GFX_BLENDOP_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
        case SC_GFX_BLENDOP_MIN:              return MTLBlendOperationMin;
        case SC_GFX_BLENDOP_MAX:              return MTLBlendOperationMax;
        default:                              return MTLBlendOperationAdd;
    }
}

static MTLLoadAction toMtlLoad(sc_gfx_load_action a) {
    switch (a) {
        case SC_GFX_LOADACTION_LOAD:     return MTLLoadActionLoad;
        case SC_GFX_LOADACTION_DONTCARE: return MTLLoadActionDontCare;
        default:                         return MTLLoadActionClear;
    }
}

static MTLStoreAction toMtlStore(sc_gfx_store_action a) {
    switch (a) {
        case SC_GFX_STOREACTION_DONTCARE:      return MTLStoreActionDontCare;
        case SC_GFX_STOREACTION_RESOLVE:       return MTLStoreActionMultisampleResolve;
        case SC_GFX_STOREACTION_STORE_RESOLVE: return MTLStoreActionStoreAndMultisampleResolve;
        default:                               return MTLStoreActionStore;
    }
}

static MTLSamplerAddressMode toMtlWrap(sc_gfx_wrap w) {
    switch (w) {
        case SC_GFX_WRAP_CLAMP:  return MTLSamplerAddressModeClampToEdge;
        case SC_GFX_WRAP_MIRROR: return MTLSamplerAddressModeMirrorRepeat;
        case SC_GFX_WRAP_BORDER: return MTLSamplerAddressModeClampToBorderColor;
        default:                 return MTLSamplerAddressModeRepeat;
    }
}

/* ---- init/shutdown ----------------------------------------- */

static bool mtlInit(const sc_gfx_desc* desc) {
    (void)desc;
    memset((void*)&mtl, 0, sizeof(mtl));
    mtl.device = (__bridge id<MTLDevice>)sc_gpu_device();
    if (!mtl.device) { gfx_log("metal: gpu env 未交付设备"); return false; }
    mtl.queue = [mtl.device newCommandQueue];
    mtl.sem = dispatch_semaphore_create(MTL_MAX_INFLIGHT);
    for (int i = 0; i < MTL_MAX_INFLIGHT; i++) {
        mtl.ubRing[i] = [mtl.device newBufferWithLength:MTL_UB_RING_SIZE
                                                options:MTLResourceStorageModeShared];
        mtl.releaseQueue[i] = [NSMutableArray array];
    }
    return true;
}

static void mtlShutdown(void) {
    if (mtl.cmd) { [mtl.cmd commit]; [mtl.cmd waitUntilCompleted]; }
    if (mtl.lastCmd) { [mtl.lastCmd waitUntilCompleted]; mtl.lastCmd = nil; }
    /* 排空在飞帧后回填初值——dispatch 信号量释放时值低于初值会故意 crash */
    if (mtl.sem) {
        for (int i = 0; i < MTL_MAX_INFLIGHT; i++)
            dispatch_semaphore_wait(mtl.sem, DISPATCH_TIME_FOREVER);
        for (int i = 0; i < MTL_MAX_INFLIGHT; i++)
            dispatch_semaphore_signal(mtl.sem);
    }
    for (int i = 0; i < MTL_MAX_INFLIGHT; i++) {
        [mtl.releaseQueue[i] removeAllObjects];
        mtl.releaseQueue[i] = nil;
        mtl.ubRing[i] = nil;
    }
    for (int i = 0; i < MTL_MAX_PRESENT; i++) mtl.present[i] = nil;
    mtl.renc = nil; mtl.cenc = nil; mtl.cmd = nil;
    mtl.curIndexBuf = nil;
    mtl.queue = nil; mtl.device = nil; mtl.sem = nil;
    memset((void*)&mtl, 0, sizeof(mtl));
}

static void mtlGfxFinish(void) {
    if (mtl.lastCmd) {
        [mtl.lastCmd waitUntilCompleted];
        mtl.lastCmd = nil;
    }
}

/* ---- buffer ------------------------------------------------ */

static bool mtlBufferCreate(gfx_buffer_t* buf) {
    MtlBuffer* b = (MtlBuffer*)calloc(1, sizeof(MtlBuffer));
    if (!b) return false;
    b->numSlots = (buf->desc.usage == SC_GFX_USAGE_IMMUTABLE) ? 1 : MTL_MAX_INFLIGHT;
    for (int i = 0; i < b->numSlots; i++) {
        if (i == 0 && buf->desc.data.ptr) {
            b->buf[i] = [mtl.device newBufferWithBytes:buf->desc.data.ptr
                                                length:buf->desc.size
                                               options:MTLResourceStorageModeShared];
        } else {
            b->buf[i] = [mtl.device newBufferWithLength:buf->desc.size
                                                options:MTLResourceStorageModeShared];
        }
        if (!b->buf[i]) { free(b); return false; }
    }
    buf->backend = b;
    return true;
}

static void mtlBufferDestroy(gfx_buffer_t* buf) {
    MtlBuffer* b = (MtlBuffer*)buf->backend;
    if (!b) return;
    for (int i = 0; i < b->numSlots; i++) {
        mtlDeferRelease(b->buf[i]);
        b->buf[i] = nil;
    }
    free(b);
    buf->backend = NULL;
}

/* 每帧首次更新时轮转副本（sokol 语义：同帧多次 append 写同一副本） */
static void mtlBufferUpdate(gfx_buffer_t* buf, const sc_gfx_range* data, int offset) {
    MtlBuffer* b = (MtlBuffer*)buf->backend;
    if (!b) return;
    if (b->numSlots > 1 && b->updFrame != mtl.frameIndex) {
        b->active = (b->active + 1) % b->numSlots;
        b->updFrame = mtl.frameIndex;
    }
    memcpy((uint8_t*)b->buf[b->active].contents + offset, data->ptr, data->size);
}

static id<MTLBuffer> mtlActiveBuf(gfx_buffer_t* buf) {
    MtlBuffer* b = (MtlBuffer*)buf->backend;
    return b ? b->buf[b->active] : nil;
}

/* ---- image ------------------------------------------------- */

/* 子图像上传（cube 面 / 数组层 / 3D 深度 / mip 链） */
static void imageUploadData(gfx_image_t* img, id<MTLTexture> tex,
                            const sc_gfx_image_data* data) {
    const sc_gfx_image_desc* d = &img->desc;
    int bpp = formatByteSize(d->format);
    int faces = (d->kind == SC_GFX_IMAGEKIND_CUBE) ? 6 : 1;
    int slices = (d->kind == SC_GFX_IMAGEKIND_ARRAY) ? d->slices : 1;

    for (int f = 0; f < faces; f++) {
        for (int mip = 0; mip < d->mip_count; mip++) {
            const sc_gfx_range* r = &data->subimage[f][mip];
            if (!r->ptr) continue;
            int mw = d->width  >> mip; if (mw < 1) mw = 1;
            int mh = d->height >> mip; if (mh < 1) mh = 1;
            NSUInteger rowBytes = (NSUInteger)(mw * bpp);
            NSUInteger imgBytes = rowBytes * (NSUInteger)mh;

            if (d->kind == SC_GFX_IMAGEKIND_3D) {
                int md = d->slices >> mip; if (md < 1) md = 1;
                [tex replaceRegion:MTLRegionMake3D(0, 0, 0, (NSUInteger)mw,
                                                   (NSUInteger)mh, (NSUInteger)md)
                       mipmapLevel:(NSUInteger)mip
                             slice:0
                         withBytes:r->ptr
                       bytesPerRow:rowBytes
                     bytesPerImage:imgBytes];
            } else if (d->kind == SC_GFX_IMAGEKIND_ARRAY) {
                /* 数组：subimage[0][mip] 为整段连续数据，按层切开 */
                const uint8_t* p = (const uint8_t*)r->ptr;
                for (int s = 0; s < slices; s++) {
                    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)mw, (NSUInteger)mh)
                           mipmapLevel:(NSUInteger)mip
                                 slice:(NSUInteger)s
                             withBytes:p + (size_t)s * imgBytes
                           bytesPerRow:rowBytes
                         bytesPerImage:0];
                }
            } else {
                [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)mw, (NSUInteger)mh)
                       mipmapLevel:(NSUInteger)mip
                             slice:(NSUInteger)f
                         withBytes:r->ptr
                       bytesPerRow:rowBytes
                     bytesPerImage:0];
            }
        }
    }
}

static bool mtlImageCreate(gfx_image_t* img) {
    MtlImage* m = (MtlImage*)calloc(1, sizeof(MtlImage));
    if (!m) return false;
    const sc_gfx_image_desc* d = &img->desc;

    /* memimg 绑定：纹理来自 gpu env（IOSurface-backed MTLTexture） */
    if (d->memimg) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>)sc_gpu_memimg_native(d->memimg);
        if (!tex) {
            gfx_log("metal: memimg %u 无效", d->memimg);
            free(m);
            return false;
        }
        if ((int)tex.width != d->width || (int)tex.height != d->height) {
            gfx_log("metal: memimg 尺寸与 desc 不一致");
            free(m);
            return false;
        }
        m->tex = tex;
        img->backend = m;
        return true;
    }

    MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
    switch (d->kind) {
        case SC_GFX_IMAGEKIND_CUBE:  td.textureType = MTLTextureTypeCube; break;
        case SC_GFX_IMAGEKIND_3D:    td.textureType = MTLTextureType3D; break;
        case SC_GFX_IMAGEKIND_ARRAY: td.textureType = MTLTextureType2DArray; break;
        default:
            td.textureType = d->sample_count > 1 ? MTLTextureType2DMultisample
                                                 : MTLTextureType2D;
            break;
    }
    td.pixelFormat = toMtlFormat(d->format);
    if (td.pixelFormat == MTLPixelFormatInvalid) { free(m); return false; }
    td.width  = (NSUInteger)d->width;
    td.height = (NSUInteger)d->height;
    if (d->kind == SC_GFX_IMAGEKIND_3D) td.depth = (NSUInteger)d->slices;
    else if (d->kind == SC_GFX_IMAGEKIND_ARRAY) td.arrayLength = (NSUInteger)d->slices;
    td.mipmapLevelCount = (NSUInteger)d->mip_count;
    td.sampleCount = (NSUInteger)(d->sample_count > 1 ? d->sample_count : 1);
    if (d->render_target) {
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
    } else {
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
    }
    m->tex = [mtl.device newTextureWithDescriptor:td];
    if (!m->tex) { free(m); return false; }

    if (!d->render_target)
        imageUploadData(img, m->tex, &d->data);
    img->backend = m;
    return true;
}

static void mtlImageDestroy(gfx_image_t* img) {
    MtlImage* m = (MtlImage*)img->backend;
    if (!m) return;
    mtlDeferRelease(m->tex);
    m->tex = nil;
    free(m);
    img->backend = NULL;
}

static void mtlImageUpdate(gfx_image_t* img, const sc_gfx_image_data* data) {
    MtlImage* m = (MtlImage*)img->backend;
    if (!m || img->desc.render_target) return;
    imageUploadData(img, m->tex, data);
}

/* ---- sampler ----------------------------------------------- */

static bool mtlSamplerCreate(gfx_sampler_t* smp) {
    MtlSampler* m = (MtlSampler*)calloc(1, sizeof(MtlSampler));
    if (!m) return false;
    const sc_gfx_sampler_desc* d = &smp->desc;

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = d->min_filter == SC_GFX_FILTER_LINEAR ? MTLSamplerMinMagFilterLinear
                                                         : MTLSamplerMinMagFilterNearest;
    sd.magFilter = d->mag_filter == SC_GFX_FILTER_LINEAR ? MTLSamplerMinMagFilterLinear
                                                         : MTLSamplerMinMagFilterNearest;
    sd.mipFilter = d->mipmap_filter == SC_GFX_FILTER_LINEAR ? MTLSamplerMipFilterLinear
                                                            : MTLSamplerMipFilterNearest;
    sd.sAddressMode = toMtlWrap(d->wrap_u);
    sd.tAddressMode = toMtlWrap(d->wrap_v);
    sd.rAddressMode = toMtlWrap(d->wrap_w);
    sd.lodMinClamp = d->min_lod;
    sd.lodMaxClamp = d->max_lod > 0.0f ? d->max_lod : FLT_MAX;
    sd.maxAnisotropy = (NSUInteger)(d->max_anisotropy > 1 ? d->max_anisotropy : 1);
    switch (d->border_color) {
        case SC_GFX_BORDERCOLOR_OPAQUE_BLACK:
            sd.borderColor = MTLSamplerBorderColorOpaqueBlack; break;
        case SC_GFX_BORDERCOLOR_OPAQUE_WHITE:
            sd.borderColor = MTLSamplerBorderColorOpaqueWhite; break;
        default:
            sd.borderColor = MTLSamplerBorderColorTransparentBlack; break;
    }
    if (d->compare != SC_GFX_COMPARE_ALWAYS)
        sd.compareFunction = toMtlCompare(d->compare);
    m->smp = [mtl.device newSamplerStateWithDescriptor:sd];
    if (!m->smp) { free(m); return false; }
    smp->backend = m;
    return true;
}

static void mtlSamplerDestroy(gfx_sampler_t* smp) {
    MtlSampler* m = (MtlSampler*)smp->backend;
    if (!m) return;
    mtlDeferRelease(m->smp);
    m->smp = nil;
    free(m);
    smp->backend = NULL;
}

/* ---- shader ------------------------------------------------ */

static bool compileStage(const sc_gfx_shader_stage_desc* sd,
                         id<MTLLibrary> __strong* outLib,
                         id<MTLFunction> __strong* outFn) {
    if (!sd->code.ptr) return true;   /* 阶段缺省 = 不用 */
    NSString* src = [[NSString alloc] initWithBytes:sd->code.ptr
                                             length:sd->code.size
                                           encoding:NSUTF8StringEncoding];
    if (!src) { gfx_log("metal: 着色器源码非 UTF-8"); return false; }
    NSError* err = nil;
    MTLCompileOptions* opt = [[MTLCompileOptions alloc] init];
    id<MTLLibrary> lib = [mtl.device newLibraryWithSource:src options:opt error:&err];
    if (!lib) {
        gfx_log("metal: MSL 编译失败: %s",
                    err ? err.localizedDescription.UTF8String : "?");
        return false;
    }
    NSString* entry = sd->entry ? [NSString stringWithUTF8String:sd->entry] : @"main0";
    id<MTLFunction> fn = [lib newFunctionWithName:entry];
    if (!fn) {
        gfx_log("metal: 入口 %s 不存在", entry.UTF8String);
        return false;
    }
    *outLib = lib;
    *outFn = fn;
    return true;
}

static bool mtlShaderCreate(gfx_shader_t* shd, const sc_gfx_shader_desc* desc) {
    MtlShader* m = (MtlShader*)calloc(1, sizeof(MtlShader));
    if (!m) return false;
    bool ok = compileStage(&desc->vs, &m->vsLib, &m->vsFn)
           && compileStage(&desc->fs, &m->fsLib, &m->fsFn)
           && compileStage(&desc->cs, &m->csLib, &m->csFn);
    if (!ok) {
        m->vsLib = nil; m->fsLib = nil; m->csLib = nil;
        m->vsFn = nil; m->fsFn = nil; m->csFn = nil;
        free(m);
        return false;
    }
    shd->backend = m;
    return true;
}

static void mtlShaderDestroy(gfx_shader_t* shd) {
    MtlShader* m = (MtlShader*)shd->backend;
    if (!m) return;
    mtlDeferRelease(m->vsLib); mtlDeferRelease(m->fsLib); mtlDeferRelease(m->csLib);
    mtlDeferRelease(m->vsFn);  mtlDeferRelease(m->fsFn);  mtlDeferRelease(m->csFn);
    m->vsLib = nil; m->fsLib = nil; m->csLib = nil;
    m->vsFn = nil; m->fsFn = nil; m->csFn = nil;
    free(m);
    shd->backend = NULL;
}

/* ---- pipeline ---------------------------------------------- */

static bool mtlPipelineCreate(gfx_pipeline_t* pip) {
    MtlShader* shd = (MtlShader*)pip->shader->backend;
    if (!shd) return false;
    MtlPipeline* m = (MtlPipeline*)calloc(1, sizeof(MtlPipeline));
    if (!m) return false;
    const sc_gfx_pipeline_desc* d = &pip->desc;

    if (d->compute) {
        if (!shd->csFn) { free(m); return false; }
        NSError* err = nil;
        m->cps = [mtl.device newComputePipelineStateWithFunction:shd->csFn error:&err];
        if (!m->cps) {
            gfx_log("metal: 计算管线创建失败: %s",
                        err ? err.localizedDescription.UTF8String : "?");
            free(m);
            return false;
        }
        m->threadsPerGroup = MTLSizeMake(
            (NSUInteger)(d->threads_per_group[0] > 0 ? d->threads_per_group[0] : 1),
            (NSUInteger)(d->threads_per_group[1] > 0 ? d->threads_per_group[1] : 1),
            (NSUInteger)(d->threads_per_group[2] > 0 ? d->threads_per_group[2] : 1));
        pip->backend = m;
        return true;
    }

    MTLRenderPipelineDescriptor* rp = [[MTLRenderPipelineDescriptor alloc] init];
    rp.vertexFunction = shd->vsFn;
    rp.fragmentFunction = shd->fsFn;
    if (d->label) rp.label = [NSString stringWithUTF8String:d->label];

    /* 顶点布局：属性 location i；缓冲槽 MTL_VBUF_BASE + buffer_index */
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    bool bufUsed[SC_GFX_MAX_VERTEX_BUFFERS] = {0};
    for (int i = 0; i < SC_GFX_MAX_VERTEX_ATTRS; i++) {
        const sc_gfx_vertex_attr* a = &d->attrs[i];
        if (a->format == SC_GFX_VERTEXFORMAT_INVALID) continue;
        vd.attributes[i].format = toMtlVertexFormat(a->format);
        vd.attributes[i].offset = (NSUInteger)a->offset;
        vd.attributes[i].bufferIndex = (NSUInteger)(MTL_VBUF_BASE + a->buffer_index);
        bufUsed[a->buffer_index] = true;
    }
    bool hasAttrs = false;
    for (int i = 0; i < SC_GFX_MAX_VERTEX_BUFFERS; i++) {
        if (!bufUsed[i]) continue;
        hasAttrs = true;
        NSUInteger li = (NSUInteger)(MTL_VBUF_BASE + i);
        vd.layouts[li].stride = (NSUInteger)d->buffers[i].stride;
        vd.layouts[li].stepFunction = d->buffers[i].step_per_instance
            ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        vd.layouts[li].stepRate = 1;
    }
    if (hasAttrs) rp.vertexDescriptor = vd;

    for (int i = 0; i < d->color_count; i++) {
        const sc_gfx_color_target_state* c = &d->colors[i];
        rp.colorAttachments[i].pixelFormat = toMtlFormat(c->format);
        if (c->write_mask)
            rp.colorAttachments[i].writeMask = (MTLColorWriteMask)c->write_mask;
        if (c->blend.enabled) {
            rp.colorAttachments[i].blendingEnabled = YES;
            rp.colorAttachments[i].sourceRGBBlendFactor = toMtlBlendFactor(c->blend.src_factor_rgb);
            rp.colorAttachments[i].destinationRGBBlendFactor = toMtlBlendFactor(c->blend.dst_factor_rgb);
            rp.colorAttachments[i].rgbBlendOperation = toMtlBlendOp(c->blend.op_rgb);
            rp.colorAttachments[i].sourceAlphaBlendFactor = toMtlBlendFactor(c->blend.src_factor_alpha);
            rp.colorAttachments[i].destinationAlphaBlendFactor = toMtlBlendFactor(c->blend.dst_factor_alpha);
            rp.colorAttachments[i].alphaBlendOperation = toMtlBlendOp(c->blend.op_alpha);
        }
    }

    bool hasDepth = d->depth.format != SC_GPU_PIXELFORMAT_NONE;
    if (hasDepth) {
        rp.depthAttachmentPixelFormat = toMtlFormat(d->depth.format);
        if (d->depth.format == SC_GPU_PIXELFORMAT_DEPTH_STENCIL)
            rp.stencilAttachmentPixelFormat = toMtlFormat(d->depth.format);
    }
    rp.rasterSampleCount = (NSUInteger)(d->sample_count > 1 ? d->sample_count : 1);
    rp.alphaToCoverageEnabled = d->alpha_to_coverage ? YES : NO;

    NSError* err = nil;
    m->rps = [mtl.device newRenderPipelineStateWithDescriptor:rp error:&err];
    if (!m->rps) {
        gfx_log("metal: 渲染管线创建失败: %s",
                    err ? err.localizedDescription.UTF8String : "?");
        free(m);
        return false;
    }

    /* 深度/模板状态 */
    if (hasDepth) {
        MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
        dsd.depthCompareFunction = toMtlCompare(d->depth.compare);
        dsd.depthWriteEnabled = d->depth.write_enabled ? YES : NO;
        if (d->stencil.enabled) {
            MTLStencilDescriptor* front = [[MTLStencilDescriptor alloc] init];
            front.stencilCompareFunction = toMtlCompare(d->stencil.front.compare);
            front.stencilFailureOperation = toMtlStencilOp(d->stencil.front.fail_op);
            front.depthFailureOperation = toMtlStencilOp(d->stencil.front.depth_fail_op);
            front.depthStencilPassOperation = toMtlStencilOp(d->stencil.front.pass_op);
            front.readMask = d->stencil.read_mask;
            front.writeMask = d->stencil.write_mask;
            MTLStencilDescriptor* back = [[MTLStencilDescriptor alloc] init];
            back.stencilCompareFunction = toMtlCompare(d->stencil.back.compare);
            back.stencilFailureOperation = toMtlStencilOp(d->stencil.back.fail_op);
            back.depthFailureOperation = toMtlStencilOp(d->stencil.back.depth_fail_op);
            back.depthStencilPassOperation = toMtlStencilOp(d->stencil.back.pass_op);
            back.readMask = d->stencil.read_mask;
            back.writeMask = d->stencil.write_mask;
            dsd.frontFaceStencil = front;
            dsd.backFaceStencil = back;
            m->hasStencil = true;
            m->stencilRef = d->stencil.ref;
        }
        m->dss = [mtl.device newDepthStencilStateWithDescriptor:dsd];
    }

    switch (d->primitive) {
        case SC_GFX_PRIMITIVE_POINTS:         m->primitive = MTLPrimitiveTypePoint; break;
        case SC_GFX_PRIMITIVE_LINES:          m->primitive = MTLPrimitiveTypeLine; break;
        case SC_GFX_PRIMITIVE_LINE_STRIP:     m->primitive = MTLPrimitiveTypeLineStrip; break;
        case SC_GFX_PRIMITIVE_TRIANGLE_STRIP: m->primitive = MTLPrimitiveTypeTriangleStrip; break;
        default:                              m->primitive = MTLPrimitiveTypeTriangle; break;
    }
    switch (d->cull) {
        case SC_GFX_CULL_FRONT: m->cull = MTLCullModeFront; break;
        case SC_GFX_CULL_BACK:  m->cull = MTLCullModeBack; break;
        default:                m->cull = MTLCullModeNone; break;
    }
    m->winding = d->winding == SC_GFX_WINDING_CW ? MTLWindingClockwise
                                                 : MTLWindingCounterClockwise;
    if (d->index_type == SC_GFX_INDEXTYPE_UINT32) {
        m->indexType = MTLIndexTypeUInt32; m->indexSize = 4;
    } else {
        m->indexType = MTLIndexTypeUInt16; m->indexSize = 2;
    }
    m->depthBias = d->depth.bias;
    m->depthBiasSlope = d->depth.bias_slope_scale;
    m->depthBiasClamp = d->depth.bias_clamp;
    memcpy(m->blendColor, d->blend_color, sizeof(m->blendColor));

    pip->backend = m;
    return true;
}

static void mtlPipelineDestroy(gfx_pipeline_t* pip) {
    MtlPipeline* m = (MtlPipeline*)pip->backend;
    if (!m) return;
    mtlDeferRelease(m->rps); mtlDeferRelease(m->cps); mtlDeferRelease(m->dss);
    m->rps = nil; m->cps = nil; m->dss = nil;
    if (mtl.curPip == m) mtl.curPip = NULL;
    free(m);
    pip->backend = NULL;
}

/* ---- 帧 ---------------------------------------------------- */

/* 帧首个 pass：等在飞帧、开命令缓冲、清延迟释放队列、重置 uniform 环 */
static void ensureCmd(void) {
    if (mtl.cmd) return;
    dispatch_semaphore_wait(mtl.sem, DISPATCH_TIME_FOREVER);
    [mtl.releaseQueue[mtlFrameSlot()] removeAllObjects];  /* GPU 已用完，真正释放 */
    mtl.cmd = [mtl.queue commandBuffer];
    mtl.ubPos = 0;
    mtl.presentCount = 0;
}

static void mtlBeginPass(const sc_gfx_pass* pass, gfx_image_t* colors[],
                         int color_count, gfx_image_t* resolves[],
                         gfx_image_t* depth) {
    ensureCmd();

    if (pass->compute) {
        mtl.cenc = [mtl.cmd computeCommandEncoder];
        return;
    }

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];

    if (color_count == 0) {
        /* 交换链 pass：渲染目标经 gpu env 交付（含 resize 竞态校验） */
        sc_gpu_frame f;
        if (!sc_gpu_frame_acquire(&f)) {
            gfx_log("metal: frame_acquire 失败");
            return;
        }
        id<MTLTexture> target = (__bridge id<MTLTexture>)f.color;
        id<MTLTexture> msaa = (__bridge id<MTLTexture>)f.msaa_color;
        id<MTLTexture> depthTex = (__bridge id<MTLTexture>)f.depth;
        mtl.curPassWidth = f.width;
        mtl.curPassHeight = f.height;

        const sc_gfx_color_attachment_action* ca = &pass->action.colors[0];
        if (msaa) {
            rpd.colorAttachments[0].texture = msaa;
            rpd.colorAttachments[0].resolveTexture = target;
            rpd.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        } else {
            rpd.colorAttachments[0].texture = target;
            rpd.colorAttachments[0].storeAction = toMtlStore(ca->store);
        }
        rpd.colorAttachments[0].loadAction = toMtlLoad(ca->load);
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(
            ca->clear[0], ca->clear[1], ca->clear[2], ca->clear[3]);

        if (depthTex) {
            const sc_gfx_depth_attachment_action* da = &pass->action.depth;
            rpd.depthAttachment.texture = depthTex;
            rpd.depthAttachment.loadAction = toMtlLoad(da->load);
            rpd.depthAttachment.storeAction = toMtlStore(da->store);
            rpd.depthAttachment.clearDepth = da->clear_depth;
            if (f.depth_format == SC_GPU_PIXELFORMAT_DEPTH_STENCIL) {
                rpd.stencilAttachment.texture = depthTex;
                rpd.stencilAttachment.loadAction = toMtlLoad(da->load);
                rpd.stencilAttachment.storeAction = toMtlStore(da->store);
                rpd.stencilAttachment.clearStencil = da->clear_stencil;
            }
        }

        /* 记入本帧呈现列表（去重；MEMORY surface 无 drawable） */
        id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)f.drawable;
        if (drawable) {
            bool listed = false;
            for (int i = 0; i < mtl.presentCount; i++)
                if (mtl.present[i] == drawable) { listed = true; break; }
            if (!listed && mtl.presentCount < MTL_MAX_PRESENT)
                mtl.present[mtl.presentCount++] = drawable;
        }
    } else {
        /* 离屏 pass */
        mtl.curPassWidth = colors[0]->desc.width >> pass->colors[0].mip;
        mtl.curPassHeight = colors[0]->desc.height >> pass->colors[0].mip;
        if (mtl.curPassWidth < 1) mtl.curPassWidth = 1;
        if (mtl.curPassHeight < 1) mtl.curPassHeight = 1;

        for (int i = 0; i < color_count; i++) {
            MtlImage* img = (MtlImage*)colors[i]->backend;
            const sc_gfx_color_attachment_action* ca = &pass->action.colors[i];
            rpd.colorAttachments[i].texture = img->tex;
            rpd.colorAttachments[i].level = (NSUInteger)pass->colors[i].mip;
            rpd.colorAttachments[i].slice = (NSUInteger)pass->colors[i].slice;
            rpd.colorAttachments[i].loadAction = toMtlLoad(ca->load);
            rpd.colorAttachments[i].storeAction = toMtlStore(ca->store);
            rpd.colorAttachments[i].clearColor = MTLClearColorMake(
                ca->clear[0], ca->clear[1], ca->clear[2], ca->clear[3]);
            if (resolves[i]) {
                MtlImage* rimg = (MtlImage*)resolves[i]->backend;
                rpd.colorAttachments[i].resolveTexture = rimg->tex;
                rpd.colorAttachments[i].resolveLevel = (NSUInteger)pass->resolves[i].mip;
                rpd.colorAttachments[i].resolveSlice = (NSUInteger)pass->resolves[i].slice;
            }
        }
        if (depth) {
            MtlImage* img = (MtlImage*)depth->backend;
            const sc_gfx_depth_attachment_action* da = &pass->action.depth;
            rpd.depthAttachment.texture = img->tex;
            rpd.depthAttachment.loadAction = toMtlLoad(da->load);
            rpd.depthAttachment.storeAction = toMtlStore(da->store);
            rpd.depthAttachment.clearDepth = da->clear_depth;
            if (depth->desc.format == SC_GPU_PIXELFORMAT_DEPTH_STENCIL) {
                rpd.stencilAttachment.texture = img->tex;
                rpd.stencilAttachment.loadAction = toMtlLoad(da->load);
                rpd.stencilAttachment.storeAction = toMtlStore(da->store);
                rpd.stencilAttachment.clearStencil = da->clear_stencil;
            }
        }
    }

    mtl.renc = [mtl.cmd renderCommandEncoderWithDescriptor:rpd];
}

/* 钳制到 pass 边界（Metal 校验层要求 scissor/viewport 不出界） */
static void clampRect(int* x, int* y, int* w, int* h, bool topLeft) {
    if (!topLeft) *y = mtl.curPassHeight - (*y + *h);   /* 翻转到左上原点 */
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > mtl.curPassWidth)  *w = mtl.curPassWidth - *x;
    if (*y + *h > mtl.curPassHeight) *h = mtl.curPassHeight - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

static void mtlApplyViewport(int x, int y, int w, int h, bool topLeft) {
    if (!mtl.renc) return;
    clampRect(&x, &y, &w, &h, topLeft);
    [mtl.renc setViewport:(MTLViewport){ (double)x, (double)y,
                                         (double)w, (double)h, 0.0, 1.0 }];
}

static void mtlApplyScissor(int x, int y, int w, int h, bool topLeft) {
    if (!mtl.renc) return;
    clampRect(&x, &y, &w, &h, topLeft);
    [mtl.renc setScissorRect:(MTLScissorRect){ (NSUInteger)x, (NSUInteger)y,
                                               (NSUInteger)w, (NSUInteger)h }];
}

static void mtlApplyPipeline(gfx_pipeline_t* pip) {
    MtlPipeline* m = (MtlPipeline*)pip->backend;
    if (!m) return;
    mtl.curPip = m;
    if (m->cps) {
        if (mtl.cenc) [mtl.cenc setComputePipelineState:m->cps];
        return;
    }
    if (!mtl.renc) return;
    [mtl.renc setRenderPipelineState:m->rps];
    if (m->dss) [mtl.renc setDepthStencilState:m->dss];
    [mtl.renc setCullMode:m->cull];
    [mtl.renc setFrontFacingWinding:m->winding];
    if (m->depthBias != 0.0f || m->depthBiasSlope != 0.0f)
        [mtl.renc setDepthBias:m->depthBias
                    slopeScale:m->depthBiasSlope
                         clamp:m->depthBiasClamp];
    if (m->hasStencil)
        [mtl.renc setStencilReferenceValue:m->stencilRef];
    [mtl.renc setBlendColorRed:m->blendColor[0] green:m->blendColor[1]
                          blue:m->blendColor[2] alpha:m->blendColor[3]];
}

static void mtlApplyBindings(gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                             gfx_buffer_t* vbufs[], gfx_buffer_t* ibuf,
                             gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                             gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                             gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]) {
    (void)pip;

    /* 计算 pass */
    if (mtl.cenc) {
        for (int i = 0; i < SC_GFX_MAX_IMAGES; i++)
            if (imgs[2][i])
                [mtl.cenc setTexture:((MtlImage*)imgs[2][i]->backend)->tex
                             atIndex:(NSUInteger)i];
        for (int i = 0; i < SC_GFX_MAX_SAMPLERS; i++)
            if (smps[2][i])
                [mtl.cenc setSamplerState:((MtlSampler*)smps[2][i]->backend)->smp
                                  atIndex:(NSUInteger)i];
        for (int i = 0; i < SC_GFX_MAX_STORAGE_BUFFERS; i++)
            if (sbufs[2][i])
                [mtl.cenc setBuffer:mtlActiveBuf(sbufs[2][i])
                             offset:0 atIndex:(NSUInteger)i];
        return;
    }
    if (!mtl.renc) return;

    /* 顶点缓冲（活动副本） */
    for (int i = 0; i < SC_GFX_MAX_VERTEX_BUFFERS; i++) {
        if (!vbufs[i]) continue;
        [mtl.renc setVertexBuffer:mtlActiveBuf(vbufs[i])
                           offset:(NSUInteger)bnd->vertex_buffer_offsets[i]
                          atIndex:(NSUInteger)(MTL_VBUF_BASE + i)];
    }
    /* 索引缓冲存起来（draw 时用） */
    if (ibuf) {
        mtl.curIndexBuf = mtlActiveBuf(ibuf);
        mtl.curIndexOffset = bnd->index_buffer_offset;
    } else {
        mtl.curIndexBuf = nil;
        mtl.curIndexOffset = 0;
    }
    /* vs/fs 纹理、采样器、storage */
    for (int i = 0; i < SC_GFX_MAX_IMAGES; i++) {
        if (imgs[0][i])
            [mtl.renc setVertexTexture:((MtlImage*)imgs[0][i]->backend)->tex
                               atIndex:(NSUInteger)i];
        if (imgs[1][i])
            [mtl.renc setFragmentTexture:((MtlImage*)imgs[1][i]->backend)->tex
                                 atIndex:(NSUInteger)i];
    }
    for (int i = 0; i < SC_GFX_MAX_SAMPLERS; i++) {
        if (smps[0][i])
            [mtl.renc setVertexSamplerState:((MtlSampler*)smps[0][i]->backend)->smp
                                    atIndex:(NSUInteger)i];
        if (smps[1][i])
            [mtl.renc setFragmentSamplerState:((MtlSampler*)smps[1][i]->backend)->smp
                                      atIndex:(NSUInteger)i];
    }
    for (int i = 0; i < SC_GFX_MAX_STORAGE_BUFFERS; i++) {
        if (sbufs[0][i])
            [mtl.renc setVertexBuffer:mtlActiveBuf(sbufs[0][i])
                               offset:0 atIndex:(NSUInteger)i];
        if (sbufs[1][i])
            [mtl.renc setFragmentBuffer:mtlActiveBuf(sbufs[1][i])
                                 offset:0 atIndex:(NSUInteger)i];
    }
}

static void mtlApplyUniforms(int stage, int slot, const void* data, size_t size) {
    /* 当前帧槽位的环缓冲追加（256 对齐，Metal 要求） */
    id<MTLBuffer> ring = mtl.ubRing[mtlFrameSlot()];
    int pos = (mtl.ubPos + 255) & ~255;
    if ((size_t)pos + size > MTL_UB_RING_SIZE) {
        gfx_log("metal: uniform 环缓冲溢出");
        return;
    }
    memcpy((uint8_t*)ring.contents + pos, data, size);
    mtl.ubPos = pos + (int)size;

    if (stage == SC_GFX_STAGE_COMPUTE) {
        if (mtl.cenc)
            [mtl.cenc setBuffer:ring offset:(NSUInteger)pos atIndex:(NSUInteger)slot];
        return;
    }
    if (!mtl.renc) return;
    if (stage == SC_GFX_STAGE_VERTEX)
        [mtl.renc setVertexBuffer:ring offset:(NSUInteger)pos atIndex:(NSUInteger)slot];
    else
        [mtl.renc setFragmentBuffer:ring offset:(NSUInteger)pos atIndex:(NSUInteger)slot];
}

static void mtlDraw(int base, int count, int instances) {
    if (!mtl.renc || !mtl.curPip) return;
    MtlPipeline* p = mtl.curPip;
    if (mtl.curIndexBuf) {
        NSUInteger off = (NSUInteger)(mtl.curIndexOffset + base * p->indexSize);
        [mtl.renc drawIndexedPrimitives:p->primitive
                             indexCount:(NSUInteger)count
                              indexType:p->indexType
                            indexBuffer:mtl.curIndexBuf
                      indexBufferOffset:off
                          instanceCount:(NSUInteger)instances];
    } else {
        [mtl.renc drawPrimitives:p->primitive
                     vertexStart:(NSUInteger)base
                     vertexCount:(NSUInteger)count
                   instanceCount:(NSUInteger)instances];
    }
}

static void mtlDispatch(int gx, int gy, int gz) {
    if (!mtl.cenc || !mtl.curPip) return;
    [mtl.cenc dispatchThreadgroups:MTLSizeMake((NSUInteger)gx, (NSUInteger)gy, (NSUInteger)gz)
             threadsPerThreadgroup:mtl.curPip->threadsPerGroup];
}

static void mtlEndPass(void) {
    if (mtl.renc) { [mtl.renc endEncoding]; mtl.renc = nil; }
    if (mtl.cenc) { [mtl.cenc endEncoding]; mtl.cenc = nil; }
    mtl.curPip = NULL;
    mtl.curIndexBuf = nil;
}

static void mtlCommit(void) {
    if (!mtl.cmd) {
        sc_gpu_frame_end();
        return;
    }
    /* 呈现本帧触达的所有 drawable（挂命令缓冲 = 最佳呈现节拍） */
    for (int i = 0; i < mtl.presentCount; i++) {
        [mtl.cmd presentDrawable:mtl.present[i]];
        mtl.present[i] = nil;
    }
    mtl.presentCount = 0;

    dispatch_semaphore_t sem = mtl.sem;
    [mtl.cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        (void)cb;
        dispatch_semaphore_signal(sem);
    }];
    [mtl.cmd commit];
    mtl.lastCmd = mtl.cmd;
    mtl.cmd = nil;
    mtl.frameIndex++;

    sc_gpu_frame_end();   /* env 释放 drawable 引用（命令缓冲仍持有） */
}

/* ---- 能力查询 ----------------------------------------------- */

static void mtlQueryPixelformat(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out) {
    switch (fmt) {
        case SC_GPU_PIXELFORMAT_DEPTH:
        case SC_GPU_PIXELFORMAT_DEPTH_STENCIL:
            out->sample = 1; out->render = 1; out->msaa = 1; out->depth = 1;
            break;
        case SC_GPU_PIXELFORMAT_NONE:
            break;
        default:
            /* macOS Metal：本模块暴露的颜色格式全部可采样/过滤/渲染/混合/MSAA */
            out->sample = 1; out->filter = 1; out->render = 1;
            out->blend = 1; out->msaa = 1;
            break;
    }
}

/* ---- vtable ------------------------------------------------ */

static const gfx_backend_api mtlApi = {
    .name = "metal",
    .init = mtlInit,
    .shutdown = mtlShutdown,
    .finish = mtlGfxFinish,
    .buffer_create = mtlBufferCreate,
    .buffer_destroy = mtlBufferDestroy,
    .buffer_update = mtlBufferUpdate,
    .image_create = mtlImageCreate,
    .image_destroy = mtlImageDestroy,
    .image_update = mtlImageUpdate,
    .sampler_create = mtlSamplerCreate,
    .sampler_destroy = mtlSamplerDestroy,
    .shader_create = mtlShaderCreate,
    .shader_destroy = mtlShaderDestroy,
    .pipeline_create = mtlPipelineCreate,
    .pipeline_destroy = mtlPipelineDestroy,
    .begin_pass = mtlBeginPass,
    .apply_viewport = mtlApplyViewport,
    .apply_scissor = mtlApplyScissor,
    .apply_pipeline = mtlApplyPipeline,
    .apply_bindings = mtlApplyBindings,
    .apply_uniforms = mtlApplyUniforms,
    .draw = mtlDraw,
    .dispatch = mtlDispatch,
    .end_pass = mtlEndPass,
    .commit = mtlCommit,
    .query_pixelformat = mtlQueryPixelformat,
};

const gfx_backend_api* gfx_backend_metal(void) { return &mtlApi; }

#endif /* SC_GPU_METAL */
