/* ============================================================
 * internal.h —— gpu（env 层）内部：后端 vtable、surface 池
 * ============================================================
 * 多后端机制（同 wsi/glfw）：每后端一张 _sc_gpu_env_api 函数表；
 * gpu.c 负责 surface 句柄池与分派，后端只做平台上下文/交换链翻译。
 * ============================================================ */

#ifndef SC_GPU_INTERNAL_H
#define SC_GPU_INTERNAL_H

#include "../gpu.h"
#include <stdbool.h>

/* ---- 编译期后端开关（build.sh 按平台注入） ---------------- */
/* SC_GPU_METAL —— Metal（macOS）
 * SC_GPU_GL    —— OpenGL（macOS NSGL / Linux GLX；Win WGL 待加载器） */

void _sc_gpu_log(const char* fmt, ...);

/* ---- surface 资源体 ---------------------------------------- */

typedef enum _sc_gpu_slot_state {
    _SC_GPU_SLOT_FREE = 0,
    _SC_GPU_SLOT_VALID,
    _SC_GPU_SLOT_FAILED,
} _sc_gpu_slot_state;

typedef struct _sc_gpu_surface_t {
    uint32_t            id;       /* 完整句柄（gen|index），0=空 */
    _sc_gpu_slot_state  state;
    sc_gpu_surface_desc desc;     /* 已填缺省；width/height 随 resize 更新 */
    void*               backend;  /* 后端私有 */
} _sc_gpu_surface_t;

/* ---- 句柄池 ----------------------------------------------- */

typedef struct _sc_gpu_pool {
    int   size;          /* 槽数（含保留槽 0） */
    int   queue_top;
    int*  free_queue;
    uint32_t* gen;
} _sc_gpu_pool;

static inline int _sc_gpu_slot_index(uint32_t id) { return (int)(id & 0xFFFF); }

/* ---- 后端 vtable ------------------------------------------ */

typedef struct _sc_gpu_env_api {
    const char*    name;
    sc_gpu_backend kind;

    bool (*init)(const sc_gpu_desc* desc);
    void (*shutdown)(void);
    void* (*device)(void);                          /* 原生设备句柄；无则 NULL */

    bool (*surface_create)(_sc_gpu_surface_t* surf);
    void (*surface_destroy)(_sc_gpu_surface_t* surf);
    void (*surface_activate)(_sc_gpu_surface_t* surf);   /* make current；NULL=无 */
    void (*surface_resize)(_sc_gpu_surface_t* surf, int w, int h);

    bool (*frame_acquire)(_sc_gpu_surface_t* surf, sc_gpu_frame* out);
    void (*frame_end)(void);
} _sc_gpu_env_api;

#ifdef SC_GPU_METAL
const _sc_gpu_env_api* _sc_gpu_env_metal(void);
#endif
#ifdef SC_GPU_GL
const _sc_gpu_env_api* _sc_gpu_env_gl(void);
#endif
const _sc_gpu_env_api* _sc_gpu_env_null(void);

/* ---- 全局状态 --------------------------------------------- */

typedef struct _sc_gpu_state {
    bool  valid;
    sc_gpu_desc desc;                 /* 已填缺省 */
    const _sc_gpu_env_api* api;

    _sc_gpu_pool       surface_pool;
    _sc_gpu_surface_t* surfaces;

    _sc_gpu_surface_t* cur_surface;   /* make_current 目标；NULL=无 */
    sc_gpu_surface     cur_surface_id;
} _sc_gpu_state;

extern _sc_gpu_state _sc_gpu;

_sc_gpu_surface_t* _sc_gpu_lookup_surface(uint32_t id);

#endif /* SC_GPU_INTERNAL_H */
