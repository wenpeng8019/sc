# surface 模块说明

## 1. 定位

`surface` 是独立模块，只做 native render target 的封装。

三层架构中的职责：
- `wsi`：root window。
- `ui`：子空间/子窗口（逻辑层与布局层）。
- `surface`：从 native handle 创建渲染 surface。

该模块不管理窗口树，不处理输入事件，不负责渲染 API 生命周期。

## 2. 核心 API

- `sc_surface_create_from_native(platform, nativeDisplay, nativeWindow)`
- `sc_surface_destroy(surface)`
- `sc_surface_get_platform(surface)`
- `sc_surface_get_native_display(surface)`
- `sc_surface_get_native_window(surface)`

## 3. 构建

```sh
cd templates/utils/surface
./build.sh
```

产物：
- `libsurface.<triple>.a`
- `libsurface.a`

## 4. 使用方式

典型流程：
1. 从 `wsi` root window 获取 native handle。
2. 或从 `ui_space` 获取子空间的 native handle。
3. 传入 `surface_create_from_native` 得到 surface 对象。

这样可以让渲染后端仅依赖 `surface`，而不依赖 `wsi`/`ui` 的内部结构。
