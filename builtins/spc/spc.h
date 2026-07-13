/* ============================================================
 * spc.h —— sc 多维空间并行计算模块（space compute）C API
 * ============================================================
 * 命名旨趣：cpu = 串行·逻辑·时间；gpu/spc = 并行·变换·空间。
 * 现代 GPU 已超越图像渲染，扩展到空间计算 / AI 并行计算——spc 即
 * gpu（env 层）之下与 gfx（渲染）平级的"计算路"。
 *
 * 三个入口（一个模块，按工作负载选引擎）：
 *   kernel 面（可编程）：执行 scc 编译 .ss comp 的产物（MSL 计算内核）
 *       —— 自定义并行算法 / 图像处理 / 传感器数据的主场（Metal，一期）
 *   graph 面（算子）  ：高性能张量算子，走平台算子图引擎
 *       —— mac = MPSGraph（一期 matmul）；为 nn 的 GPU 加速铺路
 *   model 面（整图推理）：加载编译好的模型执行
 *       —— mac = CoreML（可调度 ANE 推理芯片；MLComputePlan 可查证）
 *          板子 = RKNN 等厂商栈（后续）
 *
 * 与其他模块的关系：
 *   · 须先 sc_gpu_init（共享 sc_gpu_device()；mac headless 即可）
 *   · 与 builtins/ts 互操作：接口收 sc_tensor*（须 C-连续），只读其
 *     字段（data/shape/numel/dtype），不调用 ts 函数——零链接依赖。
 *     ts/nn 保持 CPU 纯数值不动；nn 热点算子接 GPU 属二期。
 *   · gfx 的图形语境 compute（粒子/后效）保留在 gfx；spc 面向
 *     AI/通用计算：独立命令队列、独立生命周期、不依赖 gfx。
 *
 * ANE（Apple 推理芯片）的现实约束：
 *   ANE 不可直接编程——kernel 面在 mac 恒为 GPU；ANE 唯一公开路径
 *   是 CoreML 整图执行（model 面），sc_spc_model_ane_ratio() 用
 *   MLComputePlan（macOS 14.4+）程序化查证算子实际调度。
 *
 * 函数名带 sc_ 前缀以匹配 sc 侧 @fnc name:: 约定（spc.sc 里去前缀）。
 * ============================================================ */

#ifndef SC_SPC_H
#define SC_SPC_H

#include <stdint.h>
#include "../ts/ts.h"   /* sc_tensor（编译期结构定义，无链接依赖） */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄 ------------------------------------------------ */
/* 32 位不透明句柄：低 16 位池索引，高 16 位代数。0 = 无效。 */

typedef uint32_t sc_spc_buffer;
typedef uint32_t sc_spc_kernel;
typedef uint32_t sc_spc_model;

/* ---- 通用 ------------------------------------------------ */

typedef struct sc_spc_range {
    const void* ptr;
    uint64_t    size;
} sc_spc_range;

enum { SC_SPC_MAX_BINDINGS = 8 };   /* 每内核绑定槽数（= 反射清单 binding） */

/* ---- 初始化 ---------------------------------------------- */

/* kernel 面后端选择：缺省跟随 gpu env 实际后端（Metal/GL/Vulkan）；
 * CPU = SPMD 循环化 C 直发（tar cpu@99 产物，宿主 add 编入）——全平台可用，
 * 有 GPU 时也可强制选用（数值对拍参考 / 无 GPU 兄底）。 */
typedef enum sc_spc_kernel_backend {
    SC_SPC_KERNEL_DEFAULT = 0,   /* 跟随 gpu_query_backend() */
    SC_SPC_KERNEL_CPU,           /* 强制 CPU SPMD 后端 */
} sc_spc_kernel_backend;

typedef struct sc_spc_desc {
    int buffer_pool_size;    /* 默认 128 */
    int kernel_pool_size;    /* 默认 32 */
    int model_pool_size;     /* 默认 8 */
    int kernel_backend;      /* sc_spc_kernel_backend；默认跟随 gpu */
} sc_spc_desc;

int  sc_spc_init(const sc_spc_desc* desc);   /* 1 成功 / 0 失败；须先 sc_gpu_init */
void sc_spc_shutdown(void);
int  sc_spc_isvalid(void);
void sc_spc_finish(void);                    /* 等待 GPU 完成全部已提交计算 */

/* ---- kernel 面：buffer ------------------------------------ */

typedef struct sc_spc_buffer_desc {
    uint64_t    size;        /* 字节；data 提供时可 0（取 data 大小需显式给 size） */
    const void* data;        /* 初始数据（可 NULL） */
    const char* label;
} sc_spc_buffer_desc;

sc_spc_buffer sc_spc_make_buffer(const sc_spc_buffer_desc* desc);
/* ts 互操作：按张量内容建缓冲 / 读回张量（须 C-连续；dtype 原样按字节搬运） */
sc_spc_buffer sc_spc_buffer_from_tensor(sc_tensor* t);
int  sc_spc_buffer_to_tensor(sc_spc_buffer buf, sc_tensor* t);
int  sc_spc_buffer_read(sc_spc_buffer buf, void* dst, uint64_t size, uint64_t offset);
int  sc_spc_buffer_write(sc_spc_buffer buf, const void* src, uint64_t size, uint64_t offset);
void sc_spc_destroy_buffer(sc_spc_buffer buf);

/* ---- kernel 面：内核与调度 --------------------------------- */

/* code = scc .ss comp 产物（按后端：Metal=MSL 文本；Vulkan=.spv 二进制；
 * GL/GLES=GLSL 文本——用 reflect JSON 的 target.api 或产物条目 target 选对形态）；
 * entry = comp 阶段函数名；
 * reflect_json = 同批 .reflect.json 内容——运行时据此建立
 * binding 槽 → 后端参数位 的名字对位（spirv-cross 会重排 MSL 槽），
 * 取 local_size 作为线程组尺寸，并按 spec_constants 对位特化常量类型。 */

/* 特化常量运行时传值（管线创建期覆写 `let X: T = v spec N` 的默认值）：
 *   id    = constant_id（反射 spec_constants[].id）
 *   value = 32 位值（f4 用位表示：memcpy float 进来；i4/u4 直接赋）
 * Metal = MTLFunctionConstantValues；Vulkan = VkSpecializationInfo；
 * GL/GLES 不支持（GLSL 文本已固化默认值，传入时警告并忽略）。 */
typedef struct sc_spc_spec_value {
    int      id;
    uint32_t value;
} sc_spc_spec_value;

typedef struct sc_spc_kernel_desc {
    sc_spc_range code;
    const char*  entry;
    const char*  reflect_json;
    const char*  label;
    const sc_spc_spec_value* spec_values;   /* 可 NULL（全用默认值） */
    int          spec_count;
} sc_spc_kernel_desc;

/* 绑定：按反射清单的 binding 槽下标。
 *   buffers[b]  —— storage 块（大数据，GPU 缓冲）
 *   uniforms[b] —— uniform 块（小参数，直接内联字节；布局须与块一致） */
typedef struct sc_spc_bindings {
    sc_spc_buffer buffers[SC_SPC_MAX_BINDINGS];
    sc_spc_range  uniforms[SC_SPC_MAX_BINDINGS];
} sc_spc_bindings;

sc_spc_kernel sc_spc_make_kernel(const sc_spc_kernel_desc* desc);
void sc_spc_destroy_kernel(sc_spc_kernel k);
/* gx/gy/gz = 全局线程数（N 维网格；Metal 精确网格 dispatchThreads）。1 成功 */
int  sc_spc_dispatch(sc_spc_kernel k, int gx, int gy, int gz,
                     const sc_spc_bindings* bindings);

/* ---- graph 面：张量算子（mac = MPSGraph） ------------------- */

/* out = a @ b（2D，DT_F4，全部 C-连续；out 须预分配 [M,N]）。1 成功 */
int sc_spc_mm(sc_tensor* a, sc_tensor* b, sc_tensor* out);

/* ---- model 面：整图推理（mac = CoreML / ANE） ---------------- */

typedef enum sc_spc_compute_units {
    SC_SPC_UNITS_ALL = 0,        /* CPU + GPU + ANE（系统自动调度） */
    SC_SPC_UNITS_CPU_ONLY,
    SC_SPC_UNITS_CPU_GPU,
    SC_SPC_UNITS_CPU_ANE,        /* 倾向推理芯片 */
} sc_spc_compute_units;

/* path：mac 为编译后的 .mlmodelc 目录（coremlcompiler 产物） */
sc_spc_model sc_spc_model_load(const char* path, int compute_units);
void sc_spc_destroy_model(sc_spc_model m);
/* 单输入单输出推理：in 喂给模型首个输入，结果写 out（形状/容量须匹配，
 * DT_F4 连续；fp16 输出自动转换）。1 成功 */
int  sc_spc_model_run1(sc_spc_model m, sc_tensor* in, sc_tensor* out);
/* ANE 调度占比（0..100，按 MLComputePlan 统计算子数；-1 = 不可查询） */
int  sc_spc_model_ane_ratio(sc_spc_model m);

#ifdef __cplusplus
}
#endif

#endif /* SC_SPC_H */
