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
  - sc_wsi_app_startup
  - sc_wsi_app_cleanup
  - sc_wsi_get_error
  - sc_wsi_set_error_callback

- 事件循环
  - sc_wsi_loop_poll
  - sc_wsi_loop_wait
  - sc_wsi_loop_pulse

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
  - sc_wsi_get_cursor_pos / sc_wsi_set_cursor_pos
  - sc_wsi_create_cursor / sc_wsi_destroy_cursor
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

build.sh 已降为 scc 薄包装（仅做 Wayland 协议头生成 + 调
`scc . --build`）；平台后端宏与链接项在模块 [.sc](.sc) 段配置
（见 compiler.md §7.4/§7.6）。在模块目录执行：

  cd templates/utils/wsi
  ./build.sh

产物：libwsi.a（实体文件）


### 4.2 交叉构建

工具链经 scc 机制配置（SCC_* 环境变量 / --target 目标档，
见 compiler.md §5），产物自动带变体后缀：

  SCC_TARGET_TRIPLE=aarch64-linux-gnu SCC_CC=aarch64-linux-gnu-gcc ./build.sh
  ./build.sh --target ../../targets/aarch64-linux.target
  # → libwsi.<target_suffix|triple>.a

（MSVC 变体经远程构建产出，见 compiler.md §6）


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


## 7. 已知坑：WSLg 下 Wayland 回退装饰边框消失（2026-07 已修复）

### 现象

在 WSLg（WSL2 + Windows 远程桌面呈现）上以 Wayland 后端运行时，
窗口的回退装饰（fallback CSD：标题条 + 四条灰色边框）表现异常：

- 底边框在窗口宽度小于某个阈值时整条消失；拖宽超过阈值又出现。
- 窗口高度小于某个阈值时，左右边框也消失。
- 触发消失/恢复的 resize 之后，标题条还可能整块变透明。
- 同一二进制在真实 weston / GNOME / KDE 上四条边永远正常。
- 同一二进制强制走 X11（`WAYLAND_DISPLAY=` 设为无效名）时边框完整
  （X11 下是 WM 服务端装饰，与本问题无关）。

### 根因

上游 WSI 的回退装饰实现是：**一个共享的 1×1 灰色 wl_shm buffer**，
每条边一个子表面，通过 `wp_viewport_set_destination()` 把 1×1 放大到
边框的逻辑尺寸（例如 1×1 → 488×4）。

这在协议上完全合法，常规合成器（weston/GNOME/KDE）在服务端合成，
放大 1×1 毫无问题。但 WSLg 的架构不同：

1. WSLg 的合成器是 microsoft/weston-mirror 的 RDP RAIL 后端
   （rdprail-shell）。每个 wl_surface —— 包括每个子表面 ——
   都被映射成 Windows 端 msrdc 进程里一个独立的 `RAIL_WINDOW` HWND。
2. 表面内容不在 Linux 侧合成，而是把**原始 buffer**（这里就是 1×1）
   通过 RDP graphics 通道发给 msrdc，再用
   `MapSurfaceToScaledWindow`（rdprail.c）让 **Windows 客户端负责把
   1×1 拉伸到目标窗口大小**。
3. msrdc 对这种极端放大倍率（1→数百倍，且倍率随窗口尺寸变化）
   的表面→窗口缩放映射存在渲染 bug：某些尺寸/倍率组合下分层窗口
   （`WS_EX_LAYERED`）不被绘制——HWND 存在、IsWindowVisible=TRUE、
   位置正确，但内容从未上屏，视觉上就是"边框消失"；
   并且会连带破坏相邻装饰窗口（标题条）的重绘。

诊断证据链：

- weston 日志（/mnt/wslg/weston.log）每帧打印
  `surface width/height doesn't match with buffer (windowId:0x..)`
  `surface width 488, height 4 / buffer width 1, height 1`，
  即服务端确实以 1×1 buffer + 表面尺寸不匹配的状态在推送。
- 在 Windows 宿主枚举 msrdc 的 HWND：四条边的 `RAIL_WINDOW` 全部
  存在、可见、坐标与 z 序都正确——排除了 Linux 侧几何/子表面问题。
- 屏幕取色：消失的边框区域是桌面底色；把该 HWND 挪到内容区中间，
  部分行有内容部分行没有——内容位图残缺，实锤客户端缩放绘制失败。
- 客户端侧曾系统排除：与 buffer_scale（HiDPI）无关、与 CJK 字体
  加载无关、与子表面创建顺序无关、与 y 坐标是否越出主表面无关；
  `xdg_surface_set_window_geometry` 声明含装饰几何也无效
  （rdprail-shell 的 `shell_backend_get_window_geometry` 会把几何
  强制夹回主表面范围，见 weston-mirror shell.c）。

### 修复

`src/wl_window.c`：回退装饰不再使用 1×1 buffer + viewport 放大，
改为**每条边分配与自身逻辑尺寸一致的真实 wl_shm buffer**（纯色填充）：

- `createFallbackEdgeBuffer(width, height)`：按边框实际尺寸生成
  灰色 (224,224,224,255) buffer。
- `createFallbackEdge()`：直接 attach 真实尺寸 buffer，不再创建
  `wp_viewport`。
- `resizeFallbackEdge()`：resize 时重建对应尺寸 buffer 并重新 attach
  （旧 buffer 销毁）。
- `SC_fallbackEdgeWayland` 结构：`viewport` 成员替换为每边独立的
  `buffer`；原共享的 `fallback.buffer` 字段删除。

代价：每条边多几十 KB 内存、resize 时重建小 buffer——可忽略。
收益：任何合成器上语义完全等价，且绕开 msrdc 的缩放映射 bug。

### 经验

- WSLg 不是普通 Wayland 环境：每个子表面 = 一个远程 RDP 窗口，
  缩放由 Windows 客户端执行。凡是依赖"合成器把小 buffer 放大"的
  技巧（1×1 纯色 + viewport 是常见手法）在 WSLg 上都可能踩雷。
- WSLg 永远暴露 `wayland-0`，`wl_display_connect(NULL)` 会默认连上；
  要强制 X11 必须把 `WAYLAND_DISPLAY` 设为无效值，仅 unset 无效。
- 排查这类"某条边框消失"问题的有效路径：
  1. `/mnt/wslg/weston.log` 看服务端如何理解各表面；
  2. Windows 宿主 `EnumWindows` 枚举 msrdc 的 `RAIL_WINDOW` HWND
     （需通过计划任务在交互会话执行，SSH 会话看不到桌面窗口）；
  3. `GetPixel` 屏幕取色验证实际上屏内容。


## 8. Wayland 正规窗口装饰：libdecor（标题栏 + 最小化/最大化/关闭按钮）

### 结论

Wayland 下正规的窗口装饰（带「应用标题」文字和最小化 / 最大化 /
关闭按钮的标题栏）**不是手绘的，而是由 libdecor 的 cairo 插件绘制**。
wsi 早已完整对接 libdecor，且默认就优先使用它——无需改任何代码，
只需在运行环境里装好 libdecor 运行库及其绘制插件即可。

§7 的回退装饰（fallback CSD：四条灰边 + 简单标题条）只是
**libdecor 不可用时的兜底**，不提供按钮。

### wsi 里的装饰选择链路

1. 选项 `libdecorMode`，默认值 `SC_WAYLAND_PREFER_LIBDECOR`
   （见 `src/init.c`：`.libdecorMode = SC_WAYLAND_PREFER_LIBDECOR`，
   声明在 `src/internal.h` 的 `int libdecorMode`）。
2. 初始化时（`src/wl_init.c`）若该选项为 PREFER，则
   `dlopen("libdecor-0.so.0")` 并逐个解析所有 `libdecor_*` 符号；
   全部成功才置上 `g_wsi.wl.libdecor.handle`，随后 `libdecor_new`
   建出 `g_wsi.wl.libdecor.context`。**任一符号缺失或 dlopen 失败，
   整个 libdecor 支持会被清零。**
3. 建窗口时（`src/wl_window.c` 的 `createShellObjects`）：

   ```c
   if (g_wsi.wl.libdecor.context) {          // libdecor 可用
       if (createLibdecorFrame(window))      // → cairo 插件画标题栏 + 按钮
           return true;
   }
   return createXdgShellObjects(window);     // 否则退到 xdg-shell + 回退 CSD
   ```

### 坑：装饰"没按钮 / 退化成灰边"其实是 libdecor 没装

在干净的 WSL Ubuntu-22.04 上，系统默认**不带 libdecor**。此时：

- `dlopen("libdecor-0.so.0")` 返回 NULL → `context` 为空 →
  直接落到 `createXdgShellObjects` 的回退 CSD → 只有灰边、没有按钮，
  且会撞上 §7 描述的 1×1 buffer msrdc bug（底边消失）。

它是**运行时依赖缺失**，不是代码 bug。选项和代码一直是对的。

### 修复：装 libdecor + cairo 插件

```bash
apt-get install -y libdecor-0-0 libdecor-0-plugin-1-cairo
```

装完应出现：

- 运行库：`/lib/x86_64-linux-gnu/libdecor-0.so.0`
- 绘制插件：`/usr/lib/x86_64-linux-gnu/libdecor/plugins-1/libdecor-cairo.so`

之后 wsi 的 `dlopen` 成功 → 走 libdecor frame → cairo 插件绘出
标题栏（应用标题文字 + 最小化 / 最大化 / 关闭按钮），窗口即为正常
应用外观。也可用 `-plugin-1-gtk` 代替 cairo 插件（外观随 GTK 主题）。

### 验证 libdecor 是否就位

```bash
ldconfig -p | grep libdecor                                # 有 libdecor-0.so.0
ls /usr/lib/x86_64-linux-gnu/libdecor/plugins-1/           # 有 libdecor-*.so 插件
```

若二者缺任一，装饰就会退化到 §7 的回退 CSD。

### 经验

- 换到新机器 / 新容器跑 Wayland 前，先确认 libdecor 及其插件已装，
  否则会以为是代码丢了装饰，实际只是运行时依赖没到位。
- 不要为了补按钮去手绘标题栏——libdecor 已经把这件事做好了，
  手绘只会和 libdecor / 回退 CSD 两条路径重复且容易踩 WSLg 缩放坑。

