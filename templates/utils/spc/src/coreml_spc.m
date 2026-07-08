/* ============================================================
 * coreml_spc.m —— spc model 面：CoreML 整图推理（macOS，ANE 路径）
 * ============================================================
 * ANE（Apple 推理芯片）不可直接编程，CoreML 是唯一公开路径：
 *   · 加载 coremlcompiler 编译好的 .mlmodelc，computeUnits 指定
 *     调度倾向（SC_SPC_UNITS_CPU_ANE = 倾向推理芯片）
 *   · 输入：MLMultiArray 包装 sc_tensor 内存（no-copy）
 *   · ane_ratio：MLComputePlan（macOS 14.4+）逐算子查询实际调度
 *     设备，统计 ANE 占比——程序化确证，不靠猜
 * ============================================================ */

#if defined(__APPLE__)

#include "internal.h"
#include <string.h>

#import <CoreML/CoreML.h>

typedef struct CoreMlModel {
    MLModel*  model;
    NSURL*    url;
    NSString* inName;
    NSString* outName;
    int       units;
} CoreMlModel;

static MLModelConfiguration* configFor(int units) {
    MLModelConfiguration* cfg = [[MLModelConfiguration alloc] init];
    switch (units) {
        case SC_SPC_UNITS_CPU_ONLY: cfg.computeUnits = MLComputeUnitsCPUOnly; break;
        case SC_SPC_UNITS_CPU_GPU:  cfg.computeUnits = MLComputeUnitsCPUAndGPU; break;
        case SC_SPC_UNITS_CPU_ANE:  cfg.computeUnits = MLComputeUnitsCPUAndNeuralEngine; break;
        default:                    cfg.computeUnits = MLComputeUnitsAll; break;
    }
    return cfg;
}

bool _sc_spc_coreml_load(_sc_spc_model_t* m, const char* path, int units) {
    CoreMlModel* c = (CoreMlModel*)calloc(1, sizeof(CoreMlModel));
    if (!c) return false;

    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    NSError* err = nil;
    MLModel* model = [MLModel modelWithContentsOfURL:url
                                       configuration:configFor(units)
                                               error:&err];
    if (!model) {
        _sc_spc_log("coreml: 模型加载失败: %s",
                    err ? err.localizedDescription.UTF8String : "?");
        free(c);
        return false;
    }
    c->model = model;
    c->url = url;
    c->units = units;
    c->inName  = model.modelDescription.inputDescriptionsByName.allKeys.firstObject;
    c->outName = model.modelDescription.outputDescriptionsByName.allKeys.firstObject;
    if (!c->inName || !c->outName) {
        _sc_spc_log("coreml: 模型无输入/输出描述");
        c->model = nil;
        free(c);
        return false;
    }
    m->backend = c;
    return true;
}

void _sc_spc_coreml_destroy(_sc_spc_model_t* m) {
    CoreMlModel* c = (CoreMlModel*)m->backend;
    if (!c) return;
    c->model = nil; c->url = nil; c->inName = nil; c->outName = nil;
    free(c);
    m->backend = NULL;
}

bool _sc_spc_coreml_run1(_sc_spc_model_t* m, sc_tensor* in, sc_tensor* out) {
    CoreMlModel* c = (CoreMlModel*)m->backend;
    if (!c) return false;

    @autoreleasepool {
        NSMutableArray<NSNumber*>* shape = [NSMutableArray array];
        NSMutableArray<NSNumber*>* strides = [NSMutableArray array];
        for (int d = 0; d < in->ndim; d++) {
            [shape addObject:@(in->shape[d])];
            [strides addObject:@(in->strides[d])];
        }
        NSError* err = nil;
        MLMultiArray* arr = [[MLMultiArray alloc]
            initWithDataPointer:_sc_spc_tsdata(in)
                          shape:shape
                       dataType:MLMultiArrayDataTypeFloat32
                        strides:strides
                    deallocator:nil
                          error:&err];
        if (!arr) {
            _sc_spc_log("coreml: 输入包装失败: %s",
                        err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        MLDictionaryFeatureProvider* feed = [[MLDictionaryFeatureProvider alloc]
            initWithDictionary:@{ c->inName: [MLFeatureValue featureValueWithMultiArray:arr] }
                         error:&err];
        if (!feed) return false;

        id<MLFeatureProvider> result = [c->model predictionFromFeatures:feed error:&err];
        if (!result) {
            _sc_spc_log("coreml: 推理失败: %s",
                        err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        MLMultiArray* y = [result featureValueForName:c->outName].multiArrayValue;
        if (!y) { _sc_spc_log("coreml: 输出 %s 非张量", c->outName.UTF8String); return false; }
        if (y.count != out->numel) {
            _sc_spc_log("coreml: 输出元素数 %ld ≠ 目标张量 %lld",
                        (long)y.count, (long long)out->numel);
            return false;
        }

        /* 读出（fp16/fp64 自动转 f32） */
        float* dst = (float*)_sc_spc_tsdata(out);
        __block bool ok = true;
        MLMultiArrayDataType dt = y.dataType;
        NSInteger n = y.count;
        [y getBytesWithHandler:^(const void* bytes, NSInteger size) {
            (void)size;
            if (dt == MLMultiArrayDataTypeFloat32) {
                memcpy(dst, bytes, (size_t)n * 4);
            } else if (dt == MLMultiArrayDataTypeFloat16) {
                const __fp16* s = (const __fp16*)bytes;
                for (NSInteger i = 0; i < n; i++) dst[i] = (float)s[i];
            } else if (dt == MLMultiArrayDataTypeDouble) {
                const double* s = (const double*)bytes;
                for (NSInteger i = 0; i < n; i++) dst[i] = (float)s[i];
            } else {
                ok = false;
            }
        }];
        if (!ok) _sc_spc_log("coreml: 输出 dtype %ld 未支持", (long)dt);
        return ok;
    }
}

int _sc_spc_coreml_ane_ratio(_sc_spc_model_t* m) {
    CoreMlModel* c = (CoreMlModel*)m->backend;
    if (!c) return -1;

    if (@available(macOS 14.4, *)) {
        __block int total = 0, ane = 0;
        __block bool done_ok = false;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        [MLComputePlan loadContentsOfURL:c->url
                           configuration:configFor(c->units)
                       completionHandler:^(MLComputePlan* plan, NSError* err) {
            if (plan) {
                MLModelStructureProgram* prog = plan.modelStructure.program;
                for (NSString* fname in prog.functions) {
                    MLModelStructureProgramFunction* fn = prog.functions[fname];
                    for (MLModelStructureProgramOperation* op in fn.block.operations) {
                        if ([op.operatorName isEqualToString:@"const"]) continue;
                        MLComputePlanDeviceUsage* usage =
                            [plan computeDeviceUsageForMLProgramOperation:op];
                        if (!usage) continue;
                        total++;
#ifdef SC_SPC_DEBUG_PLAN
                        NSMutableString* sup = [NSMutableString string];
                        for (id d in usage.supportedComputeDevices)
                            [sup appendFormat:@"%@ ", NSStringFromClass([d class])];
                        _sc_spc_log("plan: %s -> %s (支持: %s)",
                                    op.operatorName.UTF8String,
                                    NSStringFromClass([usage.preferredComputeDevice class]).UTF8String,
                                    sup.UTF8String);
#endif
                        if ([usage.preferredComputeDevice
                                isKindOfClass:[MLNeuralEngineComputeDevice class]])
                            ane++;
                    }
                }
                done_ok = true;
            } else {
                _sc_spc_log("coreml: ComputePlan 加载失败: %s",
                            err ? err.localizedDescription.UTF8String : "?");
            }
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        if (!done_ok || total == 0) return -1;
        return ane * 100 / total;
    }
    return -1;   /* macOS < 14.4 无 MLComputePlan */
}

#endif /* __APPLE__ */
