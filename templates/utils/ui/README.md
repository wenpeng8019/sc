# ui 模块说明

## 1. 定位

`ui` 是 `wsi` 之上的各平台原生组件封装层，只提供两类平级的「物理原生对象」：
原生子窗口（`sc_ui_window`）与原生控件（`sc_ui_control`），两者对外都只暴露
 pos/size/z-order，供外部驱动器操作其位置与层叠。

四层架构中：
- `wsi`：root window 与事件循环（平台相关）。
- `layout`：逻辑视图树 + 布局驱动（平台无关），通过 ui 定义的 `sc_ui_sink` 接口来操作 ui。
- `ui`：各平台原生子窗口/控件封装（本模块，平台相关）。
- `surface`：独立模块，从 native handle 封装渲染 surface（平台相关）。

`ui` 不直接做 GPU 渲染，也不维护「逻辑视图树 / 布局」（那属于 `layout`）。

## 2. 一期目标

一期目标覆盖以下基础控件类型：
- label
- edit
- text
- button
- checkbox
- radiobox
- combo
- list

当前实现重点：
- `sc_ui_ctx` + `sc_ui_window` + `sc_ui_control` 三类对象模型。
- 子窗口树增删与几何/层叠（z-order）管理。
- 控件基础属性读写。
- 子窗口级 native handle 读写接口（供 surface 模块消费）。
- `sc_ui_sink` 驱动接口标准：ui 自定义并实现，供 layout 等外部组件操作位置/层叠。

## 3. 模块边界

模块内负责：
- UI 上下文、子窗口树、控件生命周期。
- 控件基础属性（位置、尺寸、层叠、文本、勾选态、选项列表）。
- 各平台原生子视图/控件的创建、销毁与属性下发（当前 macOS 已实现）。
- 为每个子窗口挂载 native display/window 句柄（可由平台后端填充）。
- 定义并实现 `sc_ui_sink` 驱动接口，供外部组件驱动 pos/size/z-order。

模块暂不负责：
- 逻辑视图树 / 布局计算（属于 `layout` 模块）。
- 系统主题适配。
- 字体排版与文本测量。
- 完整输入法、焦点链与无障碍。

## 4. 目录

- `ui.h`：公共 C API。
- `ui.sc`：sc FFI 封装。
- `src/ui.c`：平台无关的共享逻辑（对象树/控件链表 + 调用后端 hook）。
- `src/ui_internal.h`：共享数据结构 + 平台后端 hook 契约。
- `src/cocoa_ui.m`：macOS(Cocoa) 后端（NSView/NSControl，参考 wsi）。
- `src/null_ui.c`：其他平台的空实现（仅维护逻辑树，不建原生控件）。
- `build.sh`：scc 薄包装（`scc . --build`；后端宏在模块 [.sc](.sc) 段配置）。

## 5. 构建

```sh
cd templates/utils/ui
./build.sh                       # → libui.a
./build.sh --target <目标档>     # → libui.<suffix|triple>.a（交叉）
```

说明：
- 该模块依赖 `wsi.h`（`sc_window*` 与平台常量）。
- 链接阶段需同时具备 `libui.a` 与 `libwsi.a`。
- macOS 后端使用 Cocoa，最终可执行文件链接时需带框架：
  `SCC_LDFLAGS="-framework Cocoa -framework IOKit -framework CoreFoundation"`。
- 平台后端现状：macOS(Cocoa) 已实现；X11/Win32/Wayland 走 null 空实现待补。

## 6. 典型用法

```sc
inc ../wsi/wsi.sc
inc ui.sc

fnc main: i4
    if wsi_init() == 0
        return 1

    var win: ::sc_window& = wsi_win_create(800, 600, "ui demo", nil, nil)
    var ui: ::sc_ui_ctx& = ui_create(win)

    var root: ::sc_ui_window& = ui_get_root_window(ui)
    var video: ::sc_ui_window& = ui_window_create(ui, root, 20, 20, 640, 360, 0)

    # 后续平台后端可把子窗口 native handle 填到 video
    ui_window_set_native_window(video, wsi_get_platform(), nil, nil)

    var btn: ::sc_ui_control& = ui_button_create(ui, 20, 400, 120, 32, "Play")
    ui_control_set_checked(btn, 1)

    while wsi_win_get_should_close(win) == 0
        wsi_wait_events()

    ui_destroy(ui)
    wsi_win_destroy(win)
    wsi_terminate()
    return 0
```

## 7. 下一步建议

建议后续按下面顺序推进：
1. 增加 `ui_backend` 抽象层，负责平台 child window/subview 创建。
2. 将命中测试、焦点链和键盘编辑下沉到 `ui_backend` + core。
3. 用独立 `surface` 模块消费 `ui_window_get_native_*`。
4. 最后对接实际渲染后端。 
