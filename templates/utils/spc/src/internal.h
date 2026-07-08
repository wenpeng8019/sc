/* ============================================================
 * internal.h —— spc 模块内部：资源体、darwin 实现函数声明
 * ============================================================
 * 一期只有 darwin（Metal kernel + MPSGraph + CoreML）真实现，
 * 公共层 spc.c 经 #ifdef __APPLE__ 直连——刻意不抽 vtable，
 * 待二期 Vulkan/RKNN 后端进场时再抽（避免为单实现造抽象）。
 * ============================================================ */

#ifndef SC_SPC_INTERNAL_H
#define SC_SPC_INTERNAL_H

#include "../spc.h"
#include <stdbool.h>

void _sc_spc_log(const char* fmt, ...);

typedef enum _sc_spc_slot_state {
    _SC_SPC_SLOT_FREE = 0,
    _SC_SPC_SLOT_VALID,
    _SC_SPC_SLOT_FAILED,
} _sc_spc_slot_state;

typedef struct _sc_spc_buffer_t {
    uint32_t           id;
    _sc_spc_slot_state state;
    uint64_t           size;
    void*              backend;
} _sc_spc_buffer_t;

/* 反射解析结果：清单 binding 槽 → 名字/种类（dispatch 时按名对位 MSL 槽） */
typedef struct _sc_spc_kernel_res {
    char name[64];
    int  binding;
    int  storage;      /* 1 = storage 块，0 = uniform 块 */
} _sc_spc_kernel_res;

typedef struct _sc_spc_kernel_t {
    uint32_t           id;
    _sc_spc_slot_state state;
    _sc_spc_kernel_res res[SC_SPC_MAX_BINDINGS];
    int                res_count;
    int                local[3];    /* 线程组尺寸（反射清单 local_size；默认 64,1,1） */
    void*              backend;
} _sc_spc_kernel_t;

typedef struct _sc_spc_model_t {
    uint32_t           id;
    _sc_spc_slot_state state;
    void*              backend;
} _sc_spc_model_t;

/* ---- 张量字段助手（只读 sc_tensor 字段，不调 ts 函数） ------- */

static inline int _sc_spc_dtsize(int dtype) {
    switch (dtype) {
        case TS_DT_F8: case TS_DT_I8: return 8;
        case TS_DT_BOOL:              return 1;
        default:                      return 4;   /* F4 / I4 */
    }
}

static inline void* _sc_spc_tsdata(const sc_tensor* t) {
    if (!t || !t->store || !t->store->data) return (void*)0;
    return (char*)t->store->data + t->offset * _sc_spc_dtsize(t->dtype);
}

static inline bool _sc_spc_tscontig(const sc_tensor* t) {
    if (!t || t->ndim <= 0) return true;
    int64_t s = 1;
    for (int d = t->ndim - 1; d >= 0; d--) {
        if (t->shape[d] > 1 && t->strides[d] != s) return false;
        s *= t->shape[d];
    }
    return true;
}

/* ---- darwin 实现（metal_spc.m / mpsg_spc.m / coreml_spc.m） -- */
#if defined(__APPLE__)
bool _sc_spc_mtl_init(void);
void _sc_spc_mtl_shutdown(void);
void _sc_spc_mtl_finish(void);
bool _sc_spc_mtl_buffer_create(_sc_spc_buffer_t* b, const void* data, uint64_t size);
void _sc_spc_mtl_buffer_destroy(_sc_spc_buffer_t* b);
bool _sc_spc_mtl_buffer_read(_sc_spc_buffer_t* b, void* dst, uint64_t size, uint64_t off);
bool _sc_spc_mtl_buffer_write(_sc_spc_buffer_t* b, const void* src, uint64_t size, uint64_t off);
bool _sc_spc_mtl_kernel_create(_sc_spc_kernel_t* k, const sc_spc_kernel_desc* desc);
void _sc_spc_mtl_kernel_destroy(_sc_spc_kernel_t* k);
bool _sc_spc_mtl_dispatch(_sc_spc_kernel_t* k, int gx, int gy, int gz,
                          const sc_spc_bindings* bnd,
                          _sc_spc_buffer_t* bufs[SC_SPC_MAX_BINDINGS]);

int  _sc_spc_mpsg_mm(sc_tensor* a, sc_tensor* b, sc_tensor* out);

bool _sc_spc_coreml_load(_sc_spc_model_t* m, const char* path, int units);
void _sc_spc_coreml_destroy(_sc_spc_model_t* m);
bool _sc_spc_coreml_run1(_sc_spc_model_t* m, sc_tensor* in, sc_tensor* out);
int  _sc_spc_coreml_ane_ratio(_sc_spc_model_t* m);
#endif

#endif /* SC_SPC_INTERNAL_H */
