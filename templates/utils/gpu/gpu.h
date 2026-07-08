/* ============================================================
 * gpu.h —— sc GPU 运行环境（env 层）C API
 * ============================================================
 * 定位（类比 sokol_app 去掉窗口维护的部分）：
 *   为渲染层（gfx 模块）搭好"GPU 运行环境"：
 *     · 选择/初始化平台图形后端（Metal / GL / Null）
 *     · surface = 呈现目标（交换链）：CAMetalLayer / GL 上下文，
 *       含交换链 MSAA / 深度附属目标、resize、vsync
 *     · 每帧交付渲染目标（sc_gpu_frame）并负责呈现收尾
 *   真正的绘制渲染（资源/管线/pass/draw）在 gfx 模块（utils/gfx）。
 *
 * 与 gfx 的边界（参考 sokol_gfx 的 environment/swapchain 契约）：
 *   一次性交付：sc_gpu_device()——原生设备句柄（Metal 的 MTLDevice；
 *               GL 无设备概念，返回 NULL，上下文经 make_current 生效）
 *   每帧交付：sc_gpu_frame_acquire()——当前 surface 本帧的渲染目标
 *               原生句柄 + 真实像素尺寸（resize 竞态以此为准）
 *   帧收尾：sc_gpu_frame_end()——GL swap / Metal 释放 drawable 引用
 *   （Metal 的呈现由 gfx 把 frame.drawable 挂到命令缓冲，最佳节拍）
 *
 * 多后端机制（同 wsi/glfw）：一个 libgpu.a 可同时编入多个后端，
 *   运行时按平台默认或 desc.backend 显式选择，不静默降级：
 *     macOS   → Metal（默认） / GL
 *     Linux   → GL（默认）   / Vulkan（远期）
 *     Windows → GL（远期，需加载器） / D3D（远期）
 *
 * 依赖：wsi（native_window / native_display）。
 * 函数名带 sc_ 前缀以匹配 sc 侧 @fnc name:: 约定（gpu.sc 里去前缀）。
 * ============================================================ */

#ifndef SC_GPU_H
#define SC_GPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄 ------------------------------------------------ */
/* 32 位不透明句柄：低 16 位池索引，高 16 位代数（防悬垂）。0 = 无效。
 * 纯 u32 typedef：与 sc 侧 FFI 的 u4 直接对应。 */

typedef uint32_t sc_gpu_surface;   /* 呈现目标（交换链）；make_current 切换 */

/* ---- 枚举 ------------------------------------------------ */

typedef enum sc_gpu_backend {
    SC_GPU_BACKEND_DEFAULT = 0,   /* 平台默认：mac=Metal, linux/win=GL */
    SC_GPU_BACKEND_METAL,
    SC_GPU_BACKEND_GL,
    SC_GPU_BACKEND_NULL,          /* 空后端（无硬件/测试） */
} sc_gpu_backend;

/* 像素格式：env 与 gfx 的共同词汇（surface 的颜色/深度格式、
 * gfx 的纹理/附件格式都用它）。 */
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

/* ---- surface（呈现目标） ---------------------------------- */
/* surface = 一块可呈现的原生窗口面（交换链）。gpu 内部持有当前
 * surface；多窗口时用 make_current 切换。sc_gpu_init 若提供
 * native_window，自动创建默认 surface 并置为当前。 */

typedef struct sc_gpu_surface_desc {
    void* native_window;   /* wsi: sc_wsi_win_get_native_window()
                              mac=NSView* / win=HWND / x11=Window / wl=wl_surface* */
    void* native_display;  /* x11=Display* / wl=wl_display*；mac/win 传 NULL */
    int   width;           /* 帧缓冲像素尺寸 */
    int   height;
    sc_gpu_pixel_format color_format;  /* 默认 BGRA8 */
    sc_gpu_pixel_format depth_format;  /* 默认 DEPTH_STENCIL，NONE=无 */
    int   sample_count;    /* 交换链 MSAA；默认 1（GL 后端暂只支持 1） */
    int   swap_interval;   /* 垂直同步；默认 1 */
    const char* label;
} sc_gpu_surface_desc;

/* ---- 初始化 ---------------------------------------------- */

typedef struct sc_gpu_desc {
    sc_gpu_backend backend;         /* DEFAULT = 平台默认 */
    sc_gpu_surface_desc surface;    /* 默认 surface；native_window 为 NULL 则不建 */
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
void sc_gpu_make_current(sc_gpu_surface surf);   /* 后续帧交付/resize 针对它 */
sc_gpu_surface sc_gpu_query_current_surface(void);
void sc_gpu_surface_resize(sc_gpu_surface surf, int width, int height);
/* 查 surface 的已解析 desc（surf=0 取当前）。1 成功 / 0 无效 */
int  sc_gpu_query_surface_info(sc_gpu_surface surf, sc_gpu_surface_desc* out);

/* ---- API：帧交付（gfx 消费） ------------------------------- */

/* 一次性环境交付：原生设备句柄。
 * Metal: id<MTLDevice>（__bridge）；GL/Null: NULL。 */
void* sc_gpu_device(void);

/* 当前 surface 本帧的渲染目标。首次调用才真正获取（懒 acquire，
 * 同帧重复调用返回同一目标）；宽高为本帧真实像素尺寸。 */
typedef struct sc_gpu_frame {
    void* color;        /* Metal: drawable 的 MTLTexture；GL: NULL（默认帧缓冲） */
    void* msaa_color;   /* Metal: 交换链 MSAA 颜色纹理；无 MSAA 为 NULL */
    void* depth;        /* Metal: 深度(模板)纹理；GL: NULL（默认帧缓冲自带） */
    void* drawable;     /* Metal: CAMetalDrawable——gfx commit 挂命令缓冲呈现 */
    uint32_t gl_fbo;    /* GL: 目标帧缓冲 id（默认 0） */
    int width, height;
    int sample_count;
    sc_gpu_pixel_format color_format;
    sc_gpu_pixel_format depth_format;   /* NONE = 无深度附件 */
} sc_gpu_frame;

int  sc_gpu_frame_acquire(sc_gpu_frame* out);    /* 1 成功 / 0 失败 */

/* 帧收尾（gfx commit 末尾调用）：呈现/翻转本帧触达的所有 surface。
 * GL = swapBuffers；Metal = 释放 drawable 引用（呈现已由 gfx 挂
 * 命令缓冲，命令缓冲持有 drawable）。 */
void sc_gpu_frame_end(void);

#ifdef __cplusplus
}
#endif

#endif /* SC_GPU_H */
