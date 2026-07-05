# wsi —— 跨平台窗口系统集成层（Window System Integration）
#
# 定位：sc 通用 utils 组件。抽象自 glfw（剔除游戏摇杆 joystick 与 gfx/GL/Vulkan
#   上下文创建），只做纯粹的 WSI —— 窗口 / 显示器 / 输入 / 事件 / 计时。
#   既可用于 UI 方向，又可通过 surface 绑定到 gfx（原生窗口句柄经 nwh 传出）。
#
# 集成方式（参考 templates/plib）：库经 build.sh 独立编译为 libwsi.<triple>.a，
#   sc 侧只做 FFI 声明 + add 链接。C API 全部以 sc_wsi_ 前缀导出（见 wsi.h）；
#   sc 侧 @fnc wsi_xxx:: 生成的外部符号即 sc_wsi_xxx，与库对齐。
#
# 平台适配：build.sh 产出 libwsi.<triple>.a，add libwsi.a 由编译器自动匹配变体。
#   目前仅 macOS(Cocoa) 完成可编译；Linux(X11)/Windows(Win32) 源码在库内、待完备。
#
# 用法：
#   inc wsi.sc
#   fnc main: i4
#       if wsi_init() == 0
#           return 1
#       var w: & = wsi_win_create(640, 480, "hello", nil, nil)
#       while wsi_win_get_should_close(w) == 0
#           wsi_wait_events()
#       wsi_win_destroy(w)
#       wsi_terminate()
#       return 0

inc wsi.h
add libwsi.a

# 指针约定：不透明句柄与结构体指针引用 wsi.h 内的 C 域类型（::sc_window /
#   ::sc_monitor / ::sc_cursor / ::GLFWvidmode / ::GLFWgammaramp / ::GLFWimage /
#   ::sc_allocator_cb），令生成的 extern 原型与手写头逐字对齐（本单元为头支撑单元，
#   .c 已 #include wsi.h，消费单元 inc wsi.sc 亦直接见 wsi.h）。回调函数指针 typedef
#   同样以 ::sc_error_cb / ::sc_monitor_cb 引用。void* 用户数据映射裸指针 &；
#   出参基础类型用 i4& / f4& / f8&；char** 用 const char&&。NULL 传 nil。

# ---------------- 版本 / 初始化 / 错误 / 分配器 ----------------
@fnc wsi_get_version:: major: i4&, minor: i4&, rev: i4&   # 运行时版本
@fnc wsi_get_version_string:: const char&                 # 版本描述串
@fnc wsi_init:: i4                                        # 初始化（成功非 0）
@fnc wsi_terminate::                                      # 反初始化，释放全部资源
@fnc wsi_init_hint:: hint: i4, value: i4                  # 初始化前设置 hint
@fnc wsi_get_error:: i4, description: const char&&        # 取最近错误码 + 描述
@fnc wsi_set_error_callback:: ::sc_error_cb, callback: ::sc_error_cb   # 设置错误回调，返回旧回调
@fnc wsi_init_allocator:: allocator: const ::sc_allocator_cb&          # 自定义内存分配器

# ---------------- 事件 ----------------
@fnc wsi_poll_events::                                    # 处理挂起事件后立即返回
@fnc wsi_wait_events::                                    # 阻塞直到有事件
@fnc wsi_wait_events_timeout:: timeout: f8                # 阻塞至有事件或超时（秒）
@fnc wsi_post_empty_event::                               # 唤醒 wait_events

# ---------------- 显示器（monitor）----------------
@fnc wsi_get_monitors:: ::sc_monitor&&, count: i4&        # 显示器数组 sc_monitor**
@fnc wsi_get_primary_monitor:: ::sc_monitor&              # 主显示器
@fnc wsi_monitor_get_pos:: monitor: ::sc_monitor&, xpos: i4&, ypos: i4&
@fnc wsi_monitor_get_work_area:: monitor: ::sc_monitor&, xpos: i4&, ypos: i4&, width: i4&, height: i4&
@fnc wsi_monitor_get_physical_size:: monitor: ::sc_monitor&, widthMM: i4&, heightMM: i4&
@fnc wsi_monitor_get_content_scale:: monitor: ::sc_monitor&, xscale: f4&, yscale: f4&
@fnc wsi_monitor_get_name:: const char&, monitor: ::sc_monitor&
@fnc wsi_monitor_get_user_data:: &, monitor: ::sc_monitor&
@fnc wsi_monitor_set_user_data:: monitor: ::sc_monitor&, pointer: &
@fnc wsi_monitor_set_callback:: ::sc_monitor_cb, callback: ::sc_monitor_cb   # 返回旧回调
@fnc wsi_monitor_get_video_modes:: const ::GLFWvidmode&, monitor: ::sc_monitor&, count: i4&
@fnc wsi_monitor_get_video_mode:: const ::GLFWvidmode&, monitor: ::sc_monitor&
@fnc wsi_monitor_get_gamma:: monitor: ::sc_monitor&, gamma: f4
@fnc wsi_monitor_get_gamma_ramp:: const ::GLFWgammaramp&, monitor: ::sc_monitor&
@fnc wsi_monitor_set_gamma_ramp:: monitor: ::sc_monitor&, ramp: const ::GLFWgammaramp&

# ---------------- 窗口 hint ----------------
@fnc wsi_default_window_hints::
@fnc wsi_window_hint:: hint: i4, value: i4
@fnc wsi_window_hint_string:: hint: i4, value: const char&

# ---------------- 窗口生命周期 / 属性 ----------------
@fnc wsi_win_create:: ::sc_window&, width: i4, height: i4, title: const char&, monitor: ::sc_monitor&, share: ::sc_window&
@fnc wsi_win_destroy:: window: ::sc_window&
@fnc wsi_win_get_should_close:: i4, window: ::sc_window&
@fnc wsi_win_set_should_close:: window: ::sc_window&, value: i4
@fnc wsi_win_get_title:: const char&, window: ::sc_window&
@fnc wsi_win_set_title:: window: ::sc_window&, title: const char&
@fnc wsi_win_set_icon:: window: ::sc_window&, count: i4, images: const ::GLFWimage&
@fnc wsi_win_get_pos:: window: ::sc_window&, xpos: i4&, ypos: i4&
@fnc wsi_win_set_pos:: window: ::sc_window&, xpos: i4, ypos: i4
@fnc wsi_win_get_size:: window: ::sc_window&, width: i4&, height: i4&
@fnc wsi_win_set_size:: window: ::sc_window&, width: i4, height: i4
@fnc wsi_win_set_size_limits:: window: ::sc_window&, minwidth: i4, minheight: i4, maxwidth: i4, maxheight: i4
@fnc wsi_win_set_size_aspect_ratio:: window: ::sc_window&, numer: i4, denom: i4
@fnc wsi_win_get_frame_size:: window: ::sc_window&, left: i4&, top: i4&, right: i4&, bottom: i4&
@fnc wsi_win_get_content_scale:: window: ::sc_window&, xscale: f4&, yscale: f4&
@fnc wsi_win_get_opacity:: f4, window: ::sc_window&
@fnc wsi_win_set_opacity:: window: ::sc_window&, opacity: f4
@fnc wsi_win_iconify:: window: ::sc_window&
@fnc wsi_win_restore:: window: ::sc_window&
@fnc wsi_win_maximize:: window: ::sc_window&
@fnc wsi_win_show:: window: ::sc_window&
@fnc wsi_win_hide:: window: ::sc_window&
@fnc wsi_win_focus:: window: ::sc_window&
@fnc wsi_win_request_attention:: window: ::sc_window&
@fnc wsi_win_get_monitor:: ::sc_monitor&, window: ::sc_window&
@fnc wsi_win_set_monitor:: window: ::sc_window&, monitor: ::sc_monitor&, xpos: i4, ypos: i4, width: i4, height: i4, refreshRate: i4
@fnc wsi_win_get_attrib:: i4, window: ::sc_window&, attrib: i4
@fnc wsi_win_set_attrib:: window: ::sc_window&, attrib: i4, value: i4
@fnc wsi_win_get_user_data:: &, window: ::sc_window&
@fnc wsi_win_set_user_data:: window: ::sc_window&, pointer: &

# 注：sc_wsi_win_set_callback(sc_window*, sc_wsi_win_cb) 按值传结构体，
#     sc FFI 无法表达（需 C 侧胶水填充回调表），故此处不导出。

# ---------------- 输入 ----------------
@fnc wsi_input_get_mode:: i4, window: ::sc_window&, mode: i4
@fnc wsi_input_set_mode:: window: ::sc_window&, mode: i4, value: i4
@fnc wsi_mouse_raw_motion_supported:: i4
@fnc wsi_key_name:: const char&, key: i4, scancode: i4
@fnc wsi_key_scancode:: i4, key: i4
@fnc wsi_key:: i4, window: ::sc_window&, key: i4
@fnc wsi_mouse_button:: i4, window: ::sc_window&, button: i4
@fnc wsi_cursor_get_pos:: window: ::sc_window&, xpos: f8&, ypos: f8&
@fnc wsi_cursor_set_pos:: window: ::sc_window&, xpos: f8, ypos: f8
@fnc wsi_cursor_create:: ::sc_cursor&, image: const ::GLFWimage&, xhot: i4, yhot: i4
@fnc wsi_cursor_create_standard:: ::sc_cursor&, shape: i4
@fnc wsi_cursor_destroy:: cursor: ::sc_cursor&
@fnc wsi_cursor_set:: window: ::sc_window&, cursor: ::sc_cursor&
@fnc wsi_clipboard_get_string:: const char&, window: ::sc_window&
@fnc wsi_clipboard_set_string:: window: ::sc_window&, string: const char&

# ---------------- 计时 ----------------
@fnc wsi_get_time:: f8
@fnc wsi_set_time:: time: f8
@fnc wsi_timer_value:: u8
@fnc wsi_timer_frequency:: u8
