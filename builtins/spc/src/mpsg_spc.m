/* ============================================================
 * mpsg_spc.m —— spc graph 面：MPSGraph 张量算子（macOS）
 * ============================================================
 * 苹果优化的算子图引擎（GPU，内部用 simdgroup matmul 等），
 * 与 builtins/ts 的 CPU 算子一一对应，是 nn GPU 加速的着力点。
 * 一期：matmul（2D，float32）。张量数据经 NSData no-copy 包装。
 * ============================================================ */

#include "../../platform.h"   /* 平台判定宏（尊重交叉目标 SC_TARGET_*）；须先于守卫 */
#if P_DARWIN

#include "internal.h"
#include <string.h>

#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <MetalPerformanceShadersGraph/MetalPerformanceShadersGraph.h>

extern void* sc_gpu_device(void);

int spc_mpsg_mm(sc_tensor* a, sc_tensor* b, sc_tensor* out) {
    id<MTLDevice> dev = (__bridge id<MTLDevice>)sc_gpu_device();
    if (!dev) return 0;

    @autoreleasepool {
        MPSGraphDevice* gdev = [MPSGraphDevice deviceWithMTLDevice:dev];
        MPSGraph* g = [[MPSGraph alloc] init];

        NSArray<NSNumber*>* shA = @[ @(a->shape[0]), @(a->shape[1]) ];
        NSArray<NSNumber*>* shB = @[ @(b->shape[0]), @(b->shape[1]) ];
        MPSGraphTensor* ta = [g placeholderWithShape:shA
                                            dataType:MPSDataTypeFloat32
                                                name:@"a"];
        MPSGraphTensor* tb = [g placeholderWithShape:shB
                                            dataType:MPSDataTypeFloat32
                                                name:@"b"];
        MPSGraphTensor* tc = [g matrixMultiplicationWithPrimaryTensor:ta
                                                      secondaryTensor:tb
                                                                 name:@"mm"];

        NSData* da = [NSData dataWithBytesNoCopy:spc_tsdata(a)
                                          length:(NSUInteger)(a->numel * 4)
                                    freeWhenDone:NO];
        NSData* db = [NSData dataWithBytesNoCopy:spc_tsdata(b)
                                          length:(NSUInteger)(b->numel * 4)
                                    freeWhenDone:NO];
        MPSGraphTensorData* xa = [[MPSGraphTensorData alloc] initWithDevice:gdev
                                                                       data:da
                                                                      shape:shA
                                                                   dataType:MPSDataTypeFloat32];
        MPSGraphTensorData* xb = [[MPSGraphTensorData alloc] initWithDevice:gdev
                                                                       data:db
                                                                      shape:shB
                                                                   dataType:MPSDataTypeFloat32];

        NSDictionary* results = [g runWithFeeds:@{ ta: xa, tb: xb }
                                  targetTensors:@[ tc ]
                               targetOperations:nil];
        MPSGraphTensorData* xc = results[tc];
        if (!xc) { spc_log("mpsg: matmul 执行失败"); return 0; }
        [[xc mpsndarray] readBytes:spc_tsdata(out) strideBytes:nil];
    }
    return 1;
}

#endif /* P_DARWIN */
