# wsi —— 跨平台窗口系统集成层（Window System Integration）
#
# 定位：sc 通用 utils 组件。抽象自 glfw（剔除游戏摇杆 joystick 与 gfx/GL/Vulkan
#   上下文创建），只做纯粹的 WSI —— 窗口 / 显示器 / 输入 / 事件 / 计时。
#   仅作为 root window 层：只暴露窗口的原生句柄（native display/window），
#   surface 封装由独立的 surface 模块经 create_from_native 完成。
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
#       if wsi_app_startup() == 0
#           return 1
#       var w: & = wsi_win_create(640, 480, "hello", nil, nil)
#       while wsi_win_get_should_close(w) == 0
#           wsi_loop_wait(0)
#       wsi_win_destroy(w)
#       wsi_app_cleanup()
#       return 0

inc wsi.h
add libwsi.a

# 指针约定：不透明句柄与结构体指针引用 wsi.h 内的 C 域类型（::sc_window /
#   ::sc_monitor / ::sc_cursor / ::sc_wsi_video_mode / ::sc_wsi_gamma_ramp / ::wsi_img），
#   令生成的 extern 原型与手写头逐字对齐（本单元为头支撑单元，
#   .c 已 #include wsi.h，消费单元 inc wsi.sc 亦直接见 wsi.h）。回调函数指针 typedef
#   同样以 ::sc_error_cb / ::sc_monitor_cb 引用。void* 用户数据映射裸指针 &；
#   出参基础类型用 i4& / f4& / f8&；char** 用 const char&&。NULL 传 nil。

# ---------------- 版本 / 平台 / 错误 ----------------
@fnc wsi_get_version:: major: i4&, minor: i4&, rev: i4&   # 运行时版本
@fnc wsi_get_version_string:: const char&                 # 版本描述串
@fnc wsi_get_platform:: i4                                # 当前已选择平台（SC_PLATFORM_*）
@fnc wsi_get_error:: i4, description: const char&&        # 取最近错误码 + 描述
@fnc wsi_set_error_callback:: ::sc_error_cb, callback: ::sc_error_cb   # 设置错误回调，返回旧回调

# ---------------- 程序 run ----------------
# 桌面的 while 主循环在移动端由系统框架接管，app 逻辑改以回调交付。移动端主窗口恒
# 全屏、由 wsi 自建（app 不再调 wsi_win_create），经 main_window_created 交付句柄（恒非
# nil，仅创建时回调）；after_startup 子系统就绪后回调一次，on_frame 每帧回调，
# before_cleanup 终止前回调。窗口丢失/销毁经 delegate 的 destroy 回调（见 wsi_win_set_callback）。
# 表面调用形式跨平台一致，底层 iOS = 自有主循环（UIApplicationMain 阻塞主线程），
# Android = 事件注册（ANativeWindow/AChoreographer 后进入渲染线程循环），桌面 = 通用托管循环。
@fnc wsi_app_run:: i4
    after_startup:       ::sc_wsi_app_cb
    main_window_created: ::sc_wsi_window_cb
    on_frame:            ::sc_wsi_app_cb
    before_cleanup:      ::sc_wsi_app_cb

# ---------------- 程序 loop ----------------
@fnc wsi_app_hint:: hint: i4, value: i4                   # 初始化前设置 hint
@fnc wsi_app_startup:: i4                                 # 启动子系统（成功非 0）
@fnc wsi_app_cleanup::                                    # 收尾，释放全部资源
@fnc wsi_loop_poll::                                      # 处理挂起事件后立即返回
@fnc wsi_loop_wait:: timeout: f8                          # timeout=0 阻塞直到有事件；>0 至多阻塞 timeout 秒
@fnc wsi_loop_pulse::                                     # 唤醒 wait_events

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
@fnc wsi_monitor_get_video_modes:: const ::sc_wsi_video_mode&, monitor: ::sc_monitor&, count: i4&
@fnc wsi_monitor_get_video_mode:: const ::sc_wsi_video_mode&, monitor: ::sc_monitor&
@fnc wsi_monitor_get_gamma:: monitor: ::sc_monitor&, gamma: f4
@fnc wsi_monitor_get_gamma_ramp:: const ::sc_wsi_gamma_ramp&, monitor: ::sc_monitor&
@fnc wsi_monitor_set_gamma_ramp:: monitor: ::sc_monitor&, ramp: const ::sc_wsi_gamma_ramp&

# ---------------- 窗口 hint ----------------
@fnc wsi_default_window_hints::
@fnc wsi_window_hint:: hint: i4, value: i4
@fnc wsi_window_hint_string:: hint: i4, value: const char&

# ---------------- 窗口生命周期 / 属性 ----------------

@fnc wsi_app_main_window:: ::sc_window&                    # 当前主窗口（未创建返回 nil）
# 窗口事件 delegate：按指针传回调表（sc_wsi_win_cb），承接 close/size/touch/suspend/
#   resume/destroy 等。原生窗口/表面丢失经 destroy 交付（承接原 on_main_window(nil) 语义）。
@fnc wsi_win_set_callback:: i4, window: ::sc_window&, cb: const ::sc_wsi_win_cb&

@fnc wsi_win_create:: ::sc_window&, width: i4, height: i4, title: const char&, monitor: ::sc_monitor&, share: ::sc_window&
@fnc wsi_win_destroy:: window: ::sc_window&
@fnc wsi_win_get_should_close:: i4, window: ::sc_window&
@fnc wsi_win_set_should_close:: window: ::sc_window&, value: i4
@fnc wsi_win_get_title:: const char&, window: ::sc_window&
@fnc wsi_win_set_title:: window: ::sc_window&, title: const char&
@fnc wsi_win_set_icon:: window: ::sc_window&, count: i4, images: const ::sc_wsi_img&
@fnc wsi_win_get_pos:: window: ::sc_window&, xpos: i4&, ypos: i4&
@fnc wsi_win_set_pos:: window: ::sc_window&, xpos: i4, ypos: i4
@fnc wsi_win_get_size:: window: ::sc_window&, width: i4&, height: i4&
@fnc wsi_win_get_framebuffer_size:: window: ::sc_window&, width: i4&, height: i4&
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
@fnc wsi_win_get_native_display:: &, window: ::sc_window&
@fnc wsi_win_get_native_window:: &, window: ::sc_window&

# ---------------- 输入 ----------------
@fnc wsi_input_get_mode:: i4, window: ::sc_window&, mode: i4
@fnc wsi_input_set_mode:: window: ::sc_window&, mode: i4, value: i4
@fnc wsi_mouse_raw_motion_supported:: i4
@fnc wsi_key_name:: const char&, key: i4, scancode: i4
@fnc wsi_key_scancode:: i4, key: i4
@fnc wsi_key:: i4, window: ::sc_window&, key: i4
@fnc wsi_mouse_button:: i4, window: ::sc_window&, button: i4
@fnc wsi_get_cursor_pos:: window: ::sc_window&, xpos: f8&, ypos: f8&
@fnc wsi_set_cursor_pos:: window: ::sc_window&, xpos: f8, ypos: f8
@fnc wsi_create_cursor:: ::sc_cursor&, image: const ::sc_wsi_img&, xhot: i4, yhot: i4
@fnc wsi_create_standard_cursor:: ::sc_cursor&, shape: i4
@fnc wsi_destroy_cursor:: cursor: ::sc_cursor&
@fnc wsi_cursor_set:: window: ::sc_window&, cursor: ::sc_cursor&
@fnc wsi_clipboard_get_string:: const char&, window: ::sc_window&
@fnc wsi_clipboard_set_string:: window: ::sc_window&, string: const char&

# ---------------- 计时 ----------------
@fnc wsi_get_time:: f8
@fnc wsi_set_time:: time: f8
@fnc wsi_timer_value:: u8
@fnc wsi_timer_frequency:: u8
