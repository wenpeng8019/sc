# gfx —— 跨平台 GPU (GL) 运行环境创建组件
#
# 定位：templates 通用 utils 组件。在原生窗口句柄上创建 OpenGL 渲染 Context。
#   与 win 模块边界清晰独立——仅通过 win_get_nwh 返回的原生窗口句柄关联。
#   提取自 glfw (context.c/nsgl_context.m/glx_context.c)、
#   sokol_app.h (_sapp_wgl_* / _sapp_macos_gl_init)、
#   bgfx (glcontext_egl.cpp/glcontext_wgl.cpp)。
#
# 目前仅 OpenGL Core Profile；将来可扩展 Vulkan/Metal 后端。
#
# 依赖：无（纯 C 实现层，add gfx_impl.o 编译链接）。
#   运行时依赖 win 模块提供原生窗口句柄（通过 nwh: & 参数传入，无代码级耦合）。
#
# 用法：
#   inc templates/wsi/win.sc
#   inc templates/utils/gfx/gfx.sc
#
#   var w: & = win_create("GL Window", 800, 600)
#   var ctx: & = gfx_init(win_get_nwh(w), 800, 600, 3, 3)
#   while win_should_close(w) == 0
#       win_poll(w)
#       gfx_make_current(ctx)
#       ::glClearColor(0.2, 0.3, 0.3, 1.0)
#       ::glClear(::GL_COLOR_BUFFER_BIT)
#       gfx_swap(ctx)
#   gfx_destroy(ctx)
#   win_destroy(w)

add gfx_impl.o

# ---- C 函数映射 ----
@fnc gfx_init:: &, nwh: &, w: i4, h: i4, major: i4, minor: i4  # 创建 GL Context，返回不透明句柄
@fnc gfx_swap:: ctx: &                                           # swap buffers
@fnc gfx_make_current:: ctx: &                                   # 设为当前线程活动 Context
@fnc gfx_destroy:: ctx: &                                        # 销毁 Context 并释放
@fnc gfx_get_proc:: &, name: const char&                         # 获取 GL 函数指针
