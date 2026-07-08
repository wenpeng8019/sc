/* ============================================================
 * gfx.h —— sc 渲染层（绘制渲染技术）C API
 * ============================================================
 * 定位（对应 sokol_gfx 的渲染部分）：
 *   在 gpu 模块（utils/gpu，GPU 运行环境）之上做**驱动 GPU 硬件、
 *   执行 scc 编译转义后的 GPU 代码（.sg → MSL/GLSL/SPIR-V + 反射
 *   清单）**的薄硬件访问层。不做场景图/材质/渲染图等上层概念。
 *
 * 与 gpu（env 层）的边界（软边界：本库编译期单向依赖 libgpu.a）：
 *   · sc_gfx_init 前须 sc_gpu_init 成功（后端种类跟随 gpu）
 *   · 一次性消费 sc_gpu_device()；交换链 pass 每帧消费
 *     sc_gpu_frame_acquire()；commit 末尾调 sc_gpu_frame_end()
 *   · 像素格式沿用 sc_gpu_pixel_format（两层共同词汇）
 *
 * 与 scc 的整合（核心）：
 *   sc_gfx_shader_desc 直接消费 scc 编译 .sg 的产物：
 *     · 各阶段目标代码 blob（Metal 吃 MSL，GL 吃 GLSL）
 *     · 反射清单 JSON——运行时据此自动建立管线绑定。
 *
 * 资源模型（参考 sokol_gfx 的句柄 + desc 模型）：
 *   buffer / image / sampler / shader / pipeline 五类资源，
 *   32 位句柄（池索引 + 代数），desc 一次性描述、不可变创建。
 *
 * 帧模型：
 *   begin_pass（附件全空 = 交换链，渲染到 gpu 当前 surface）
 *   → apply_pipeline/bindings/uniforms → draw / dispatch
 *   → end_pass → commit（呈现并收尾帧）。
 *
 * 函数名带 sc_ 前缀以匹配 sc 侧 @fnc name:: 约定（gfx.sc 里去前缀）。
 * ============================================================ */

#ifndef SC_GFX_H
#define SC_GFX_H

#include "../gpu/gpu.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄 ------------------------------------------------ */
/* 32 位不透明句柄：低 16 位池索引，高 16 位代数（防悬垂）。0 = 无效。
 * 纯 u32 typedef：与 sc 侧 FFI 的 u4 直接对应。 */

typedef uint32_t sc_gfx_buffer;
typedef uint32_t sc_gfx_image;
typedef uint32_t sc_gfx_sampler;
typedef uint32_t sc_gfx_shader;
typedef uint32_t sc_gfx_pipeline;

/* ---- 通用 ------------------------------------------------ */

typedef struct sc_gfx_range {
    const void* ptr;
    size_t      size;
} sc_gfx_range;

/* ---- 枚举 ------------------------------------------------ */
/* 像素格式用 sc_gpu_pixel_format（gpu.h，两层共同词汇）。 */

typedef enum sc_gfx_buffer_kind {
    SC_GFX_BUFFERKIND_DEFAULT = 0,     /* = VERTEX */
    SC_GFX_BUFFERKIND_VERTEX,
    SC_GFX_BUFFERKIND_INDEX,
    SC_GFX_BUFFERKIND_STORAGE,
} sc_gfx_buffer_kind;

typedef enum sc_gfx_usage {
    SC_GFX_USAGE_DEFAULT = 0,          /* = IMMUTABLE */
    SC_GFX_USAGE_IMMUTABLE,            /* 创建时提供数据，不可更新 */
    SC_GFX_USAGE_DYNAMIC,              /* 偶尔 update */
    SC_GFX_USAGE_STREAM,               /* 每帧 update/append */
} sc_gfx_usage;

typedef enum sc_gfx_image_kind {
    SC_GFX_IMAGEKIND_DEFAULT = 0,      /* = 2D */
    SC_GFX_IMAGEKIND_2D,
    SC_GFX_IMAGEKIND_CUBE,
    SC_GFX_IMAGEKIND_3D,
    SC_GFX_IMAGEKIND_ARRAY,
} sc_gfx_image_kind;

typedef enum sc_gfx_filter {
    SC_GFX_FILTER_DEFAULT = 0,         /* = NEAREST */
    SC_GFX_FILTER_NEAREST,
    SC_GFX_FILTER_LINEAR,
} sc_gfx_filter;

typedef enum sc_gfx_wrap {
    SC_GFX_WRAP_DEFAULT = 0,           /* = REPEAT */
    SC_GFX_WRAP_REPEAT,
    SC_GFX_WRAP_CLAMP,
    SC_GFX_WRAP_MIRROR,
    SC_GFX_WRAP_BORDER,                /* 超出取 border_color */
} sc_gfx_wrap;

typedef enum sc_gfx_border_color {
    SC_GFX_BORDERCOLOR_DEFAULT = 0,    /* = 透明黑 */
    SC_GFX_BORDERCOLOR_TRANSPARENT_BLACK,
    SC_GFX_BORDERCOLOR_OPAQUE_BLACK,
    SC_GFX_BORDERCOLOR_OPAQUE_WHITE,
} sc_gfx_border_color;

typedef enum sc_gfx_vertex_format {
    SC_GFX_VERTEXFORMAT_INVALID = 0,
    SC_GFX_VERTEXFORMAT_FLOAT,
    SC_GFX_VERTEXFORMAT_FLOAT2,
    SC_GFX_VERTEXFORMAT_FLOAT3,
    SC_GFX_VERTEXFORMAT_FLOAT4,
    SC_GFX_VERTEXFORMAT_BYTE4,
    SC_GFX_VERTEXFORMAT_BYTE4N,
    SC_GFX_VERTEXFORMAT_UBYTE4,
    SC_GFX_VERTEXFORMAT_UBYTE4N,
    SC_GFX_VERTEXFORMAT_SHORT2,
    SC_GFX_VERTEXFORMAT_SHORT2N,
    SC_GFX_VERTEXFORMAT_SHORT4,
    SC_GFX_VERTEXFORMAT_SHORT4N,
    SC_GFX_VERTEXFORMAT_USHORT2,
    SC_GFX_VERTEXFORMAT_USHORT2N,
    SC_GFX_VERTEXFORMAT_USHORT4,
    SC_GFX_VERTEXFORMAT_USHORT4N,
    SC_GFX_VERTEXFORMAT_HALF2,
    SC_GFX_VERTEXFORMAT_HALF4,
    SC_GFX_VERTEXFORMAT_UINT10N2,
    SC_GFX_VERTEXFORMAT_UINT,
    SC_GFX_VERTEXFORMAT_COUNT
} sc_gfx_vertex_format;

typedef enum sc_gfx_index_type {
    SC_GFX_INDEXTYPE_DEFAULT = 0,      /* = NONE */
    SC_GFX_INDEXTYPE_NONE,
    SC_GFX_INDEXTYPE_UINT16,
    SC_GFX_INDEXTYPE_UINT32,
} sc_gfx_index_type;

typedef enum sc_gfx_primitive {
    SC_GFX_PRIMITIVE_DEFAULT = 0,      /* = TRIANGLES */
    SC_GFX_PRIMITIVE_POINTS,
    SC_GFX_PRIMITIVE_LINES,
    SC_GFX_PRIMITIVE_LINE_STRIP,
    SC_GFX_PRIMITIVE_TRIANGLES,
    SC_GFX_PRIMITIVE_TRIANGLE_STRIP,
} sc_gfx_primitive;

typedef enum sc_gfx_cull {
    SC_GFX_CULL_DEFAULT = 0,           /* = NONE */
    SC_GFX_CULL_NONE,
    SC_GFX_CULL_FRONT,
    SC_GFX_CULL_BACK,
} sc_gfx_cull;

typedef enum sc_gfx_winding {
    SC_GFX_WINDING_DEFAULT = 0,        /* = CCW */
    SC_GFX_WINDING_CCW,
    SC_GFX_WINDING_CW,
} sc_gfx_winding;

typedef enum sc_gfx_compare {
    SC_GFX_COMPARE_DEFAULT = 0,        /* = ALWAYS */
    SC_GFX_COMPARE_NEVER,
    SC_GFX_COMPARE_LESS,
    SC_GFX_COMPARE_EQUAL,
    SC_GFX_COMPARE_LESS_EQUAL,
    SC_GFX_COMPARE_GREATER,
    SC_GFX_COMPARE_NOT_EQUAL,
    SC_GFX_COMPARE_GREATER_EQUAL,
    SC_GFX_COMPARE_ALWAYS,
} sc_gfx_compare;

typedef enum sc_gfx_blend_factor {
    SC_GFX_BLEND_DEFAULT = 0,          /* src=ONE dst=ZERO */
    SC_GFX_BLEND_ZERO,
    SC_GFX_BLEND_ONE,
    SC_GFX_BLEND_SRC_COLOR,
    SC_GFX_BLEND_ONE_MINUS_SRC_COLOR,
    SC_GFX_BLEND_SRC_ALPHA,
    SC_GFX_BLEND_ONE_MINUS_SRC_ALPHA,
    SC_GFX_BLEND_DST_COLOR,
    SC_GFX_BLEND_ONE_MINUS_DST_COLOR,
    SC_GFX_BLEND_DST_ALPHA,
    SC_GFX_BLEND_ONE_MINUS_DST_ALPHA,
    SC_GFX_BLEND_SRC_ALPHA_SATURATED,
    SC_GFX_BLEND_BLEND_COLOR,            /* 管线 blend_color 常量 */
    SC_GFX_BLEND_ONE_MINUS_BLEND_COLOR,
} sc_gfx_blend_factor;

typedef enum sc_gfx_blend_op {
    SC_GFX_BLENDOP_DEFAULT = 0,        /* = ADD */
    SC_GFX_BLENDOP_ADD,
    SC_GFX_BLENDOP_SUBTRACT,
    SC_GFX_BLENDOP_REVERSE_SUBTRACT,
    SC_GFX_BLENDOP_MIN,
    SC_GFX_BLENDOP_MAX,
} sc_gfx_blend_op;

typedef enum sc_gfx_stencil_op {
    SC_GFX_STENCILOP_DEFAULT = 0,      /* = KEEP */
    SC_GFX_STENCILOP_KEEP,
    SC_GFX_STENCILOP_ZERO,
    SC_GFX_STENCILOP_REPLACE,
    SC_GFX_STENCILOP_INCR_CLAMP,
    SC_GFX_STENCILOP_DECR_CLAMP,
    SC_GFX_STENCILOP_INVERT,
    SC_GFX_STENCILOP_INCR_WRAP,
    SC_GFX_STENCILOP_DECR_WRAP,
} sc_gfx_stencil_op;

typedef enum sc_gfx_load_action {
    SC_GFX_LOADACTION_DEFAULT = 0,     /* = CLEAR */
    SC_GFX_LOADACTION_CLEAR,
    SC_GFX_LOADACTION_LOAD,
    SC_GFX_LOADACTION_DONTCARE,
} sc_gfx_load_action;

typedef enum sc_gfx_store_action {
    SC_GFX_STOREACTION_DEFAULT = 0,    /* = STORE；带 resolve 附件时 = RESOLVE */
    SC_GFX_STOREACTION_STORE,
    SC_GFX_STOREACTION_DONTCARE,
    SC_GFX_STOREACTION_RESOLVE,        /* MSAA 解析到 resolve 附件，不保留 MSAA 本体 */
    SC_GFX_STOREACTION_STORE_RESOLVE,  /* 解析且保留 MSAA 本体 */
} sc_gfx_store_action;

typedef enum sc_gfx_shader_stage {
    SC_GFX_STAGE_VERTEX = 0,
    SC_GFX_STAGE_FRAGMENT,
    SC_GFX_STAGE_COMPUTE,
    SC_GFX_STAGE_COUNT
} sc_gfx_shader_stage;

typedef enum sc_gfx_resource_state {
    SC_GFX_RESOURCESTATE_INITIAL = 0,
    SC_GFX_RESOURCESTATE_VALID,
    SC_GFX_RESOURCESTATE_FAILED,
    SC_GFX_RESOURCESTATE_INVALID,
} sc_gfx_resource_state;

/* ---- 限制常量 -------------------------------------------- */

enum {
    SC_GFX_MAX_COLOR_ATTACHMENTS = 4,
    SC_GFX_MAX_VERTEX_BUFFERS    = 8,
    SC_GFX_MAX_VERTEX_ATTRS      = 16,
    SC_GFX_MAX_UNIFORM_BLOCKS    = 8,   /* 每阶段 */
    SC_GFX_MAX_IMAGES            = 12,  /* 每阶段绑定纹理数 */
    SC_GFX_MAX_SAMPLERS          = 8,
    SC_GFX_MAX_STORAGE_BUFFERS   = 8,
    SC_GFX_MAX_MIPMAPS           = 16,
    SC_GFX_MAX_INFLIGHT_FRAMES   = 2,   /* CPU/GPU 并行帧数 */
};

/* ---- 资源描述 -------------------------------------------- */

typedef struct sc_gfx_buffer_desc {
    sc_gfx_buffer_kind kind;
    sc_gfx_usage       usage;
    size_t             size;      /* 字节；data.size 为 0 时必填 */
    sc_gfx_range       data;      /* 初始数据（IMMUTABLE 必填） */
    const char*        label;
} sc_gfx_buffer_desc;

typedef struct sc_gfx_image_data {
    /* [face/slice][mip]；2D 只用 [0][mip] */
    sc_gfx_range subimage[6][SC_GFX_MAX_MIPMAPS];
} sc_gfx_image_data;

typedef struct sc_gfx_image_desc {
    sc_gfx_image_kind   kind;
    int                 width;
    int                 height;
    int                 slices;        /* 3D 深度 / 数组层数；默认 1 */
    int                 mip_count;     /* 默认 1 */
    sc_gpu_pixel_format format;        /* 默认 RGBA8 */
    sc_gfx_usage        usage;
    int                 render_target; /* 可作 pass 附件 */
    int                 sample_count;  /* MSAA；默认 1 */
    sc_gfx_image_data   data;
    const char*         label;
} sc_gfx_image_desc;

typedef struct sc_gfx_sampler_desc {
    sc_gfx_filter  min_filter;
    sc_gfx_filter  mag_filter;
    sc_gfx_filter  mipmap_filter;
    sc_gfx_wrap    wrap_u;
    sc_gfx_wrap    wrap_v;
    sc_gfx_wrap    wrap_w;
    float          min_lod;       /* 默认 0 */
    float          max_lod;       /* 0 = 不限 */
    int            max_anisotropy; /* 0/1 = 关；最大 16 */
    sc_gfx_border_color border_color; /* WRAP_BORDER 时生效 */
    sc_gfx_compare compare;       /* 深度比较采样；默认关（ALWAYS=关） */
    const char*    label;
} sc_gfx_sampler_desc;

/* 着色器：核心整合点 —— 直接消费 scc 编译 .sg 的产物。
 *   code   ：当前后端的目标代码。Metal=MSL 文本、GL=GLSL 文本。
 *            文本无须 NUL 结尾（按 size 取）。
 *   entry  ：入口函数名。scc 产物 MSL 为重命名后的入口（如 vs_main）；
 *            GLSL 恒为 main（GL 后端忽略）。NULL = "main"。
 *   reflect_json：scc 反射清单（<stem>.reflect.json 的内容）。提供时
 *            uniform 块 / 顶点属性 / sampler 的绑定信息自动解析。 */
typedef struct sc_gfx_shader_stage_desc {
    sc_gfx_range code;
    const char*  entry;
} sc_gfx_shader_stage_desc;

/* 手工绑定描述（无反射清单时使用；有清单则忽略） */
typedef struct sc_gfx_uniform_block_desc {
    int         stage;      /* sc_gfx_shader_stage */
    int         slot;       /* binding 槽 */
    size_t      size;       /* std140 字节大小 */
    const char* name;       /* GL 低版本按名绑定用 */
} sc_gfx_uniform_block_desc;

typedef struct sc_gfx_image_slot_desc {
    int         stage;
    int         slot;
    const char* name;
} sc_gfx_image_slot_desc;

typedef struct sc_gfx_shader_desc {
    sc_gfx_shader_stage_desc vs;
    sc_gfx_shader_stage_desc fs;
    sc_gfx_shader_stage_desc cs;      /* 计算着色器（与 vs/fs 互斥） */
    const char*              reflect_json;
    /* 手工绑定（reflect_json 为 NULL 时用） */
    sc_gfx_uniform_block_desc uniform_blocks[SC_GFX_MAX_UNIFORM_BLOCKS * 2];
    sc_gfx_image_slot_desc    images[SC_GFX_MAX_IMAGES];
    sc_gfx_image_slot_desc    samplers[SC_GFX_MAX_SAMPLERS];
    const char*              label;
} sc_gfx_shader_desc;

/* 顶点布局 */
typedef struct sc_gfx_vertex_buffer_layout {
    int stride;        /* 0 = 按属性自动推导 */
    int step_per_instance; /* 0=逐顶点 1=逐实例 */
} sc_gfx_vertex_buffer_layout;

typedef struct sc_gfx_vertex_attr {
    int                  buffer_index; /* 顶点缓冲槽 */
    int                  offset;       /* 0 且 stride 自动时按序累加 */
    sc_gfx_vertex_format format;
} sc_gfx_vertex_attr;

typedef struct sc_gfx_blend_state {
    int                 enabled;
    sc_gfx_blend_factor src_factor_rgb;
    sc_gfx_blend_factor dst_factor_rgb;
    sc_gfx_blend_op     op_rgb;
    sc_gfx_blend_factor src_factor_alpha;
    sc_gfx_blend_factor dst_factor_alpha;
    sc_gfx_blend_op     op_alpha;
} sc_gfx_blend_state;

typedef struct sc_gfx_depth_state {
    sc_gpu_pixel_format format;        /* 附件深度格式；NONE=无深度 */
    sc_gfx_compare      compare;
    int                 write_enabled;
    float               bias;              /* 深度偏置（shadow map 等） */
    float               bias_slope_scale;
    float               bias_clamp;
} sc_gfx_depth_state;

typedef struct sc_gfx_stencil_face_state {
    sc_gfx_compare    compare;
    sc_gfx_stencil_op fail_op;
    sc_gfx_stencil_op depth_fail_op;
    sc_gfx_stencil_op pass_op;
} sc_gfx_stencil_face_state;

typedef struct sc_gfx_stencil_state {
    int                       enabled;
    sc_gfx_stencil_face_state front;
    sc_gfx_stencil_face_state back;
    uint8_t                   read_mask;   /* 0 = 0xFF */
    uint8_t                   write_mask;  /* 0 = 0xFF */
    uint8_t                   ref;
} sc_gfx_stencil_state;

typedef struct sc_gfx_color_target_state {
    sc_gpu_pixel_format format;
    sc_gfx_blend_state  blend;
    int                 write_mask;    /* 0 = RGBA 全写 */
} sc_gfx_color_target_state;

typedef struct sc_gfx_pipeline_desc {
    sc_gfx_shader shader;
    sc_gfx_vertex_buffer_layout buffers[SC_GFX_MAX_VERTEX_BUFFERS];
    sc_gfx_vertex_attr          attrs[SC_GFX_MAX_VERTEX_ATTRS];
    sc_gfx_primitive  primitive;
    sc_gfx_index_type index_type;
    sc_gfx_cull       cull;
    sc_gfx_winding    winding;
    sc_gfx_depth_state   depth;
    sc_gfx_stencil_state stencil;
    int               color_count;    /* 0 = 1 */
    sc_gfx_color_target_state colors[SC_GFX_MAX_COLOR_ATTACHMENTS];
    float             blend_color[4]; /* BLEND_*_BLEND_COLOR 因子用 */
    int               alpha_to_coverage;
    int               sample_count;   /* 0 = 交换链（gpu 当前 surface）一致 */
    int               compute;        /* 计算管线（shader 须含 cs） */
    int               threads_per_group[3]; /* 计算线程组尺寸；0=1 */
    const char*       label;
} sc_gfx_pipeline_desc;

/* ---- pass ------------------------------------------------ */

typedef struct sc_gfx_color_attachment_action {
    sc_gfx_load_action  load;
    sc_gfx_store_action store;
    float               clear[4];     /* RGBA */
} sc_gfx_color_attachment_action;

typedef struct sc_gfx_depth_attachment_action {
    sc_gfx_load_action  load;
    sc_gfx_store_action store;
    float               clear_depth;   /* 默认 1.0 */
    uint8_t             clear_stencil;
} sc_gfx_depth_attachment_action;

typedef struct sc_gfx_pass_action {
    sc_gfx_color_attachment_action colors[SC_GFX_MAX_COLOR_ATTACHMENTS];
    sc_gfx_depth_attachment_action depth;
} sc_gfx_pass_action;

typedef struct sc_gfx_attachment {
    sc_gfx_image image;
    int          mip;
    int          slice;
} sc_gfx_attachment;

/* 附件全空 = 交换链 pass（渲染到 gpu 当前 surface）；否则离屏 pass。
 * MSAA 离屏：colors[i] 指向 sample_count>1 的图像，resolves[i] 指向
 * 同尺寸 sample_count=1 的图像，store 默认变 RESOLVE。
 * compute 非 0 = 计算 pass（无附件，只可 dispatch）。 */
typedef struct sc_gfx_pass {
    sc_gfx_pass_action action;
    sc_gfx_attachment  colors[SC_GFX_MAX_COLOR_ATTACHMENTS];
    sc_gfx_attachment  resolves[SC_GFX_MAX_COLOR_ATTACHMENTS]; /* MSAA 解析目标 */
    sc_gfx_attachment  depth_stencil;
    int                compute;
    const char*        label;
} sc_gfx_pass;

/* ---- 绑定 ------------------------------------------------ */

typedef struct sc_gfx_stage_bindings {
    sc_gfx_image   images[SC_GFX_MAX_IMAGES];
    sc_gfx_sampler samplers[SC_GFX_MAX_SAMPLERS];
    sc_gfx_buffer  storage_buffers[SC_GFX_MAX_STORAGE_BUFFERS];
} sc_gfx_stage_bindings;

typedef struct sc_gfx_bindings {
    sc_gfx_buffer vertex_buffers[SC_GFX_MAX_VERTEX_BUFFERS];
    int           vertex_buffer_offsets[SC_GFX_MAX_VERTEX_BUFFERS];
    sc_gfx_buffer index_buffer;
    int           index_buffer_offset;
    sc_gfx_stage_bindings vs;
    sc_gfx_stage_bindings fs;
    sc_gfx_stage_bindings cs;
} sc_gfx_bindings;

/* ---- 初始化 ---------------------------------------------- */

typedef struct sc_gfx_desc {
    int buffer_pool_size;    /* 各资源池容量；默认 128 */
    int image_pool_size;     /* 默认 128 */
    int sampler_pool_size;   /* 默认 64 */
    int shader_pool_size;    /* 默认 64 */
    int pipeline_pool_size;  /* 默认 64 */
} sc_gfx_desc;

/* ---- API：生命周期 ---------------------------------------- */

int  sc_gfx_init(const sc_gfx_desc* desc);       /* 1 成功 / 0 失败；须先 sc_gpu_init */
void sc_gfx_shutdown(void);
int  sc_gfx_isvalid(void);

/* ---- API：能力查询 ----------------------------------------- */

typedef struct sc_gfx_pixelformat_info {
    int sample;   /* 可采样 */
    int filter;   /* 可线性过滤 */
    int render;   /* 可作渲染附件 */
    int blend;    /* 可混合 */
    int msaa;     /* 可 MSAA */
    int depth;    /* 深度/模板格式 */
} sc_gfx_pixelformat_info;

sc_gfx_pixelformat_info sc_gfx_query_pixelformat(sc_gpu_pixel_format fmt);

/* ---- API：资源 -------------------------------------------- */

sc_gfx_buffer   sc_gfx_make_buffer(const sc_gfx_buffer_desc* desc);
sc_gfx_image    sc_gfx_make_image(const sc_gfx_image_desc* desc);
sc_gfx_sampler  sc_gfx_make_sampler(const sc_gfx_sampler_desc* desc);
sc_gfx_shader   sc_gfx_make_shader(const sc_gfx_shader_desc* desc);
sc_gfx_pipeline sc_gfx_make_pipeline(const sc_gfx_pipeline_desc* desc);

void sc_gfx_destroy_buffer(sc_gfx_buffer buf);
void sc_gfx_destroy_image(sc_gfx_image img);
void sc_gfx_destroy_sampler(sc_gfx_sampler smp);
void sc_gfx_destroy_shader(sc_gfx_shader shd);
void sc_gfx_destroy_pipeline(sc_gfx_pipeline pip);

void sc_gfx_update_buffer(sc_gfx_buffer buf, const sc_gfx_range* data);
int  sc_gfx_append_buffer(sc_gfx_buffer buf, const sc_gfx_range* data); /* 返回偏移 */
void sc_gfx_update_image(sc_gfx_image img, const sc_gfx_image_data* data);

int  sc_gfx_query_buffer_state(sc_gfx_buffer buf);   /* sc_gfx_resource_state */
int  sc_gfx_query_image_state(sc_gfx_image img);
int  sc_gfx_query_shader_state(sc_gfx_shader shd);
int  sc_gfx_query_pipeline_state(sc_gfx_pipeline pip);

/* ---- API：帧 ---------------------------------------------- */

void sc_gfx_begin_pass(const sc_gfx_pass* pass);
void sc_gfx_apply_viewport(int x, int y, int w, int h, int origin_top_left);
void sc_gfx_apply_scissor(int x, int y, int w, int h, int origin_top_left);
void sc_gfx_apply_pipeline(sc_gfx_pipeline pip);
void sc_gfx_apply_bindings(const sc_gfx_bindings* bindings);
/* stage: sc_gfx_shader_stage；slot: 反射清单里的 binding */
void sc_gfx_apply_uniforms(int stage, int slot, const void* data, uint64_t size);
void sc_gfx_draw(int base_element, int element_count, int instance_count);
void sc_gfx_dispatch(int groups_x, int groups_y, int groups_z);
void sc_gfx_end_pass(void);
void sc_gfx_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* SC_GFX_H */
