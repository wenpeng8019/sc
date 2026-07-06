/* ============================================================
 * gpu.h —— sc GPU 设备操作统一接口（C API）
 * ============================================================
 * 定位（区别于常见 gfx 抽象层）：
 *   本模块不追求"消除平台差异性的统一图形抽象"，而是**驱动 GPU 硬件、
 *   执行 scc 编译转义后的 GPU 代码（.sg → MSL/GLSL/SPIR-V + 反射清单）**
 *   的运行时。概念对应 gfx-hal / sokol_gfx 的"薄硬件访问层"。
 *
 * 多后端机制（同 wsi/glfw）：
 *   一个 libgpu.a 可同时编入多个后端，运行时按平台默认或显式 hint 选择：
 *     macOS   → Metal（默认） / GL
 *     Linux   → GL（默认）   / Vulkan（远期）
 *     Windows → GL（默认）   / D3D（远期）
 *
 * 与 scc 的整合（核心）：
 *   sc_gpu_shader_desc 直接消费 scc 编译 .sg 的产物：
 *     · 各阶段目标代码 blob（Metal 后端吃 MSL，GL 后端吃 GLSL，
 *       Vulkan 后端吃 SPIR-V——按当前后端取对应 blob）
 *     · 反射清单 JSON（uniform 块 set/binding/size/offset、顶点属性
 *       location、sampler 绑定）——运行时据此自动建立管线绑定。
 *
 * 资源模型（参考 sokol_gfx 的句柄 + desc 模型）：
 *   buffer / image / sampler / shader / pipeline / surface 六类资源，
 *   32 位句柄（池索引 + 代数），desc 结构一次性描述、不可变创建。
 *   surface = 呈现目标（交换链），make_current 切换多窗口渲染目标。
 *
 * 帧模型：
 *   begin_pass（交换链或离屏）→ apply_pipeline/bindings/uniforms
 *   → draw / dispatch → end_pass → commit。
 *
 * 函数名带 sc_ 前缀以匹配 sc 侧 @fnc name:: 约定（gpu.sc 里去前缀）。
 * ============================================================ */

#ifndef SC_GPU_H
#define SC_GPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄 ------------------------------------------------ */
/* 32 位不透明句柄：低 16 位池索引，高 16 位代数（防悬垂）。0 = 无效。
 * 纯 u32 typedef（非单成员结构体）：与 sc 侧 FFI 的 u4 直接对应。 */

typedef uint32_t sc_gpu_buffer;
typedef uint32_t sc_gpu_image;
typedef uint32_t sc_gpu_sampler;
typedef uint32_t sc_gpu_shader;
typedef uint32_t sc_gpu_pipeline;
typedef uint32_t sc_gpu_surface;   /* 呈现目标（交换链）；make_current 切换 */

/* ---- 通用 ------------------------------------------------ */

typedef struct sc_gpu_range {
    const void* ptr;
    size_t      size;
} sc_gpu_range;

/* ---- 枚举 ------------------------------------------------ */

typedef enum sc_gpu_backend {
    SC_GPU_BACKEND_DEFAULT = 0,   /* 平台默认：mac=Metal, linux/win=GL */
    SC_GPU_BACKEND_METAL,
    SC_GPU_BACKEND_GL,
    SC_GPU_BACKEND_NULL,          /* 空后端（无硬件/测试） */
} sc_gpu_backend;

typedef enum sc_gpu_pixel_format {
    SC_GPU_PIXELFORMAT_DEFAULT = 0,   /* 交换链默认（BGRA8） */
    SC_GPU_PIXELFORMAT_NONE,
    SC_GPU_PIXELFORMAT_R8,
    SC_GPU_PIXELFORMAT_R16F,
    SC_GPU_PIXELFORMAT_R32F,
    SC_GPU_PIXELFORMAT_RG8,
    SC_GPU_PIXELFORMAT_RG16F,
    SC_GPU_PIXELFORMAT_RG32F,
    SC_GPU_PIXELFORMAT_RGBA8,
    SC_GPU_PIXELFORMAT_SRGB8A8,
    SC_GPU_PIXELFORMAT_BGRA8,
    SC_GPU_PIXELFORMAT_RGB10A2,
    SC_GPU_PIXELFORMAT_RGBA16F,
    SC_GPU_PIXELFORMAT_RGBA32F,
    SC_GPU_PIXELFORMAT_DEPTH,          /* 32F 深度 */
    SC_GPU_PIXELFORMAT_DEPTH_STENCIL,  /* 32F 深度 + 8 模板 */
    SC_GPU_PIXELFORMAT_COUNT
} sc_gpu_pixel_format;

typedef enum sc_gpu_buffer_kind {
    SC_GPU_BUFFERKIND_DEFAULT = 0,     /* = VERTEX */
    SC_GPU_BUFFERKIND_VERTEX,
    SC_GPU_BUFFERKIND_INDEX,
    SC_GPU_BUFFERKIND_STORAGE,
} sc_gpu_buffer_kind;

typedef enum sc_gpu_usage {
    SC_GPU_USAGE_DEFAULT = 0,          /* = IMMUTABLE */
    SC_GPU_USAGE_IMMUTABLE,            /* 创建时提供数据，不可更新 */
    SC_GPU_USAGE_DYNAMIC,              /* 偶尔 update */
    SC_GPU_USAGE_STREAM,               /* 每帧 update/append */
} sc_gpu_usage;

typedef enum sc_gpu_image_kind {
    SC_GPU_IMAGEKIND_DEFAULT = 0,      /* = 2D */
    SC_GPU_IMAGEKIND_2D,
    SC_GPU_IMAGEKIND_CUBE,
    SC_GPU_IMAGEKIND_3D,
    SC_GPU_IMAGEKIND_ARRAY,
} sc_gpu_image_kind;

typedef enum sc_gpu_filter {
    SC_GPU_FILTER_DEFAULT = 0,         /* = NEAREST */
    SC_GPU_FILTER_NEAREST,
    SC_GPU_FILTER_LINEAR,
} sc_gpu_filter;

typedef enum sc_gpu_wrap {
    SC_GPU_WRAP_DEFAULT = 0,           /* = REPEAT */
    SC_GPU_WRAP_REPEAT,
    SC_GPU_WRAP_CLAMP,
    SC_GPU_WRAP_MIRROR,
    SC_GPU_WRAP_BORDER,                /* 超出取 border_color */
} sc_gpu_wrap;

typedef enum sc_gpu_border_color {
    SC_GPU_BORDERCOLOR_DEFAULT = 0,    /* = 透明黑 */
    SC_GPU_BORDERCOLOR_TRANSPARENT_BLACK,
    SC_GPU_BORDERCOLOR_OPAQUE_BLACK,
    SC_GPU_BORDERCOLOR_OPAQUE_WHITE,
} sc_gpu_border_color;

typedef enum sc_gpu_vertex_format {
    SC_GPU_VERTEXFORMAT_INVALID = 0,
    SC_GPU_VERTEXFORMAT_FLOAT,
    SC_GPU_VERTEXFORMAT_FLOAT2,
    SC_GPU_VERTEXFORMAT_FLOAT3,
    SC_GPU_VERTEXFORMAT_FLOAT4,
    SC_GPU_VERTEXFORMAT_BYTE4,
    SC_GPU_VERTEXFORMAT_BYTE4N,
    SC_GPU_VERTEXFORMAT_UBYTE4,
    SC_GPU_VERTEXFORMAT_UBYTE4N,
    SC_GPU_VERTEXFORMAT_SHORT2,
    SC_GPU_VERTEXFORMAT_SHORT2N,
    SC_GPU_VERTEXFORMAT_SHORT4,
    SC_GPU_VERTEXFORMAT_SHORT4N,
    SC_GPU_VERTEXFORMAT_USHORT2,
    SC_GPU_VERTEXFORMAT_USHORT2N,
    SC_GPU_VERTEXFORMAT_USHORT4,
    SC_GPU_VERTEXFORMAT_USHORT4N,
    SC_GPU_VERTEXFORMAT_HALF2,
    SC_GPU_VERTEXFORMAT_HALF4,
    SC_GPU_VERTEXFORMAT_UINT10N2,
    SC_GPU_VERTEXFORMAT_UINT,
    SC_GPU_VERTEXFORMAT_COUNT
} sc_gpu_vertex_format;

typedef enum sc_gpu_index_type {
    SC_GPU_INDEXTYPE_DEFAULT = 0,      /* = NONE */
    SC_GPU_INDEXTYPE_NONE,
    SC_GPU_INDEXTYPE_UINT16,
    SC_GPU_INDEXTYPE_UINT32,
} sc_gpu_index_type;

typedef enum sc_gpu_primitive {
    SC_GPU_PRIMITIVE_DEFAULT = 0,      /* = TRIANGLES */
    SC_GPU_PRIMITIVE_POINTS,
    SC_GPU_PRIMITIVE_LINES,
    SC_GPU_PRIMITIVE_LINE_STRIP,
    SC_GPU_PRIMITIVE_TRIANGLES,
    SC_GPU_PRIMITIVE_TRIANGLE_STRIP,
} sc_gpu_primitive;

typedef enum sc_gpu_cull {
    SC_GPU_CULL_DEFAULT = 0,           /* = NONE */
    SC_GPU_CULL_NONE,
    SC_GPU_CULL_FRONT,
    SC_GPU_CULL_BACK,
} sc_gpu_cull;

typedef enum sc_gpu_winding {
    SC_GPU_WINDING_DEFAULT = 0,        /* = CCW */
    SC_GPU_WINDING_CCW,
    SC_GPU_WINDING_CW,
} sc_gpu_winding;

typedef enum sc_gpu_compare {
    SC_GPU_COMPARE_DEFAULT = 0,        /* = ALWAYS */
    SC_GPU_COMPARE_NEVER,
    SC_GPU_COMPARE_LESS,
    SC_GPU_COMPARE_EQUAL,
    SC_GPU_COMPARE_LESS_EQUAL,
    SC_GPU_COMPARE_GREATER,
    SC_GPU_COMPARE_NOT_EQUAL,
    SC_GPU_COMPARE_GREATER_EQUAL,
    SC_GPU_COMPARE_ALWAYS,
} sc_gpu_compare;

typedef enum sc_gpu_blend_factor {
    SC_GPU_BLEND_DEFAULT = 0,          /* src=ONE dst=ZERO */
    SC_GPU_BLEND_ZERO,
    SC_GPU_BLEND_ONE,
    SC_GPU_BLEND_SRC_COLOR,
    SC_GPU_BLEND_ONE_MINUS_SRC_COLOR,
    SC_GPU_BLEND_SRC_ALPHA,
    SC_GPU_BLEND_ONE_MINUS_SRC_ALPHA,
    SC_GPU_BLEND_DST_COLOR,
    SC_GPU_BLEND_ONE_MINUS_DST_COLOR,
    SC_GPU_BLEND_DST_ALPHA,
    SC_GPU_BLEND_ONE_MINUS_DST_ALPHA,
    SC_GPU_BLEND_SRC_ALPHA_SATURATED,
    SC_GPU_BLEND_BLEND_COLOR,            /* 管线 blend_color 常量 */
    SC_GPU_BLEND_ONE_MINUS_BLEND_COLOR,
} sc_gpu_blend_factor;

typedef enum sc_gpu_blend_op {
    SC_GPU_BLENDOP_DEFAULT = 0,        /* = ADD */
    SC_GPU_BLENDOP_ADD,
    SC_GPU_BLENDOP_SUBTRACT,
    SC_GPU_BLENDOP_REVERSE_SUBTRACT,
    SC_GPU_BLENDOP_MIN,
    SC_GPU_BLENDOP_MAX,
} sc_gpu_blend_op;

typedef enum sc_gpu_stencil_op {
    SC_GPU_STENCILOP_DEFAULT = 0,      /* = KEEP */
    SC_GPU_STENCILOP_KEEP,
    SC_GPU_STENCILOP_ZERO,
    SC_GPU_STENCILOP_REPLACE,
    SC_GPU_STENCILOP_INCR_CLAMP,
    SC_GPU_STENCILOP_DECR_CLAMP,
    SC_GPU_STENCILOP_INVERT,
    SC_GPU_STENCILOP_INCR_WRAP,
    SC_GPU_STENCILOP_DECR_WRAP,
} sc_gpu_stencil_op;

typedef enum sc_gpu_load_action {
    SC_GPU_LOADACTION_DEFAULT = 0,     /* = CLEAR */
    SC_GPU_LOADACTION_CLEAR,
    SC_GPU_LOADACTION_LOAD,
    SC_GPU_LOADACTION_DONTCARE,
} sc_gpu_load_action;

typedef enum sc_gpu_store_action {
    SC_GPU_STOREACTION_DEFAULT = 0,    /* = STORE；带 resolve 附件时 = RESOLVE */
    SC_GPU_STOREACTION_STORE,
    SC_GPU_STOREACTION_DONTCARE,
    SC_GPU_STOREACTION_RESOLVE,        /* MSAA 解析到 resolve 附件，不保留 MSAA 本体 */
    SC_GPU_STOREACTION_STORE_RESOLVE,  /* 解析且保留 MSAA 本体 */
} sc_gpu_store_action;

typedef enum sc_gpu_shader_stage {
    SC_GPU_STAGE_VERTEX = 0,
    SC_GPU_STAGE_FRAGMENT,
    SC_GPU_STAGE_COMPUTE,
    SC_GPU_STAGE_COUNT
} sc_gpu_shader_stage;

typedef enum sc_gpu_resource_state {
    SC_GPU_RESOURCESTATE_INITIAL = 0,
    SC_GPU_RESOURCESTATE_VALID,
    SC_GPU_RESOURCESTATE_FAILED,
    SC_GPU_RESOURCESTATE_INVALID,
} sc_gpu_resource_state;

/* ---- 限制常量 -------------------------------------------- */

enum {
    SC_GPU_MAX_COLOR_ATTACHMENTS = 4,
    SC_GPU_MAX_VERTEX_BUFFERS    = 8,
    SC_GPU_MAX_VERTEX_ATTRS      = 16,
    SC_GPU_MAX_UNIFORM_BLOCKS    = 8,   /* 每阶段 */
    SC_GPU_MAX_IMAGES            = 12,  /* 每阶段绑定纹理数 */
    SC_GPU_MAX_SAMPLERS          = 8,
    SC_GPU_MAX_STORAGE_BUFFERS   = 8,
    SC_GPU_MAX_MIPMAPS           = 16,
    SC_GPU_MAX_INFLIGHT_FRAMES   = 2,   /* CPU/GPU 并行帧数 */
};

/* ---- 资源描述 -------------------------------------------- */

typedef struct sc_gpu_buffer_desc {
    sc_gpu_buffer_kind kind;
    sc_gpu_usage       usage;
    size_t             size;      /* 字节；data.size 为 0 时必填 */
    sc_gpu_range       data;      /* 初始数据（IMMUTABLE 必填） */
    const char*        label;
} sc_gpu_buffer_desc;

typedef struct sc_gpu_image_data {
    /* [face/slice][mip]；2D 只用 [0][mip] */
    sc_gpu_range subimage[6][SC_GPU_MAX_MIPMAPS];
} sc_gpu_image_data;

typedef struct sc_gpu_image_desc {
    sc_gpu_image_kind   kind;
    int                 width;
    int                 height;
    int                 slices;        /* 3D 深度 / 数组层数；默认 1 */
    int                 mip_count;     /* 默认 1 */
    sc_gpu_pixel_format format;        /* 默认 RGBA8 */
    sc_gpu_usage        usage;
    int                 render_target; /* 可作 pass 附件 */
    int                 sample_count;  /* MSAA；默认 1 */
    sc_gpu_image_data   data;
    const char*         label;
} sc_gpu_image_desc;

typedef struct sc_gpu_sampler_desc {
    sc_gpu_filter  min_filter;
    sc_gpu_filter  mag_filter;
    sc_gpu_filter  mipmap_filter;
    sc_gpu_wrap    wrap_u;
    sc_gpu_wrap    wrap_v;
    sc_gpu_wrap    wrap_w;
    float          min_lod;       /* 默认 0 */
    float          max_lod;       /* 0 = 不限 */
    int            max_anisotropy; /* 0/1 = 关；最大 16 */
    sc_gpu_border_color border_color; /* WRAP_BORDER 时生效 */
    sc_gpu_compare compare;       /* 深度比较采样；默认关（ALWAYS=关） */
    const char*    label;
} sc_gpu_sampler_desc;

/* 着色器：核心整合点 —— 直接消费 scc 编译 .sg 的产物。
 *   code   ：当前后端的目标代码。Metal=MSL 文本、GL=GLSL 文本、
 *            Vulkan=SPIR-V 二进制。文本无须 NUL 结尾（按 size 取）。
 *   entry  ：入口函数名。scc 产物 MSL 为重命名后的入口（如 vs_main）；
 *            GLSL 恒为 main。NULL = "main"。
 *   reflect_json：scc 反射清单（<stem>.reflect.json 的内容）。提供时
 *            uniform 块 / 顶点属性 / sampler 的绑定信息自动解析，
 *            无须手工填 uniform_blocks / images / samplers。 */
typedef struct sc_gpu_shader_stage_desc {
    sc_gpu_range code;
    const char*  entry;
} sc_gpu_shader_stage_desc;

/* 手工绑定描述（无反射清单时使用；有清单则忽略） */
typedef struct sc_gpu_uniform_block_desc {
    int         stage;      /* sc_gpu_shader_stage */
    int         slot;       /* binding 槽 */
    size_t      size;       /* std140 字节大小 */
    const char* name;       /* GL 低版本按名绑定用 */
} sc_gpu_uniform_block_desc;

typedef struct sc_gpu_image_slot_desc {
    int         stage;
    int         slot;
    const char* name;
} sc_gpu_image_slot_desc;

typedef struct sc_gpu_shader_desc {
    sc_gpu_shader_stage_desc vs;
    sc_gpu_shader_stage_desc fs;
    sc_gpu_shader_stage_desc cs;      /* 计算着色器（与 vs/fs 互斥） */
    const char*              reflect_json;
    /* 手工绑定（reflect_json 为 NULL 时用） */
    sc_gpu_uniform_block_desc uniform_blocks[SC_GPU_MAX_UNIFORM_BLOCKS * 2];
    sc_gpu_image_slot_desc    images[SC_GPU_MAX_IMAGES];
    sc_gpu_image_slot_desc    samplers[SC_GPU_MAX_SAMPLERS];
    const char*              label;
} sc_gpu_shader_desc;

/* 顶点布局 */
typedef struct sc_gpu_vertex_buffer_layout {
    int stride;        /* 0 = 按属性自动推导 */
    int step_per_instance; /* 0=逐顶点 1=逐实例 */
} sc_gpu_vertex_buffer_layout;

typedef struct sc_gpu_vertex_attr {
    int                  buffer_index; /* 顶点缓冲槽 */
    int                  offset;       /* 0 且 stride 自动时按序累加 */
    sc_gpu_vertex_format format;
} sc_gpu_vertex_attr;

typedef struct sc_gpu_blend_state {
    int                 enabled;
    sc_gpu_blend_factor src_factor_rgb;
    sc_gpu_blend_factor dst_factor_rgb;
    sc_gpu_blend_op     op_rgb;
    sc_gpu_blend_factor src_factor_alpha;
    sc_gpu_blend_factor dst_factor_alpha;
    sc_gpu_blend_op     op_alpha;
} sc_gpu_blend_state;

typedef struct sc_gpu_depth_state {
    sc_gpu_pixel_format format;        /* 附件深度格式；NONE=无深度 */
    sc_gpu_compare      compare;
    int                 write_enabled;
    float               bias;              /* 深度偏置（shadow map 等） */
    float               bias_slope_scale;
    float               bias_clamp;
} sc_gpu_depth_state;

typedef struct sc_gpu_stencil_face_state {
    sc_gpu_compare    compare;
    sc_gpu_stencil_op fail_op;
    sc_gpu_stencil_op depth_fail_op;
    sc_gpu_stencil_op pass_op;
} sc_gpu_stencil_face_state;

typedef struct sc_gpu_stencil_state {
    int                       enabled;
    sc_gpu_stencil_face_state front;
    sc_gpu_stencil_face_state back;
    uint8_t                   read_mask;   /* 0 = 0xFF */
    uint8_t                   write_mask;  /* 0 = 0xFF */
    uint8_t                   ref;
} sc_gpu_stencil_state;

typedef struct sc_gpu_color_target_state {
    sc_gpu_pixel_format format;
    sc_gpu_blend_state  blend;
    int                 write_mask;    /* 0 = RGBA 全写 */
} sc_gpu_color_target_state;

typedef struct sc_gpu_pipeline_desc {
    sc_gpu_shader shader;
    sc_gpu_vertex_buffer_layout buffers[SC_GPU_MAX_VERTEX_BUFFERS];
    sc_gpu_vertex_attr          attrs[SC_GPU_MAX_VERTEX_ATTRS];
    sc_gpu_primitive  primitive;
    sc_gpu_index_type index_type;
    sc_gpu_cull       cull;
    sc_gpu_winding    winding;
    sc_gpu_depth_state   depth;
    sc_gpu_stencil_state stencil;
    int               color_count;    /* 0 = 1 */
    sc_gpu_color_target_state colors[SC_GPU_MAX_COLOR_ATTACHMENTS];
    float             blend_color[4]; /* BLEND_*_BLEND_COLOR 因子用（当前后端恒定量） */
    int               alpha_to_coverage;
    int               sample_count;   /* 0 = 交换链一致 */
    int               compute;        /* 计算管线（shader 须含 cs） */
    int               threads_per_group[3]; /* 计算线程组尺寸（.sg comp 的 local_size）；0=1 */
    const char*       label;
} sc_gpu_pipeline_desc;

/* ---- pass ------------------------------------------------ */

typedef struct sc_gpu_color_attachment_action {
    sc_gpu_load_action  load;
    sc_gpu_store_action store;
    float               clear[4];     /* RGBA */
} sc_gpu_color_attachment_action;

typedef struct sc_gpu_depth_attachment_action {
    sc_gpu_load_action  load;
    sc_gpu_store_action store;
    float               clear_depth;   /* 默认 1.0 */
    uint8_t             clear_stencil;
} sc_gpu_depth_attachment_action;

typedef struct sc_gpu_pass_action {
    sc_gpu_color_attachment_action colors[SC_GPU_MAX_COLOR_ATTACHMENTS];
    sc_gpu_depth_attachment_action depth;
} sc_gpu_pass_action;

typedef struct sc_gpu_attachment {
    sc_gpu_image image;
    int          mip;
    int          slice;
} sc_gpu_attachment;

/* 附件全空 = 交换链 pass（渲染到当前 surface）；否则离屏 pass。
 * MSAA 离屏：colors[i] 指向 sample_count>1 的图像，resolves[i] 指向
 * 同尺寸 sample_count=1 的图像，store 默认变 RESOLVE。
 * compute 非 0 = 计算 pass（无附件，encoder 为计算类型，只可 dispatch）。 */
typedef struct sc_gpu_pass {
    sc_gpu_pass_action action;
    sc_gpu_attachment  colors[SC_GPU_MAX_COLOR_ATTACHMENTS];
    sc_gpu_attachment  resolves[SC_GPU_MAX_COLOR_ATTACHMENTS]; /* MSAA 解析目标 */
    sc_gpu_attachment  depth_stencil;
    int                compute;
    const char*        label;
} sc_gpu_pass;

/* ---- 绑定 ------------------------------------------------ */

typedef struct sc_gpu_stage_bindings {
    sc_gpu_image   images[SC_GPU_MAX_IMAGES];
    sc_gpu_sampler samplers[SC_GPU_MAX_SAMPLERS];
    sc_gpu_buffer  storage_buffers[SC_GPU_MAX_STORAGE_BUFFERS];
} sc_gpu_stage_bindings;

typedef struct sc_gpu_bindings {
    sc_gpu_buffer vertex_buffers[SC_GPU_MAX_VERTEX_BUFFERS];
    int           vertex_buffer_offsets[SC_GPU_MAX_VERTEX_BUFFERS];
    sc_gpu_buffer index_buffer;
    int           index_buffer_offset;
    sc_gpu_stage_bindings vs;
    sc_gpu_stage_bindings fs;
    sc_gpu_stage_bindings cs;
} sc_gpu_bindings;

/* ---- surface（呈现目标） ---------------------------------- */
/* surface = 一块可呈现的原生窗口面（交换链）。gpu 内部持有
 * 当前 surface，交换链 pass 渲染到它；多窗口时用 make_current
 * 切换。sc_gpu_init 若提供 native_window，自动创建默认 surface
 * 并置为当前。 */
typedef struct sc_gpu_surface_desc {
    void* native_window;   /* wsi: sc_wsi_win_get_native_window()
                              mac=NSView* / win=HWND / x11=Window / wl=wl_surface* */
    void* native_display;  /* x11=Display* / wl=wl_display*；mac/win 传 NULL */
    int   width;           /* 帧缓冲像素尺寸 */
    int   height;
    sc_gpu_pixel_format color_format;  /* 默认 BGRA8 */
    sc_gpu_pixel_format depth_format;  /* 默认 DEPTH_STENCIL，NONE=无 */
    int   sample_count;    /* 交换链 MSAA；默认 1 */
    int   swap_interval;   /* 垂直同步；默认 1 */
    const char* label;
} sc_gpu_surface_desc;

/* ---- 初始化 ---------------------------------------------- */

typedef struct sc_gpu_desc {
    sc_gpu_backend backend;         /* DEFAULT = 平台默认 */
    sc_gpu_surface_desc surface;    /* 默认 surface；native_window 为 NULL 则不建 */
    int            buffer_pool_size;    /* 各资源池容量；默认 128 */
    int            image_pool_size;     /* 默认 128 */
    int            sampler_pool_size;   /* 默认 64 */
    int            shader_pool_size;    /* 默认 64 */
    int            pipeline_pool_size;  /* 默认 64 */
    int            surface_pool_size;   /* 默认 8 */
} sc_gpu_desc;

/* ---- API：生命周期 ---------------------------------------- */

int  sc_gpu_init(const sc_gpu_desc* desc);       /* 1 成功 / 0 失败 */
void sc_gpu_shutdown(void);
int  sc_gpu_isvalid(void);
int  sc_gpu_query_backend(void);                 /* 实际生效的 sc_gpu_backend */
void sc_gpu_resize(int width, int height);       /* 当前 surface 尺寸变更（像素） */

/* ---- API：surface ------------------------------------------ */

sc_gpu_surface sc_gpu_make_surface(const sc_gpu_surface_desc* desc);
void sc_gpu_destroy_surface(sc_gpu_surface surf);
void sc_gpu_make_current(sc_gpu_surface surf);   /* 后续交换链 pass/resize/commit 针对它 */
sc_gpu_surface sc_gpu_query_current_surface(void);
void sc_gpu_surface_resize(sc_gpu_surface surf, int width, int height);

/* ---- API：能力查询 ----------------------------------------- */

typedef struct sc_gpu_pixelformat_info {
    int sample;   /* 可采样 */
    int filter;   /* 可线性过滤 */
    int render;   /* 可作渲染附件 */
    int blend;    /* 可混合 */
    int msaa;     /* 可 MSAA */
    int depth;    /* 深度/模板格式 */
} sc_gpu_pixelformat_info;

sc_gpu_pixelformat_info sc_gpu_query_pixelformat(sc_gpu_pixel_format fmt);

/* ---- API：资源 -------------------------------------------- */

sc_gpu_buffer   sc_gpu_make_buffer(const sc_gpu_buffer_desc* desc);
sc_gpu_image    sc_gpu_make_image(const sc_gpu_image_desc* desc);
sc_gpu_sampler  sc_gpu_make_sampler(const sc_gpu_sampler_desc* desc);
sc_gpu_shader   sc_gpu_make_shader(const sc_gpu_shader_desc* desc);
sc_gpu_pipeline sc_gpu_make_pipeline(const sc_gpu_pipeline_desc* desc);

void sc_gpu_destroy_buffer(sc_gpu_buffer buf);
void sc_gpu_destroy_image(sc_gpu_image img);
void sc_gpu_destroy_sampler(sc_gpu_sampler smp);
void sc_gpu_destroy_shader(sc_gpu_shader shd);
void sc_gpu_destroy_pipeline(sc_gpu_pipeline pip);

void sc_gpu_update_buffer(sc_gpu_buffer buf, const sc_gpu_range* data);
int  sc_gpu_append_buffer(sc_gpu_buffer buf, const sc_gpu_range* data); /* 返回偏移 */
void sc_gpu_update_image(sc_gpu_image img, const sc_gpu_image_data* data);

int  sc_gpu_query_buffer_state(sc_gpu_buffer buf);   /* sc_gpu_resource_state */
int  sc_gpu_query_image_state(sc_gpu_image img);
int  sc_gpu_query_shader_state(sc_gpu_shader shd);
int  sc_gpu_query_pipeline_state(sc_gpu_pipeline pip);

/* ---- API：帧 ---------------------------------------------- */

void sc_gpu_begin_pass(const sc_gpu_pass* pass);
void sc_gpu_apply_viewport(int x, int y, int w, int h, int origin_top_left);
void sc_gpu_apply_scissor(int x, int y, int w, int h, int origin_top_left);
void sc_gpu_apply_pipeline(sc_gpu_pipeline pip);
void sc_gpu_apply_bindings(const sc_gpu_bindings* bindings);
/* stage: sc_gpu_shader_stage；slot: 反射清单里的 binding */
void sc_gpu_apply_uniforms(int stage, int slot, const void* data, uint64_t size);
void sc_gpu_draw(int base_element, int element_count, int instance_count);
void sc_gpu_dispatch(int groups_x, int groups_y, int groups_z);
void sc_gpu_end_pass(void);
void sc_gpu_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* SC_GPU_H */
