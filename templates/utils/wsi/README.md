# WSI 模块说明

## 1. 模块目的

WSI 是 sc 工程里的跨平台窗口系统接口层，负责提供统一的窗口、输入、显示器、剪贴板与计时能力。

设计目标：
- 在不同平台上提供一致的 C API。
- Linux 单库同时包含 X11 与 Wayland 后端，运行时自动选择。
- 与渲染后端解耦，专注窗口系统与事件系统。
- 作为编译器与运行时的基础设施模块，减少上层平台分支。

在新的三层架构中：
- `wsi`：只负责 root window 与系统事件。
- `ui`：负责子空间/子窗口组织。
- `surface`：独立模块，从 native handle 创建渲染 surface。

关键入口头文件：
- wsi.h

关键构建脚本：
- build.sh

关键源码目录：
- src
- protocols


## 2. 模块边界

WSI 当前边界如下。

模块内负责：
- 窗口生命周期管理（创建、销毁、显示、隐藏、尺寸、位置、焦点等）。
- 输入事件与状态（键盘、鼠标、游标模式、原始鼠标输入）。
- 显示器信息与模式（主显示器、工作区、缩放、Gamma）。
- 光标与剪贴板。
- 跨平台计时与事件泵接口。
- 平台后端加载与运行时选择。
- root window 原生句柄导出（给 UI/surface 模块消费）。

模块不负责：
- OpenGL 或 EGL 上下文创建与生命周期。
- Vulkan surface 扩展对接。
- OSMesa 软件上下文。
- 图形渲染逻辑与渲染资源管理。

说明：
- 该模块已完成去 GL/去 Vulkan 清理，平台接口以窗口系统能力为主。


## 3. 模块能力清单

来自公开 API（wsi.h）的主要能力分组：

- 初始化与错误处理
  - sc_wsi_init
  - sc_wsi_terminate
  - sc_wsi_get_error
  - sc_wsi_set_error_callback

- 事件循环
  - sc_wsi_poll_events
  - sc_wsi_wait_events
  - sc_wsi_wait_events_timeout
  - sc_wsi_post_empty_event

- 显示器
  - sc_wsi_get_monitors
  - sc_wsi_get_primary_monitor
  - sc_wsi_monitor_get_pos
  - sc_wsi_monitor_get_work_area
  - sc_wsi_monitor_get_content_scale
  - sc_wsi_monitor_get_video_modes
  - sc_wsi_monitor_get_gamma_ramp

- 窗口
  - sc_wsi_win_create
  - sc_wsi_win_destroy
  - sc_wsi_win_get_size / sc_wsi_win_set_size
  - sc_wsi_win_get_pos / sc_wsi_win_set_pos
  - sc_wsi_win_set_title
  - sc_wsi_win_show / sc_wsi_win_hide / sc_wsi_win_focus
  - sc_wsi_win_get_attrib / sc_wsi_win_set_attrib
  - sc_wsi_get_platform
  - sc_wsi_win_get_native_display / sc_wsi_win_get_native_window

- 输入与光标
  - sc_wsi_key
  - sc_wsi_mouse_button
  - sc_wsi_cursor_get_pos / sc_wsi_cursor_set_pos
  - sc_wsi_cursor_create / sc_wsi_cursor_destroy
  - sc_wsi_mouse_raw_motion_supported

- 剪贴板与时间
  - sc_wsi_clipboard_get_string / sc_wsi_clipboard_set_string
  - sc_wsi_get_time / sc_wsi_set_time
  - sc_wsi_timer_value / sc_wsi_timer_frequency

- 平台后端能力
  - macOS: Cocoa
  - Linux: X11 + Wayland（同库同时编入，运行时自动选择）
  - Windows: Win32


## 4. 构建

### 4.1 宿主构建

在模块目录执行：

  cd templates/utils/wsi
  ./build.sh

产物：
- libwsi.<triple>.a
- libwsi.a（宿主构建时生成，指向或复制自带 triple 后缀版本）


### 4.2 交叉构建

示例：

  ./build.sh --target aarch64-linux-gnu
  ./build.sh --cc cl --ar lib
  ./build.sh --cc x86_64-w64-mingw32-gcc --target x86_64-windows-gnu


### 4.3 Linux 依赖

Debian/Ubuntu 推荐安装：

  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake pkg-config \
    libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libxext-dev \
    libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev

要点：
- Linux 构建会生成 Wayland 协议头，依赖 wayland-scanner（由 libwayland-bin 提供）。
- build.sh 会从 protocols 目录生成协议代码到 build/wayland-protocols。


### 4.4 已验证状态

- macOS: 构建可通过（Cocoa 后端）。
- Linux 容器: WSI 构建可通过（X11 + Wayland 同时编入）。


## 5. 调试容器配置（Apple container）

本节给出可复用的 Linux 调试容器配置，用于 WSI 与上层编译验证。

### 5.1 启动 container 服务

  container system start

若服务卡在 APIServer 检查，可先执行：

  container system stop
  container system start


### 5.2 创建可调试容器（推荐）

建议使用固定名称、挂载源码目录、并提高内存：

  container run -d \
    --name sc-ubuntu-build \
    -c 4 \
    -m 4G \
    --mount type=bind,source=/Users/<your-user>/dev/c/sc,target=/work/sc \
    ubuntu:22.04 sleep infinity

进入执行：

  container exec sc-ubuntu-build sh -lc 'cd /work/sc && uname -a'


### 5.3 容器内执行 WSI 构建

  container exec sc-ubuntu-build sh -lc 'cd /work/sc/templates/utils/wsi && ./build.sh'


### 5.4 容器内执行主工程构建

推荐新建独立构建目录，避免宿主与容器路径混用导致 CMakeCache 路径冲突：

  container exec sc-ubuntu-build sh -lc 'cd /work/sc/compiler && cmake -S . -B build-linux'
  container exec sc-ubuntu-build sh -lc 'cd /work/sc/compiler && cmake --build build-linux -j1'

说明：
- 若内存较小（例如 1GB），并行编译可能触发 cc1plus 被系统杀死。
- 出现 OOM 时降并发到 -j1，或在创建容器时增加 -m 内存。


### 5.5 常见问题

1) APIServer 启动失败，日志含 entity default already exist
- 现象：container system start 卡住，日志出现 entity default already exist。
- 通常与 networks/default 状态冲突有关。
- 处理思路：停止对应服务，备份冲突目录，再重新 start。

2) CMakeCache 路径不一致
- 现象：提示当前 CMakeCache.txt 与创建时路径不一致。
- 处理：改用新的构建目录（如 build-linux），不要复用宿主生成目录。

3) Wayland 协议生成失败
- 现象：提示找不到 wayland-scanner。
- 处理：安装 libwayland-bin 与 wayland-protocols。


## 6. 维护建议

- 优先以内置 build.sh 作为 WSI 构建入口，避免手写平台宏组合。
- 修改平台后端接口时，以 src/internal.h 中平台 vtable 为最终约束。
- Linux 回归建议至少覆盖：
  - WSI 单库构建（X11 + Wayland 同编）
  - 编译器主工程构建（build-linux）

