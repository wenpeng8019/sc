/* ============================================================
 * gfx.h —— 跨平台 GPU (GL) 运行环境 C 类型与函数原型
 *
 * 定位：templates/utils/gfx 的 C 接口层。
 *   在原生窗口句柄上创建 OpenGL 渲染 Context。
 *   平台无关的类型与原型；平台实现在 gfx_impl.c。
 *
 * 函数名带 sc_ 前缀以匹配 sc 侧的 @fnc name:: 约定。
 * ============================================================ */

#ifndef SC_GFX_H
#define SC_GFX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gfx_s gfx_t;

void* sc_gfx_init(void* nwh, int w, int h, int major, int minor);
void  sc_gfx_swap(void* ctx);
void  sc_gfx_make_current(void* ctx);
void  sc_gfx_destroy(void* ctx);
void* sc_gfx_get_proc(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* SC_GFX_H */
