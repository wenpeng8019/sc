/* ============================================================
 * internal.h —— gpu（env 层）内部：后端 vtable、surface 池
 * ============================================================
 * 多后端机制（同 wsi/glfw）：每后端一张 gpu_env_api 函数表；
 * gpu.c 负责 surface 句柄池与分派，后端只做平台上下文/交换链翻译。
 * ============================================================ */

#ifndef SC_GPU_INTERNAL_H
#define SC_GPU_INTERNAL_H

#include "../gpu.h"
#include "../../platform.h"   /* 平台判定宏 P_DARWIN/P_LINUX/P_WIN（尊重交叉目标 SC_TARGET_*） */
#include <stdbool.h>

/* ---- 编译期后端开关：按目标平台自推导（无需构建系统注入） ----
 * 源文件由 scc 动态编译（模块 .sc 逐文件 add src 源码）：平台即配置——
 *   darwin → Metal + GL；linux → GL + Vulkan；windows → GL（WGL；Vulkan/D3D 待补）。
 * 平台判定用 platform.h 的 P_XXX（而非裸 __APPLE__/_WIN32）：交叉编译时
 * scc 注入 SC_TARGET_{WIN,DARWIN,LINUX}，令「目标平台」压过 C 编译器预定义的
 * 「宿主平台」宏——例如 mac 上不带 -target 交叉到 win，裸 __APPLE__ 会误真，
 * 而 P_DARWIN 经 SC_TARGET_WIN 正确为 0。未注入时 P_XXX 退化为自动判定。
 * SC_GPU_GLES（GLES 形态）是构建选择非平台事实，由目标档 cflags 显式给出
 *（-DSC_GPU_GLES -I builtins/gpu/khr）。显式 -D 优先（#ifndef 保护）。 */
#if P_DARWIN
  #ifndef SC_GPU_METAL
  #define SC_GPU_METAL 1
  #endif
  #ifndef SC_GPU_GL
  #define SC_GPU_GL 1
  #endif
#elif P_LINUX
  #ifndef SC_GPU_GL
  #define SC_GPU_GL 1
  #endif
  #ifndef SC_GPU_VULKAN
  #define SC_GPU_VULKAN 1
  #endif
#elif P_WIN
  #ifndef SC_GPU_GL
  #define SC_GPU_GL 1
  #endif
#endif

void gpu_log(const char* fmt, ...);

/* ---- surface 资源体 ---------------------------------------- */

typedef enum gpu_slot_state {
    GPU_SLOT_FREE = 0,
    GPU_SLOT_VALID,
    GPU_SLOT_FAILED,
} gpu_slot_state;

/* MEMORY surface 环槽状态（单生产者/单消费者；跨线程用 __atomic 读写） */
typedef enum gpu_ring_state {
    GPU_RING_FREE = 0,     /* 可供渲染 acquire */
    GPU_RING_ACQUIRED,     /* 渲染中（本帧） */
    GPU_RING_RENDERED,     /* 渲染完，可 dequeue */
    GPU_RING_DEQUEUED,     /* 消费中，等 enqueue 归还 */
} gpu_ring_state;

typedef struct gpu_surface_t {
    uint32_t            id;       /* 完整句柄（gen|index），0=空 */
    gpu_slot_state  state;
    sc_gpu_surface_desc desc;     /* 已填缺省；width/height 随 resize 更新 */
    void*               backend;  /* 后端私有 */

    /* kind=MEMORY：memimg 环（公共层分配与调度，后端按槽建渲染目标） */
    sc_gpu_memimg       ring_imgs[SC_GPU_MAX_MEMORY_IMAGES];
    int                 ring_state[SC_GPU_MAX_MEMORY_IMAGES];  /* gpu_ring_state */
    int                 ring_cur;        /* 本帧渲染槽；-1 = 未 acquire */
    int                 ring_acquire;    /* 下个 acquire 位置 */
    int                 ring_dequeue;    /* 下个 dequeue 位置 */
} gpu_surface_t;

/* memimg 资源体 */
typedef struct gpu_memimg_t {
    uint32_t            id;
    gpu_slot_state  state;
    sc_gpu_memimg_desc  desc;     /* 已填缺省（fourcc 已解析） */
    bool                imported; /* 导入（非自分配） */
    void*               backend;
} gpu_memimg_t;

/* ---- 句柄池 ----------------------------------------------- */

typedef struct gpu_pool {
    int   size;          /* 槽数（含保留槽 0） */
    int   queue_top;
    int*  free_queue;
    uint32_t* gen;
} gpu_pool;

static inline int gpu_slot_index(uint32_t id) { return (int)(id & 0xFFFF); }

/* ---- 后端 vtable ------------------------------------------ */

typedef struct gpu_env_api {
    const char*    name;
    sc_gpu_backend kind;

    bool (*init)(const sc_gpu_desc* desc);
    void (*shutdown)(void);
    void* (*device)(void);                          /* 原生设备句柄；无则 NULL */

    bool (*surface_create)(gpu_surface_t* surf);
    void (*surface_destroy)(gpu_surface_t* surf);
    void (*surface_activate)(gpu_surface_t* surf);   /* make current；NULL=无 */
    void (*surface_resize)(gpu_surface_t* surf, int w, int h);

    bool (*frame_acquire)(gpu_surface_t* surf, sc_gpu_frame* out);
    void (*frame_end)(void);

    /* memimg（可选：后端不支持则置 NULL） */
    bool (*memimg_alloc)(gpu_memimg_t* img);
    bool (*memimg_import)(gpu_memimg_t* img, const sc_gpu_memory_frame* src);
    bool (*memimg_export)(gpu_memimg_t* img, sc_gpu_memory_frame* out, bool with_fence);
    void* (*memimg_native)(gpu_memimg_t* img);
    void* (*memimg_map)(gpu_memimg_t* img, int plane, uint32_t* out_stride);
    void (*memimg_unmap)(gpu_memimg_t* img, int plane);
    void (*memimg_free)(gpu_memimg_t* img);

    /* MEMORY surface：从槽位导出帧（含 frame_end 时存的栅栏） */
    bool (*surface_dequeue)(gpu_surface_t* surf, int slot, sc_gpu_memory_frame* out);
} gpu_env_api;

#ifdef SC_GPU_METAL
const gpu_env_api* gpu_env_metal(void);
#endif
#ifdef SC_GPU_GL
const gpu_env_api* gpu_env_gl(void);
#endif
#ifdef SC_GPU_VULKAN
const gpu_env_api* gpu_env_vulkan(void);
#endif
const gpu_env_api* gpu_env_null(void);

/* ---- 全局状态 --------------------------------------------- */

typedef struct gpu_state {
    bool  valid;
    sc_gpu_desc desc;                 /* 已填缺省 */
    const gpu_env_api* api;

    gpu_pool       surface_pool;
    gpu_surface_t* surfaces;
    gpu_pool       memimg_pool;
    gpu_memimg_t*  memimgs;

    gpu_surface_t* cur_surface;   /* make_current 目标；NULL=无 */
    sc_gpu_surface     cur_surface_id;

    /* 本帧已 acquire 的 MEMORY surface（frame_end 时推进环状态） */
    gpu_surface_t* mem_acquired[16];
    int                mem_acquired_count;
} gpu_state;

extern gpu_state g_gpu;

gpu_surface_t* gpu_lookup_surface(uint32_t id);
gpu_memimg_t*  gpu_lookup_memimg(uint32_t id);

#endif /* SC_GPU_INTERNAL_H */
