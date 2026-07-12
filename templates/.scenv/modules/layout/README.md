# layout 模块说明

## 1. 定位

`layout` 是**平台无关**的逻辑层，维护一棵「视图节点树」，负责逻辑布局与
视图结构，并反过来**驱动**各平台原生控件/窗口的位置与层叠顺序。

四层架构中：
- `wsi`：root window 与事件循环（平台相关）。
- `layout`：逻辑视图树 + 布局驱动（本模块，**平台无关**）。
- `ui`：各平台原生控件/子窗口封装（平台相关，被 layout 驱动）。
- `surface`：从 native handle 创建渲染 surface（平台相关）。

## 2. 为什么独立成层

逻辑布局与视图树是平台无关的事：
- 个别平台（如 Android）内置了自己的 layout/tree 机制，但各平台互不相同，
  很多桌面平台根本没有，无法跨平台。
- 因此把「逻辑树 + 布局」抽成独立模块，反过来驱动各平台原生控件的位置，
  而不是依赖平台自带能力。

## 3. 驱动契约（sink）

`layout` 不认识「控件」或「窗口」，只认识**能被摆放的东西**——即实现了
`sc_layout_sink`（`set_frame` / `set_z`）的对象：

```c
typedef struct sc_layout_sink {
    void (*set_frame)(void* target, int x, int y, int width, int height);
    void (*set_z)(void* target, int z);
} sc_layout_sink;
```

任何暴露了 pos/size/z-order 的原生对象，绑定一个 sink 即可被 layout 驱动：

```c
sc_layout_node_bind(node, &my_sink, my_target);
...
sc_layout_apply(ctx);   /* 把各节点几何/层叠推送到其 target */
```

`ui` 模块为其 `sc_ui_window` / `sc_ui_control` 提供了现成的 sink，见 ui 模块。

## 4. 核心 API

- 上下文：`sc_layout_create` / `sc_layout_destroy` / `sc_layout_get_root`
- 节点树：`sc_layout_node_create` / `_destroy` / `_parent` / `_first_child` / `_next_sibling`
- 几何/层叠：`sc_layout_node_get_frame` / `_set_frame` / `_get_z` / `_set_z` / `_get_flags` / `_set_flags`
- 驱动：`sc_layout_node_bind` / `_unbind` / `sc_layout_apply`

## 5. 模块边界

模块内负责：
- 逻辑视图树的增删与遍历。
- 每个节点的几何（pos/size）、z-order 与布局 flags。
- 通过 sink 把布局结果驱动到外部可摆放对象。

模块暂不负责：
- 自动布局求解（盒模型/弹性/网格等）——当前 `sc_layout_apply` 仅传播
  已设置的 frame/z，求解算法将在后续版本补充。
- 任何平台相关的渲染、输入、窗口创建。

## 6. 目录

- `layout.h`：公共 C API。
- `layout.sc`：sc FFI 封装。
- `src/layout.c`：通用骨架实现。
- `build.sh`：独立静态库构建脚本。

## 7. 构建

```sh
cd templates/.scenv/modules/layout
./build.sh
```

产物：
- `liblayout.<triple>.a`
- `liblayout.a`
