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
 * 平台原生句柄标准（本头即标准定义，窗口库适配本标准）：
 *   gpu 不依赖任何窗口库——native_window/native_display 是平台原生
 *   句柄（void*），含义由下表定义。任何窗口库（如 templates/.scenv/modules/wsi）
 *   按此标准交付句柄即可对接；无窗口场景（MEMORY surface）两者均 NULL。
 *
 *   | 平台        | native_window     | native_display |
 *   |-------------|-------------------|----------------|
 *   | macOS       | NSView*           | NULL           |
 *   | Windows     | HWND              | NULL           |
 *   | X11         | Window(XID)       | Display*       |
 *   | Wayland     | wl_surface*       | wl_display*    |
 *   | Android(规划)| ANativeWindow*    | NULL           |
 *   | 嵌入式 EGL   | EGLNativeWindowType | EGLNativeDisplayType |
 *
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
typedef uint32_t sc_gpu_memimg;    /* 可导出/可导入内存图像（dma-buf / IOSurface） */

/* ---- 枚举 ------------------------------------------------ */

typedef enum sc_gpu_backend {
    SC_GPU_BACKEND_DEFAULT = 0,   /* 平台默认：mac=Metal, linux/win=GL */
    SC_GPU_BACKEND_METAL,
    SC_GPU_BACKEND_GL,
    SC_GPU_BACKEND_VULKAN,        /* Vulkan（linux/win；SPIR-V + set/binding 反射） */
    SC_GPU_BACKEND_D3D11,         /* Direct3D 11（windows；HLSL/DXBC） */
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
/* surface = 一块可呈现的目标面（交换链），两种形态：
 *   WINDOW：原生窗口面（CAMetalLayer / GL 上下文）
 *   MEMORY：无表面——N 张可导出内存图像（memimg）的环，渲染后
 *           dequeue 取帧送编码器，enqueue 归还（视频流/无屏场景）
 * gpu 内部持有当前 surface；多目标时用 make_current 切换。
 * sc_gpu_init 若提供 native_window，自动创建默认 WINDOW surface。 */

typedef enum sc_gpu_surface_kind {
    SC_GPU_SURFACE_WINDOW = 0,
    SC_GPU_SURFACE_MEMORY,
} sc_gpu_surface_kind;

/* 内存图像的底层分配方式 */
typedef enum sc_gpu_memory_kind {
    SC_GPU_MEMORY_DEFAULT = 0,   /* 平台最优：linux=GBM BO，mac=IOSurface */
    SC_GPU_MEMORY_GBM,           /* linux GBM BO（设备最优布局） */
    SC_GPU_MEMORY_DMA_HEAP,      /* linux CMA dma-heap（物理连续，编码器要求时） */
    SC_GPU_MEMORY_IOSURFACE,     /* macOS IOSurface（可送 VideoToolbox） */
} sc_gpu_memory_kind;

enum { SC_GPU_MAX_MEMORY_IMAGES = 8 };   /* MEMORY surface 环深度上限 */

typedef struct sc_gpu_surface_desc {
    sc_gpu_surface_kind kind;
    void* native_window;   /* WINDOW：平台原生窗口句柄（标准见文件头表）；
                              窗口库适配本标准，如 wsi: sc_wsi_win_get_native_window() */
    void* native_display;  /* x11=Display* / wl=wl_display*；mac/win 传 NULL */
    int   width;           /* 帧缓冲像素尺寸 */
    int   height;
    sc_gpu_pixel_format color_format;  /* 默认 BGRA8 */
    sc_gpu_pixel_format depth_format;  /* 默认 DEPTH_STENCIL，NONE=无 */
    int   sample_count;    /* 交换链 MSAA；默认 1（GL 后端暂只支持 1） */
    int   swap_interval;   /* 垂直同步；默认 1（MEMORY 忽略） */
    int   image_count;     /* MEMORY：环深度，默认 3 */
    sc_gpu_memory_kind memory;   /* MEMORY：底层分配方式 */
    const char* label;
} sc_gpu_surface_desc;

/* ---- 初始化 ---------------------------------------------- */

typedef struct sc_gpu_desc {
    sc_gpu_backend backend;         /* DEFAULT = 平台默认 */
    sc_gpu_surface_desc surface;    /* 默认 surface；native_window 为 NULL 则不建 */
    int            surface_pool_size;   /* 默认 8 */
    int            memimg_pool_size;    /* 默认 64 */
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

/* ---- memimg（可导出/可导入内存图像，平台原语） ------------ */
/* 双向复用的边界原语：
 *   输出：MEMORY surface 环成员（Mode A，gpu 驱动）；或绑定到
 *         gfx image 作离屏渲染目标（Mode B，按需绘制，应用自管环）
 *   输入：导入外部 dma-buf（如 v4l2 相机）绑定到 gfx image 作
 *         采样纹理（零拷贝）
 * 底层：linux = GBM BO / dma-heap → EGLImage；mac = IOSurface。 */

typedef struct sc_gpu_memimg_desc {
    int width, height;
    sc_gpu_pixel_format format;   /* RGB 系格式；默认 BGRA8 */
    uint32_t fourcc;              /* 非 0 时覆盖 format（DRM fourcc，YUV 等） */
    sc_gpu_memory_kind memory;
    int render_target;            /* 需作渲染目标（默认是；导入纹理可 0） */
    uint64_t modifier;            /* DRM modifier 意向；0 = LINEAR（预留） */
    const char* label;
} sc_gpu_memimg_desc;

#define SC_GPU_FOURCC(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/* 导出帧描述（送编码器 / 外部导入来源）。
 * 生命周期：fd/native 为借用——dequeue→enqueue 或 export→memimg_free
 * 期间有效，调用方不关闭（需更长持有自行 dup）。sync_fd 归调用方
 * 关闭（-1 = 无栅栏，消费前应 sc_gfx_finish() 或依赖平台隐式同步）。 */
typedef struct sc_gpu_memory_frame {
    int      planes;
    int      fd[4];         /* linux: dma-buf fd（每平面）；mac 无效(-1) */
    uint32_t stride[4];
    uint32_t offset[4];
    uint32_t fourcc;        /* DRM fourcc */
    int      width, height;
    int      sync_fd;       /* 显式同步栅栏；-1 = 无 */
    void*    native;        /* mac: IOSurfaceRef（可直送 VideoToolbox） */
    sc_gpu_memimg img;      /* 对应的 memimg 句柄（map 用） */
    uint32_t slot;          /* MEMORY surface 归还凭据 */
} sc_gpu_memory_frame;

sc_gpu_memimg sc_gpu_memimg_alloc(const sc_gpu_memimg_desc* desc);
sc_gpu_memimg sc_gpu_memimg_import(const sc_gpu_memory_frame* src);  /* 相机零拷贝 */
int   sc_gpu_memimg_export(sc_gpu_memimg img, sc_gpu_memory_frame* out, int with_fence);
void* sc_gpu_memimg_native(sc_gpu_memimg img);   /* EGLImage / MTLTexture（gfx 后端消费） */
void* sc_gpu_memimg_map(sc_gpu_memimg img, int plane, uint32_t* out_stride); /* CPU 映射 */
void  sc_gpu_memimg_unmap(sc_gpu_memimg img, int plane);
void  sc_gpu_memimg_free(sc_gpu_memimg img);

/* ---- API：MEMORY surface 消费端（编码器侧） ----------------- */
/* 渲染端循环与窗口场景完全同构（make_current → gfx pass → commit）；
 * 消费端（可在另一线程）：dequeue 取渲染完的帧 → 编码 → enqueue 归还。
 * 单生产者 + 单消费者语义。 */

int  sc_gpu_memory_dequeue(sc_gpu_surface surf, sc_gpu_memory_frame* out); /* 1 成功/0 无帧 */
void sc_gpu_memory_enqueue(sc_gpu_surface surf, uint32_t slot);

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
