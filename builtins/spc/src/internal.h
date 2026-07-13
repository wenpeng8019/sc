/* ============================================================
 * internal.h —— spc 模块内部：资源体、kernel 面 vtable、后端声明
 * ============================================================
 * 二期抽象：kernel 面（buffer/kernel/dispatch）按后端 vtable 派发——
 *   metal_spc.m（darwin）/ vulkan_spc.c（linux/android/win）/
 *   gl_spc.c（GLES3.1 / 桌面 GL4.3 compute）；后端跟随 gpu env 的
 *   实际生效后端（sc_gpu_query_backend）。
 * graph/model 面仍 darwin 直连（MPSGraph/CoreML；其它平台待 RKNN 等）。
 * ============================================================ */

#ifndef SC_SPC_INTERNAL_H
#define SC_SPC_INTERNAL_H

#include "../spc.h"
#include "../../platform.h"   /* 平台判定宏 P_DARWIN（尊重交叉目标 SC_TARGET_*） */
#include <stdbool.h>

void spc_log(const char* fmt, ...);

typedef enum spc_slot_state {
    SPC_SLOT_FREE = 0,
    SPC_SLOT_VALID,
    SPC_SLOT_FAILED,
} spc_slot_state;

typedef struct spc_buffer_t {
    uint32_t           id;
    spc_slot_state state;
    uint64_t           size;
    void*              backend;
} spc_buffer_t;

/* 反射解析结果：清单 binding 槽 → 名字/种类（dispatch 时按名对位 MSL 槽） */
typedef struct spc_kernel_res {
    char name[64];
    int  binding;
    int  storage;      /* 1 = storage 块，0 = uniform 块 */
} spc_kernel_res;

/* 反射的特化常量条目（spec_constants[]）：运行时传值时按 id 对位类型 */
typedef struct spc_kernel_spec {
    int  id;
    char type;         /* 'f'=float / 'i'=int / 'u'=uint（Metal 需类型；Vulkan 类型无关） */
} spc_kernel_spec;

typedef struct spc_kernel_t {
    uint32_t           id;
    spc_slot_state state;
    spc_kernel_res res[SC_SPC_MAX_BINDINGS];
    int                res_count;
    spc_kernel_spec    spec[SC_SPC_MAX_BINDINGS];   /* 反射 spec_constants */
    int                spec_count;
    int                local[3];    /* 线程组尺寸（反射清单 local_size；默认 64,1,1） */
    void*              backend;
} spc_kernel_t;

typedef struct spc_model_t {
    uint32_t           id;
    spc_slot_state state;
    void*              backend;
} spc_model_t;

/* ---- 张量字段助手（只读 sc_tensor 字段，不调 ts 函数） ------- */

static inline int spc_dtsize(int dtype) {
    switch (dtype) {
        case TS_DT_F8: case TS_DT_I8: return 8;
        case TS_DT_BOOL:              return 1;
        default:                      return 4;   /* F4 / I4 */
    }
}

static inline void* spc_tsdata(const sc_tensor* t) {
    if (!t || !t->store || !t->store->data) return (void*)0;
    return (char*)t->store->data + t->offset * spc_dtsize(t->dtype);
}

static inline bool spc_tscontig(const sc_tensor* t) {
    if (!t || t->ndim <= 0) return true;
    int64_t s = 1;
    for (int d = t->ndim - 1; d >= 0; d--) {
        if (t->shape[d] > 1 && t->strides[d] != s) return false;
        s *= t->shape[d];
    }
    return true;
}

/* ---- kernel 面 vtable（每后端一张；由 spc.c 按 gpu 后端选择） ---- */
typedef struct spc_kernel_api {
    const char* name;
    bool (*init)(void);
    void (*shutdown)(void);
    void (*finish)(void);
    bool (*buffer_create)(spc_buffer_t* b, const void* data, uint64_t size);
    void (*buffer_destroy)(spc_buffer_t* b);
    bool (*buffer_read)(spc_buffer_t* b, void* dst, uint64_t size, uint64_t off);
    bool (*buffer_write)(spc_buffer_t* b, const void* src, uint64_t size, uint64_t off);
    bool (*kernel_create)(spc_kernel_t* k, const sc_spc_kernel_desc* desc);
    void (*kernel_destroy)(spc_kernel_t* k);
    bool (*dispatch)(spc_kernel_t* k, int gx, int gy, int gz,
                     const sc_spc_bindings* bnd,
                     spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS]);
} spc_kernel_api;

/* 后端 vtable 入口（未编入的后端不声明；平台守卫与 gpu 模块同源） */
#if P_DARWIN
const spc_kernel_api* spc_mtl_api(void);

int  spc_mpsg_mm(sc_tensor* a, sc_tensor* b, sc_tensor* out);

bool spc_coreml_load(spc_model_t* m, const char* path, int units);
void spc_coreml_destroy(spc_model_t* m);
bool spc_coreml_run1(spc_model_t* m, sc_tensor* in, sc_tensor* out);
int  spc_coreml_ane_ratio(spc_model_t* m);
#endif
#if P_LINUX || P_WIN
const spc_kernel_api* spc_vk_api(void);    /* Vulkan compute（vulkan_spc.c） */
#endif
#if P_LINUX
const spc_kernel_api* spc_gl_api(void);    /* GLES3.1 / 桌面 GL4.3 compute（gl_spc.c） */
#endif
/* CPU SPMD 后端（cpu_spc.c，全平台；§17） */
const spc_kernel_api* spc_cpu_api(void);

#endif /* SC_SPC_INTERNAL_H */
