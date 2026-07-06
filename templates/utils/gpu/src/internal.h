/* ============================================================
 * internal.h —— gpu 模块内部：后端 vtable、句柄池、公共状态
 * ============================================================
 * 多后端机制（同 wsi/glfw）：
 *   每个后端提供一张 _sc_gpu_backend_api 函数表；gpu.c 公共层负责
 *   句柄池管理、参数校验、desc 缺省值解析、反射清单解析，然后把
 *   已解析的资源对象转交后端函数表。
 *
 * 句柄池（参考 sokol_gfx）：
 *   32 位 id = (代数 gen << 16) | 池槽位 index。槽 0 保留为无效。
 *   资源公共体（_sc_gpu_*_common）保存已解析 desc；后端私有体
 *   （backend union）由各后端自取字段。
 * ============================================================ */

#ifndef SC_GPU_INTERNAL_H
#define SC_GPU_INTERNAL_H

#include "../gpu.h"
#include <stdbool.h>

/* ---- 编译期后端开关（build.sh 按平台注入） ---------------- */
/* SC_GPU_METAL —— Metal 后端（macOS）
 * SC_GPU_GL    —— OpenGL 后端（macOS NSGL / Linux GLX/EGL / Win WGL）
 * SC_GPU_NULL_BACKEND —— 空后端（恒可用） */

/* ---- 日志/错误 -------------------------------------------- */

void _sc_gpu_log(const char* fmt, ...);

/* ---- 反射清单（scc .sg 产物）解析结果 --------------------- */

typedef struct _sc_gpu_reflect_block {
    int    stage;                 /* sc_gpu_shader_stage */
    int    slot;                  /* binding */
    size_t size;                  /* std140 大小 */
    char   name[64];
} _sc_gpu_reflect_block;

typedef struct _sc_gpu_reflect_sampler {
    int  stage;
    int  slot;
    char name[64];
} _sc_gpu_reflect_sampler;

typedef struct _sc_gpu_reflect_attr {
    int  location;
    char name[64];
} _sc_gpu_reflect_attr;

typedef struct _sc_gpu_reflect {
    _sc_gpu_reflect_block   blocks[SC_GPU_MAX_UNIFORM_BLOCKS * 2];
    int                     block_count;
    _sc_gpu_reflect_sampler samplers[SC_GPU_MAX_SAMPLERS];
    int                     sampler_count;
    _sc_gpu_reflect_attr    attrs[SC_GPU_MAX_VERTEX_ATTRS];
    int                     attr_count;
} _sc_gpu_reflect;

/* gpu_reflect.c：解析 scc <stem>.reflect.json（失败返回 false） */
bool _sc_gpu_parse_reflect(const char* json, _sc_gpu_reflect* out);

/* ---- 资源公共体 ------------------------------------------- */

typedef enum _sc_gpu_slot_state {
    _SC_GPU_SLOT_FREE = 0,
    _SC_GPU_SLOT_ALLOC,      /* id 已分配，资源未创建 */
    _SC_GPU_SLOT_VALID,
    _SC_GPU_SLOT_FAILED,
} _sc_gpu_slot_state;

typedef struct _sc_gpu_slot {
    uint32_t id;             /* 完整句柄（gen|index），0=空 */
    _sc_gpu_slot_state state;
} _sc_gpu_slot;

typedef struct _sc_gpu_buffer_t {
    _sc_gpu_slot        slot;
    sc_gpu_buffer_desc  desc;      /* 已填缺省 */
    int                 append_pos;
    bool                append_overflow;
    void*               backend;   /* 后端私有 */
} _sc_gpu_buffer_t;

typedef struct _sc_gpu_image_t {
    _sc_gpu_slot       slot;
    sc_gpu_image_desc  desc;
    void*              backend;
} _sc_gpu_image_t;

typedef struct _sc_gpu_sampler_t {
    _sc_gpu_slot        slot;
    sc_gpu_sampler_desc desc;
    void*               backend;
} _sc_gpu_sampler_t;

typedef struct _sc_gpu_shader_t {
    _sc_gpu_slot       slot;
    _sc_gpu_reflect    reflect;   /* 解析后的绑定模型 */
    bool               has_cs;
    void*              backend;
} _sc_gpu_shader_t;

typedef struct _sc_gpu_pipeline_t {
    _sc_gpu_slot         slot;
    sc_gpu_pipeline_desc desc;    /* 已填缺省（含解析后的 stride/offset） */
    _sc_gpu_shader_t*    shader;
    void*                backend;
} _sc_gpu_pipeline_t;

typedef struct _sc_gpu_surface_t {
    _sc_gpu_slot        slot;
    sc_gpu_surface_desc desc;     /* 已填缺省；width/height 随 resize 更新 */
    void*               backend;
} _sc_gpu_surface_t;

/* ---- 句柄池 ----------------------------------------------- */

typedef struct _sc_gpu_pool {
    int   size;          /* 槽数（含保留槽 0） */
    int   queue_top;
    int*  free_queue;
    uint32_t* gen;       /* 每槽代数 */
} _sc_gpu_pool;

void     _sc_gpu_pool_init(_sc_gpu_pool* p, int num);
void     _sc_gpu_pool_free(_sc_gpu_pool* p);
uint32_t _sc_gpu_pool_alloc(_sc_gpu_pool* p);           /* 返回完整 id，0=满 */
void     _sc_gpu_pool_release(_sc_gpu_pool* p, uint32_t id);
static inline int _sc_gpu_slot_index(uint32_t id) { return (int)(id & 0xFFFF); }

/* ---- 后端 vtable ------------------------------------------ */

typedef struct _sc_gpu_backend_api {
    const char* name;
    sc_gpu_backend kind;

    bool (*init)(const sc_gpu_desc* desc);
    void (*shutdown)(void);

    bool (*surface_create)(_sc_gpu_surface_t* surf);
    void (*surface_destroy)(_sc_gpu_surface_t* surf);
    void (*surface_activate)(_sc_gpu_surface_t* surf);   /* make current；NULL=无 */
    void (*surface_resize)(_sc_gpu_surface_t* surf, int w, int h);

    bool (*buffer_create)(_sc_gpu_buffer_t* buf);
    void (*buffer_destroy)(_sc_gpu_buffer_t* buf);
    void (*buffer_update)(_sc_gpu_buffer_t* buf, const sc_gpu_range* data, int offset);

    bool (*image_create)(_sc_gpu_image_t* img);
    void (*image_destroy)(_sc_gpu_image_t* img);
    void (*image_update)(_sc_gpu_image_t* img, const sc_gpu_image_data* data);

    bool (*sampler_create)(_sc_gpu_sampler_t* smp);
    void (*sampler_destroy)(_sc_gpu_sampler_t* smp);

    bool (*shader_create)(_sc_gpu_shader_t* shd, const sc_gpu_shader_desc* desc);
    void (*shader_destroy)(_sc_gpu_shader_t* shd);

    bool (*pipeline_create)(_sc_gpu_pipeline_t* pip);
    void (*pipeline_destroy)(_sc_gpu_pipeline_t* pip);

    void (*begin_pass)(const sc_gpu_pass* pass, _sc_gpu_image_t* color[], int color_count,
                       _sc_gpu_image_t* resolve[], _sc_gpu_image_t* depth);
    void (*apply_viewport)(int x, int y, int w, int h, bool top_left);
    void (*apply_scissor)(int x, int y, int w, int h, bool top_left);
    void (*apply_pipeline)(_sc_gpu_pipeline_t* pip);
    void (*apply_bindings)(_sc_gpu_pipeline_t* pip, const sc_gpu_bindings* bnd,
                           _sc_gpu_buffer_t* vbufs[], _sc_gpu_buffer_t* ibuf,
                           _sc_gpu_image_t* imgs[][SC_GPU_MAX_IMAGES],
                           _sc_gpu_sampler_t* smps[][SC_GPU_MAX_SAMPLERS],
                           _sc_gpu_buffer_t* sbufs[][SC_GPU_MAX_STORAGE_BUFFERS]);
    void (*apply_uniforms)(int stage, int slot, const void* data, size_t size);
    void (*draw)(int base, int count, int instances);
    void (*dispatch)(int gx, int gy, int gz);
    void (*end_pass)(void);
    void (*commit)(void);
    void (*query_pixelformat)(sc_gpu_pixel_format fmt, sc_gpu_pixelformat_info* out);
} _sc_gpu_backend_api;

/* 各后端导出（未编入则为弱空实现 / 不声明） */
#ifdef SC_GPU_METAL
const _sc_gpu_backend_api* _sc_gpu_backend_metal(void);
#endif
#ifdef SC_GPU_GL
const _sc_gpu_backend_api* _sc_gpu_backend_gl(void);
#endif
const _sc_gpu_backend_api* _sc_gpu_backend_null(void);

/* ---- 全局状态 --------------------------------------------- */

typedef struct _sc_gpu_state {
    bool  valid;
    sc_gpu_desc desc;                 /* 已填缺省 */
    const _sc_gpu_backend_api* api;

    _sc_gpu_pool buffer_pool;   _sc_gpu_buffer_t*   buffers;
    _sc_gpu_pool image_pool;    _sc_gpu_image_t*    images;
    _sc_gpu_pool sampler_pool;  _sc_gpu_sampler_t*  samplers;
    _sc_gpu_pool shader_pool;   _sc_gpu_shader_t*   shaders;
    _sc_gpu_pool pipeline_pool; _sc_gpu_pipeline_t* pipelines;
    _sc_gpu_pool surface_pool;  _sc_gpu_surface_t*  surfaces;

    /* 帧内状态 */
    bool in_pass;
    bool pass_is_swapchain;
    _sc_gpu_pipeline_t* cur_pipeline;
    _sc_gpu_surface_t*  cur_surface;   /* make_current 目标；NULL=无 */
    sc_gpu_surface      cur_surface_id;
    int  frame_index;
} _sc_gpu_state;

extern _sc_gpu_state _sc_gpu;

/* 查表助手（校验代数） */
_sc_gpu_buffer_t*   _sc_gpu_lookup_buffer(uint32_t id);
_sc_gpu_image_t*    _sc_gpu_lookup_image(uint32_t id);
_sc_gpu_sampler_t*  _sc_gpu_lookup_sampler(uint32_t id);
_sc_gpu_shader_t*   _sc_gpu_lookup_shader(uint32_t id);
_sc_gpu_pipeline_t* _sc_gpu_lookup_pipeline(uint32_t id);
_sc_gpu_surface_t*  _sc_gpu_lookup_surface(uint32_t id);

#endif /* SC_GPU_INTERNAL_H */
