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
#include <stdbool.h>

/* ---- 编译期后端开关（与 gpu 的 build.sh 同套宏） ----------- */
/* SC_GPU_METAL / SC_GPU_GL */

void _sc_gfx_log(const char* fmt, ...);

/* ---- 反射清单（scc .sg 产物）解析结果 --------------------- */

typedef struct _sc_gfx_reflect_block {
    int    stage;                 /* sc_gfx_shader_stage；-1=全阶段 */
    int    slot;                  /* binding */
    size_t size;                  /* std140 大小 */
    char   name[64];
} _sc_gfx_reflect_block;

typedef struct _sc_gfx_reflect_sampler {
    int  stage;
    int  slot;
    char name[64];
} _sc_gfx_reflect_sampler;

typedef struct _sc_gfx_reflect_attr {
    int  location;
    char name[64];
} _sc_gfx_reflect_attr;

typedef struct _sc_gfx_reflect {
    _sc_gfx_reflect_block   blocks[SC_GFX_MAX_UNIFORM_BLOCKS * 2];
    int                     block_count;
    _sc_gfx_reflect_sampler samplers[SC_GFX_MAX_SAMPLERS];
    int                     sampler_count;
    _sc_gfx_reflect_attr    attrs[SC_GFX_MAX_VERTEX_ATTRS];
    int                     attr_count;
} _sc_gfx_reflect;

/* gfx_reflect.c：解析 scc <stem>.reflect.json（失败返回 false） */
bool _sc_gfx_parse_reflect(const char* json, _sc_gfx_reflect* out);

/* ---- 资源公共体 ------------------------------------------- */

typedef enum _sc_gfx_slot_state {
    _SC_GFX_SLOT_FREE = 0,
    _SC_GFX_SLOT_ALLOC,      /* id 已分配，资源未创建 */
    _SC_GFX_SLOT_VALID,
    _SC_GFX_SLOT_FAILED,
} _sc_gfx_slot_state;

typedef struct _sc_gfx_slot {
    uint32_t id;             /* 完整句柄（gen|index），0=空 */
    _sc_gfx_slot_state state;
} _sc_gfx_slot;

typedef struct _sc_gfx_buffer_t {
    _sc_gfx_slot        slot;
    sc_gfx_buffer_desc  desc;      /* 已填缺省 */
    int                 append_pos;
    bool                append_overflow;
    void*               backend;   /* 后端私有 */
} _sc_gfx_buffer_t;

typedef struct _sc_gfx_image_t {
    _sc_gfx_slot       slot;
    sc_gfx_image_desc  desc;
    void*              backend;
} _sc_gfx_image_t;

typedef struct _sc_gfx_sampler_t {
    _sc_gfx_slot        slot;
    sc_gfx_sampler_desc desc;
    void*               backend;
} _sc_gfx_sampler_t;

typedef struct _sc_gfx_shader_t {
    _sc_gfx_slot       slot;
    _sc_gfx_reflect    reflect;   /* 解析后的绑定模型 */
    bool               has_cs;
    void*              backend;
} _sc_gfx_shader_t;

typedef struct _sc_gfx_pipeline_t {
    _sc_gfx_slot         slot;
    sc_gfx_pipeline_desc desc;    /* 已填缺省（含解析后的 stride/offset） */
    _sc_gfx_shader_t*    shader;
    void*                backend;
} _sc_gfx_pipeline_t;

/* ---- 句柄池 ----------------------------------------------- */

typedef struct _sc_gfx_pool {
    int   size;          /* 槽数（含保留槽 0） */
    int   queue_top;
    int*  free_queue;
    uint32_t* gen;       /* 每槽代数 */
} _sc_gfx_pool;

void     _sc_gfx_pool_init(_sc_gfx_pool* p, int num);
void     _sc_gfx_pool_free(_sc_gfx_pool* p);
uint32_t _sc_gfx_pool_alloc(_sc_gfx_pool* p);           /* 返回完整 id，0=满 */
void     _sc_gfx_pool_release(_sc_gfx_pool* p, uint32_t id);
static inline int _sc_gfx_slot_index(uint32_t id) { return (int)(id & 0xFFFF); }

/* ---- 后端 vtable ------------------------------------------ */

typedef struct _sc_gfx_backend_api {
    const char* name;

    bool (*init)(const sc_gfx_desc* desc);   /* device 经 sc_gpu_device() 自取 */
    void (*shutdown)(void);
    void (*finish)(void);                    /* 等待 GPU 全部完成（glFinish 语义） */

    bool (*buffer_create)(_sc_gfx_buffer_t* buf);
    void (*buffer_destroy)(_sc_gfx_buffer_t* buf);
    void (*buffer_update)(_sc_gfx_buffer_t* buf, const sc_gfx_range* data, int offset);

    bool (*image_create)(_sc_gfx_image_t* img);
    void (*image_destroy)(_sc_gfx_image_t* img);
    void (*image_update)(_sc_gfx_image_t* img, const sc_gfx_image_data* data);

    bool (*sampler_create)(_sc_gfx_sampler_t* smp);
    void (*sampler_destroy)(_sc_gfx_sampler_t* smp);

    bool (*shader_create)(_sc_gfx_shader_t* shd, const sc_gfx_shader_desc* desc);
    void (*shader_destroy)(_sc_gfx_shader_t* shd);

    bool (*pipeline_create)(_sc_gfx_pipeline_t* pip);
    void (*pipeline_destroy)(_sc_gfx_pipeline_t* pip);

    /* 交换链 pass（color_count==0 且非 compute）由后端内部
     * sc_gpu_frame_acquire() 取渲染目标 */
    void (*begin_pass)(const sc_gfx_pass* pass, _sc_gfx_image_t* color[], int color_count,
                       _sc_gfx_image_t* resolve[], _sc_gfx_image_t* depth);
    void (*apply_viewport)(int x, int y, int w, int h, bool top_left);
    void (*apply_scissor)(int x, int y, int w, int h, bool top_left);
    void (*apply_pipeline)(_sc_gfx_pipeline_t* pip);
    void (*apply_bindings)(_sc_gfx_pipeline_t* pip, const sc_gfx_bindings* bnd,
                           _sc_gfx_buffer_t* vbufs[], _sc_gfx_buffer_t* ibuf,
                           _sc_gfx_image_t* imgs[][SC_GFX_MAX_IMAGES],
                           _sc_gfx_sampler_t* smps[][SC_GFX_MAX_SAMPLERS],
                           _sc_gfx_buffer_t* sbufs[][SC_GFX_MAX_STORAGE_BUFFERS]);
    void (*apply_uniforms)(int stage, int slot, const void* data, size_t size);
    void (*draw)(int base, int count, int instances);
    void (*dispatch)(int gx, int gy, int gz);
    void (*end_pass)(void);
    void (*commit)(void);                    /* 末尾调 sc_gpu_frame_end() */
    void (*query_pixelformat)(sc_gpu_pixel_format fmt, sc_gfx_pixelformat_info* out);
} _sc_gfx_backend_api;

/* 各后端导出（未编入则不声明） */
#ifdef SC_GPU_METAL
const _sc_gfx_backend_api* _sc_gfx_backend_metal(void);
#endif
#ifdef SC_GPU_GL
const _sc_gfx_backend_api* _sc_gfx_backend_gl(void);
#endif
const _sc_gfx_backend_api* _sc_gfx_backend_null(void);

/* ---- 全局状态 --------------------------------------------- */

typedef struct _sc_gfx_state {
    bool  valid;
    sc_gfx_desc desc;                 /* 已填缺省 */
    const _sc_gfx_backend_api* api;

    _sc_gfx_pool buffer_pool;   _sc_gfx_buffer_t*   buffers;
    _sc_gfx_pool image_pool;    _sc_gfx_image_t*    images;
    _sc_gfx_pool sampler_pool;  _sc_gfx_sampler_t*  samplers;
    _sc_gfx_pool shader_pool;   _sc_gfx_shader_t*   shaders;
    _sc_gfx_pool pipeline_pool; _sc_gfx_pipeline_t* pipelines;

    /* 帧内状态 */
    bool in_pass;
    bool pass_is_swapchain;
    _sc_gfx_pipeline_t* cur_pipeline;
    int  frame_index;
} _sc_gfx_state;

extern _sc_gfx_state _sc_gfx;

/* 查表助手（校验代数） */
_sc_gfx_buffer_t*   _sc_gfx_lookup_buffer(uint32_t id);
_sc_gfx_image_t*    _sc_gfx_lookup_image(uint32_t id);
_sc_gfx_sampler_t*  _sc_gfx_lookup_sampler(uint32_t id);
_sc_gfx_shader_t*   _sc_gfx_lookup_shader(uint32_t id);
_sc_gfx_pipeline_t* _sc_gfx_lookup_pipeline(uint32_t id);

#endif /* SC_GFX_INTERNAL_H */
