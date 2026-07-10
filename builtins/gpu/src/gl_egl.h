/* ============================================================
 * gl_egl.h —— Linux EGL headless（GBM/DRM/dma-heap）内部接口
 * ============================================================
 * gl_env.c 消费（#if P_LINUX）；实现见 gl_egl.c。
 * 职责：无窗口 GL 上下文（EGL surfaceless）、memimg 底层
 *（GBM BO / dma-heap → EGLImage）、fence 导出。
 * ============================================================ */

#ifndef SC_GPU_GL_EGL_H
#define SC_GPU_GL_EGL_H

#include "../gpu.h"
#include <stdbool.h>

/* 懒初始化：打开 DRM、GBM、EGLDisplay、surfaceless 上下文。
 * 多次调用幂等；失败返回 false。 */
bool gl_egl_init(void);
void gl_egl_shutdown(void);
void gl_egl_make_current(void);      /* headless 上下文置为当前 */

/* memimg 底层对象 */
typedef struct gl_memimg gl_memimg;

gl_memimg* gl_memimg_alloc(int w, int h, uint32_t fourcc,
                                   sc_gpu_memory_kind memory, uint64_t modifier,
                                   int renderable);
gl_memimg* gl_memimg_import(const sc_gpu_memory_frame* src);
bool  gl_memimg_export(gl_memimg* m, sc_gpu_memory_frame* out);
void* gl_memimg_egl_image(gl_memimg* m);   /* EGLImageKHR */
void* gl_memimg_map(gl_memimg* m, uint32_t* out_stride);
void  gl_memimg_unmap(gl_memimg* m);
void  gl_memimg_free(gl_memimg* m);

/* 在当前上下文插入 native fence 并导出 sync_fd（调用方负责 close）。
 * 需先 glFlush；扩展不可用返回 -1。 */
int gl_egl_fence_fd(void);

/* ---- window surface（EGL 窗口路径：Wayland/X11/嵌入式） ----
 * 与 headless 分属不同 EGLDisplay；SC_GPU_GLES 建 ES3（回落 ES2），
 * 否则桌面 GL 4.1→3.3 core。创建后即为当前上下文。 */
typedef struct gl_egl_win gl_egl_win;
gl_egl_win* gl_egl_win_create(void* native_display, void* native_window,
                              int swap_interval);
void gl_egl_win_destroy(gl_egl_win* w);
void gl_egl_win_make_current(gl_egl_win* w);   /* NULL = 清当前 */
void gl_egl_win_swap(gl_egl_win* w);

#endif /* SC_GPU_GL_EGL_H */
