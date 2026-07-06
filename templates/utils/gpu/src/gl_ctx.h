/* ============================================================
 * gl_ctx.h —— OpenGL 上下文创建（平台层）内部接口
 * ============================================================
 * gl_dev.c 消费；实现见 gl_ctx.c（NSGL / WGL / GLX）。
 * ============================================================ */

#ifndef SC_GPU_GL_CTX_H
#define SC_GPU_GL_CTX_H

typedef struct _sc_gl_ctx _sc_gl_ctx;

/* 在 native_window 上创建 GL 上下文（core profile major.minor），
 * 创建后即为当前上下文。失败返回 NULL。 */
_sc_gl_ctx* _sc_gl_ctx_create(void* native_window, void* native_display,
                              int major, int minor, int swap_interval);
void _sc_gl_ctx_destroy(_sc_gl_ctx* c);
void _sc_gl_ctx_make_current(_sc_gl_ctx* c);   /* NULL = 清当前 */
void _sc_gl_ctx_swap(_sc_gl_ctx* c);
void _sc_gl_ctx_resize(_sc_gl_ctx* c);         /* 窗口尺寸变更后调（NSGL 需 update） */
void* _sc_gl_get_proc(const char* name);       /* GL 函数指针加载 */

#endif /* SC_GPU_GL_CTX_H */
