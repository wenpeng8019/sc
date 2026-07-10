/* ============================================================
 * internal.h —— gfx 模块内部：后端 vtable、句柄池、公共状态
 * ============================================================
 * 后端种类跟随 gpu（env 层）：sc_gfx_init 按 sc_gpu_query_backend()
 * 选配对的命令翻译 vtable。gfx.c 公共层负责句柄池、参数校验、
 * desc 缺省值解析、反射清单解析，后端只做纯图形 API 翻译。
 *
 * 句柄池（参考 sokol_gfx）：
 *   32 位 id = (代数 gen << 16) | 池槽位 index。槽 0 保留为无效。
 * ============================================================ */

#ifndef SC_GFX_INTERNAL_H
#define SC_GFX_INTERNAL_H

#include "../gfx.h"
#include "../../platform.h"   /* 平台判定宏 P_DARWIN/P_LINUX/P_WIN（尊重交叉目标 SC_TARGET_*） */
#include <stdbool.h>

/* ---- 编译期后端开关：按目标平台自推导（与 gpu/src/internal.h 同源逻辑）----
 * darwin → Metal + GL；linux → GL + Vulkan；windows → GL（WGL；Vulkan/D3D 待补）。
 * 平台判定用 platform.h 的 P_XXX（尊重交叉目标 SC_TARGET_*，非裸 __APPLE__/_WIN32）。
 * SC_GPU_GLES 由目标档 cflags 显式给出。 */
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

void gfx_log(const char* fmt, ...);

/* ---- 反射清单（scc .ss 产物）解析结果 --------------------- */

typedef struct gfx_reflect_block {
    int    stage;                 /* sc_gfx_shader_stage；-1=全阶段 */
    int    slot;                  /* binding */
    size_t size;                  /* std140 大小 */
    char   name[64];
} gfx_reflect_block;

typedef struct gfx_reflect_sampler {
    int  stage;
    int  slot;
    char name[64];
} gfx_reflect_sampler;

typedef struct gfx_reflect_attr {
    int  location;
    char name[64];
} gfx_reflect_attr;

typedef struct gfx_reflect {
    gfx_reflect_block   blocks[SC_GFX_MAX_UNIFORM_BLOCKS * 2];
    int                     block_count;
    gfx_reflect_sampler samplers[SC_GFX_MAX_SAMPLERS];
    int                     sampler_count;
    gfx_reflect_attr    attrs[SC_GFX_MAX_VERTEX_ATTRS];
    int                     attr_count;
} gfx_reflect;

/* gfx_reflect.c：解析 scc <stem>.reflect.json（失败返回 false） */
bool gfx_parse_reflect(const char* json, gfx_reflect* out);

/* ---- 资源公共体 ------------------------------------------- */

typedef enum gfx_slot_state {
    GFX_SLOT_FREE = 0,
    GFX_SLOT_ALLOC,      /* id 已分配，资源未创建 */
    GFX_SLOT_VALID,
    GFX_SLOT_FAILED,
} gfx_slot_state;

typedef struct gfx_slot {
    uint32_t id;             /* 完整句柄（gen|index），0=空 */
    gfx_slot_state state;
} gfx_slot;

typedef struct gfx_buffer_t {
    gfx_slot        slot;
    sc_gfx_buffer_desc  desc;      /* 已填缺省 */
    int                 append_pos;
    bool                append_overflow;
    void*               backend;   /* 后端私有 */
} gfx_buffer_t;

typedef struct gfx_image_t {
    gfx_slot       slot;
    sc_gfx_image_desc  desc;
    void*              backend;
} gfx_image_t;

typedef struct gfx_sampler_t {
    gfx_slot        slot;
    sc_gfx_sampler_desc desc;
    void*               backend;
} gfx_sampler_t;

typedef struct gfx_shader_t {
    gfx_slot       slot;
    gfx_reflect    reflect;   /* 解析后的绑定模型 */
    bool               has_cs;
    void*              backend;
} gfx_shader_t;

typedef struct gfx_pipeline_t {
    gfx_slot         slot;
    sc_gfx_pipeline_desc desc;    /* 已填缺省（含解析后的 stride/offset） */
    gfx_shader_t*    shader;
    void*                backend;
} gfx_pipeline_t;

/* ---- 句柄池 ----------------------------------------------- */

typedef struct gfx_pool {
    int   size;          /* 槽数（含保留槽 0） */
    int   queue_top;
    int*  free_queue;
    uint32_t* gen;       /* 每槽代数 */
} gfx_pool;

void     gfx_pool_init(gfx_pool* p, int num);
void     gfx_pool_free(gfx_pool* p);
uint32_t gfx_pool_alloc(gfx_pool* p);           /* 返回完整 id，0=满 */
void     gfx_pool_release(gfx_pool* p, uint32_t id);
static inline int gfx_slot_index(uint32_t id) { return (int)(id & 0xFFFF); }

/* ---- 后端 vtable ------------------------------------------ */

typedef struct gfx_backend_api {
    const char* name;

    bool (*init)(const sc_gfx_desc* desc);   /* device 经 sc_gpu_device() 自取 */
    void (*shutdown)(void);
    void (*finish)(void);                    /* 等待 GPU 全部完成（glFinish 语义） */

    bool (*buffer_create)(gfx_buffer_t* buf);
    void (*buffer_destroy)(gfx_buffer_t* buf);
    void (*buffer_update)(gfx_buffer_t* buf, const sc_gfx_range* data, int offset);

    bool (*image_create)(gfx_image_t* img);
    void (*image_destroy)(gfx_image_t* img);
    void (*image_update)(gfx_image_t* img, const sc_gfx_image_data* data);

    bool (*sampler_create)(gfx_sampler_t* smp);
    void (*sampler_destroy)(gfx_sampler_t* smp);

    bool (*shader_create)(gfx_shader_t* shd, const sc_gfx_shader_desc* desc);
    void (*shader_destroy)(gfx_shader_t* shd);

    bool (*pipeline_create)(gfx_pipeline_t* pip);
    void (*pipeline_destroy)(gfx_pipeline_t* pip);

    /* 交换链 pass（color_count==0 且非 compute）由后端内部
     * sc_gpu_frame_acquire() 取渲染目标 */
    void (*begin_pass)(const sc_gfx_pass* pass, gfx_image_t* color[], int color_count,
                       gfx_image_t* resolve[], gfx_image_t* depth);
    void (*apply_viewport)(int x, int y, int w, int h, bool top_left);
    void (*apply_scissor)(int x, int y, int w, int h, bool top_left);
    void (*apply_pipeline)(gfx_pipeline_t* pip);
    void (*apply_bindings)(gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                           gfx_buffer_t* vbufs[], gfx_buffer_t* ibuf,
                           gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                           gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                           gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]);
    void (*apply_uniforms)(int stage, int slot, const void* data, size_t size);
    void (*draw)(int base, int count, int instances);
    void (*dispatch)(int gx, int gy, int gz);
    void (*end_pass)(void);
    void (*commit)(void);                    /* 末尾调 sc_gpu_frame_end() */
    void (*query_pixelformat)(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out);
} gfx_backend_api;

/* 各后端导出（未编入则不声明） */
#ifdef SC_GPU_METAL
const gfx_backend_api* gfx_backend_metal(void);
#endif
#ifdef SC_GPU_GL
const gfx_backend_api* gfx_backend_gl(void);
#endif
#ifdef SC_GPU_VULKAN
const gfx_backend_api* gfx_backend_vulkan(void);
#endif
const gfx_backend_api* gfx_backend_null(void);

/* ---- 全局状态 --------------------------------------------- */

typedef struct gfx_state {
    bool  valid;
    sc_gfx_desc desc;                 /* 已填缺省 */
    const gfx_backend_api* api;

    gfx_pool buffer_pool;   gfx_buffer_t*   buffers;
    gfx_pool image_pool;    gfx_image_t*    images;
    gfx_pool sampler_pool;  gfx_sampler_t*  samplers;
    gfx_pool shader_pool;   gfx_shader_t*   shaders;
    gfx_pool pipeline_pool; gfx_pipeline_t* pipelines;

    /* 帧内状态 */
    bool in_pass;
    bool pass_is_swapchain;
    gfx_pipeline_t* cur_pipeline;
    int  frame_index;
} gfx_state;

extern gfx_state g_gfx;

/* 查表助手（校验代数） */
gfx_buffer_t*   gfx_lookup_buffer(uint32_t id);
gfx_image_t*    gfx_lookup_image(uint32_t id);
gfx_sampler_t*  gfx_lookup_sampler(uint32_t id);
gfx_shader_t*   gfx_lookup_shader(uint32_t id);
gfx_pipeline_t* gfx_lookup_pipeline(uint32_t id);

#endif /* SC_GFX_INTERNAL_H */
