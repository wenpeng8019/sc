/* ============================================================
 * gl_ctx.h —— OpenGL 上下文创建（平台层）内部接口
 * ============================================================
 * gl_dev.c 消费；实现见 gl_ctx.c（NSGL / WGL / GLX）。
 * ============================================================ */

#ifndef SC_GPU_GL_CTX_H
#define SC_GPU_GL_CTX_H

#include "../../platform.h"   /* 平台判定宏 P_DARWIN/P_LINUX/P_WIN（尊重交叉目标 SC_TARGET_*） */

/* 后端宏自推导（与 internal.h 同源逻辑；gl_ctx.m 独立于 internal.h） */
#if !defined(SC_GPU_GL) && (P_DARWIN || P_LINUX || P_WIN)
#define SC_GPU_GL 1
#endif

typedef struct gl_ctx gl_ctx;

/* 在 native_window 上创建 GL 上下文（core profile major.minor），
 * 创建后即为当前上下文。失败返回 NULL。 */
gl_ctx* gl_ctx_create(void* native_window, void* native_display,
                              int major, int minor, int swap_interval);
void gl_ctx_destroy(gl_ctx* c);
void gl_ctx_make_current(gl_ctx* c);   /* NULL = 清当前 */
void gl_ctx_swap(gl_ctx* c);
void gl_ctx_resize(gl_ctx* c);         /* 窗口尺寸变更后调（NSGL 需 update） */
void* gl_get_proc(const char* name);       /* GL 函数指针加载 */

#endif /* SC_GPU_GL_CTX_H */
