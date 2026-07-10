/* ============================================================
 * metal_spc.m —— spc kernel 面：Metal compute（macOS）
 * ============================================================
 * · device 借自 gpu env（sc_gpu_device()），命令队列独立于 gfx
 * · 内核 = scc .sg comp 产物（MSL 文本）编译 + 计算管线
 * · 槽位对位：spirv-cross 会重排 MSL [[buffer(N)]]，与反射清单的
 *   binding 不一致——经管线反射按「块实例名」对位（清单 name ↔
 *   MSL 参数名一致，均为 @def 块名）
 * · dispatch：精确全局网格（dispatchThreads，Apple GPU 均支持
 *   non-uniform threadgroups）；线程组尺寸 = 清单 local_size
 * ============================================================ */

#include "../../platform.h"   /* 平台判定宏（尊重交叉目标 SC_TARGET_*）；须先于守卫 */
#if P_DARWIN

#include "internal.h"
#include <string.h>

#import <Metal/Metal.h>

extern void* sc_gpu_device(void);

typedef struct MtlSpcBuffer {
    id<MTLBuffer> buf;
} MtlSpcBuffer;

typedef struct MtlSpcKernel {
    id<MTLComputePipelineState> pso;
    int mslIndex[SC_SPC_MAX_BINDINGS];   /* res[i] → MSL buffer 槽；-1 未用 */
} MtlSpcKernel;

static struct {
    id<MTLDevice>        device;
    id<MTLCommandQueue>  queue;
    id<MTLCommandBuffer> lastCmd;
} M;

/* ---- 生命周期 ---------------------------------------------- */

bool spc_mtl_init(void) {
    memset((void*)&M, 0, sizeof(M));
    M.device = (__bridge id<MTLDevice>)sc_gpu_device();
    if (!M.device) {
        spc_log("metal: gpu env 未交付设备（先 sc_gpu_init）");
        return false;
    }
    M.queue = [M.device newCommandQueue];
    return M.queue != nil;
}

void spc_mtl_shutdown(void) {
    if (M.lastCmd) [M.lastCmd waitUntilCompleted];
    M.lastCmd = nil;
    M.queue = nil;
    M.device = nil;
    memset((void*)&M, 0, sizeof(M));
}

void spc_mtl_finish(void) {
    if (M.lastCmd) {
        [M.lastCmd waitUntilCompleted];
        M.lastCmd = nil;
    }
}

/* ---- buffer ------------------------------------------------ */

bool spc_mtl_buffer_create(spc_buffer_t* b, const void* data, uint64_t size) {
    MtlSpcBuffer* m = (MtlSpcBuffer*)calloc(1, sizeof(MtlSpcBuffer));
    if (!m) return false;
    m->buf = data
        ? [M.device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared]
        : [M.device newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (!m->buf) { free(m); return false; }
    b->backend = m;
    return true;
}

void spc_mtl_buffer_destroy(spc_buffer_t* b) {
    MtlSpcBuffer* m = (MtlSpcBuffer*)b->backend;
    if (!m) return;
    m->buf = nil;
    free(m);
    b->backend = NULL;
}

bool spc_mtl_buffer_read(spc_buffer_t* b, void* dst, uint64_t size, uint64_t off) {
    MtlSpcBuffer* m = (MtlSpcBuffer*)b->backend;
    if (!m) return false;
    spc_mtl_finish();   /* 读回前确保 GPU 写入完成 */
    memcpy(dst, (const uint8_t*)m->buf.contents + off, size);
    return true;
}

bool spc_mtl_buffer_write(spc_buffer_t* b, const void* src, uint64_t size, uint64_t off) {
    MtlSpcBuffer* m = (MtlSpcBuffer*)b->backend;
    if (!m) return false;
    memcpy((uint8_t*)m->buf.contents + off, src, size);
    return true;
}

/* ---- kernel ------------------------------------------------ */

bool spc_mtl_kernel_create(spc_kernel_t* k, const sc_spc_kernel_desc* desc) {
    MtlSpcKernel* m = (MtlSpcKernel*)calloc(1, sizeof(MtlSpcKernel));
    if (!m) return false;
    for (int i = 0; i < SC_SPC_MAX_BINDINGS; i++) m->mslIndex[i] = -1;

    NSString* src = [[NSString alloc] initWithBytes:desc->code.ptr
                                             length:desc->code.size
                                           encoding:NSUTF8StringEncoding];
    if (!src) { spc_log("metal: 内核源码非 UTF-8"); free(m); return false; }
    NSError* err = nil;
    id<MTLLibrary> lib = [M.device newLibraryWithSource:src
                                                options:[[MTLCompileOptions alloc] init]
                                                  error:&err];
    if (!lib) {
        spc_log("metal: MSL 编译失败: %s",
                    err ? err.localizedDescription.UTF8String : "?");
        free(m);
        return false;
    }
    NSString* entry = desc->entry ? [NSString stringWithUTF8String:desc->entry] : @"main0";
    id<MTLFunction> fn = [lib newFunctionWithName:entry];
    if (!fn) {
        spc_log("metal: 入口 %s 不存在", entry.UTF8String);
        free(m);
        return false;
    }

    /* 带反射建管线：拿参数名 → MSL 槽位 */
    MTLComputePipelineDescriptor* pd = [[MTLComputePipelineDescriptor alloc] init];
    pd.computeFunction = fn;
    MTLComputePipelineReflection* refl = nil;
    m->pso = [M.device newComputePipelineStateWithDescriptor:pd
                                                     options:MTLPipelineOptionBindingInfo
                                                  reflection:&refl
                                                       error:&err];
    if (!m->pso) {
        spc_log("metal: 计算管线创建失败: %s",
                    err ? err.localizedDescription.UTF8String : "?");
        free(m);
        return false;
    }
    for (int i = 0; i < k->res_count; i++) {
        for (id<MTLBinding> arg in refl.bindings) {
            if (arg.type == MTLBindingTypeBuffer &&
                strcmp(arg.name.UTF8String, k->res[i].name) == 0) {
                m->mslIndex[i] = (int)arg.index;
                break;
            }
        }
        if (m->mslIndex[i] < 0)
            spc_log("metal: 内核参数 %s 未在管线反射中出现（可能被优化掉）",
                        k->res[i].name);
    }
    k->backend = m;
    return true;
}

void spc_mtl_kernel_destroy(spc_kernel_t* k) {
    MtlSpcKernel* m = (MtlSpcKernel*)k->backend;
    if (!m) return;
    m->pso = nil;
    free(m);
    k->backend = NULL;
}

bool spc_mtl_dispatch(spc_kernel_t* k, int gx, int gy, int gz,
                          const sc_spc_bindings* bnd,
                          spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS]) {
    MtlSpcKernel* m = (MtlSpcKernel*)k->backend;
    if (!m) return false;

    id<MTLCommandBuffer> cmd = [M.queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:m->pso];

    for (int i = 0; i < k->res_count; i++) {
        if (m->mslIndex[i] < 0) continue;
        const spc_kernel_res* r = &k->res[i];
        if (r->storage) {
            [enc setBuffer:((MtlSpcBuffer*)bufs[r->binding]->backend)->buf
                    offset:0
                   atIndex:(NSUInteger)m->mslIndex[i]];
        } else {
            [enc setBytes:bnd->uniforms[r->binding].ptr
                   length:(NSUInteger)bnd->uniforms[r->binding].size
                  atIndex:(NSUInteger)m->mslIndex[i]];
        }
    }

    /* 线程组尺寸：清单 local_size，钳到管线上限 */
    NSUInteger maxT = m->pso.maxTotalThreadsPerThreadgroup;
    NSUInteger lx = (NSUInteger)k->local[0], ly = (NSUInteger)k->local[1],
               lz = (NSUInteger)k->local[2];
    while (lx * ly * lz > maxT && lx > 1) lx /= 2;
    [enc dispatchThreads:MTLSizeMake((NSUInteger)gx, (NSUInteger)gy, (NSUInteger)gz)
        threadsPerThreadgroup:MTLSizeMake(lx, ly, lz)];
    [enc endEncoding];
    [cmd commit];
    M.lastCmd = cmd;
    return true;
}

#endif /* P_DARWIN */
