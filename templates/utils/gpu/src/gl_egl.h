/* ============================================================
 * gl_egl.h —— Linux EGL headless（GBM/DRM/dma-heap）内部接口
 * ============================================================
 * gl_env.c 消费（#ifdef __linux__）；实现见 gl_egl.c。
 * 职责：无窗口 GL 上下文（EGL surfaceless）、memimg 底层
 *（GBM BO / dma-heap → EGLImage）、fence 导出。
 * ============================================================ */

#ifndef SC_GPU_GL_EGL_H
#define SC_GPU_GL_EGL_H

#include "../gpu.h"
#include <stdbool.h>

/* 懒初始化：打开 DRM、GBM、EGLDisplay、surfaceless 上下文。
 * 多次调用幂等；失败返回 false。 */
bool _sc_gl_egl_init(void);
void _sc_gl_egl_shutdown(void);
void _sc_gl_egl_make_current(void);      /* headless 上下文置为当前 */

/* memimg 底层对象 */
typedef struct _sc_gl_memimg _sc_gl_memimg;

_sc_gl_memimg* _sc_gl_memimg_alloc(int w, int h, uint32_t fourcc,
                                   sc_gpu_memory_kind memory, uint64_t modifier,
                                   int renderable);
_sc_gl_memimg* _sc_gl_memimg_import(const sc_gpu_memory_frame* src);
bool  _sc_gl_memimg_export(_sc_gl_memimg* m, sc_gpu_memory_frame* out);
void* _sc_gl_memimg_egl_image(_sc_gl_memimg* m);   /* EGLImageKHR */
void* _sc_gl_memimg_map(_sc_gl_memimg* m, uint32_t* out_stride);
void  _sc_gl_memimg_unmap(_sc_gl_memimg* m);
void  _sc_gl_memimg_free(_sc_gl_memimg* m);

/* 在当前上下文插入 native fence 并导出 sync_fd（调用方负责 close）。
 * 需先 glFlush；扩展不可用返回 -1。 */
int _sc_gl_egl_fence_fd(void);

#endif /* SC_GPU_GL_EGL_H */
