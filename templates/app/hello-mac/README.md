# hello-mac —— macOS 桌面 app 脚手架

最小的 sc 原生窗口 app：打开窗口、跑主循环、响应输入、干净退出。
拷走本目录改名即为你自己 app 的起点。

## 前置

- 已构建 scc 编译器（`compiler/build/scc`）。
- macOS 自带 Cocoa/Metal，无需额外依赖。

## 构建并运行

```sh
./build.sh
```

窗口打开后：按 **ESC** 或点关闭按钮退出。终端每秒打印一次帧率作自检。

## 说明

- **主循环归属**：桌面平台由程序自己掌控——`main` 里显式 `while` 泵事件。
  这与 iOS（系统 app 对象封装循环、回调驱动）、Android（framework 接管、
  无 main）不同，详见 [../README.md](../README.md)。
- **链接**：`inc wsi.sc` 会按目标平台自动注入 wsi 模块 `.sc` 段声明的框架
  （macOS = `-framework Cocoa -framework IOKit -framework CoreFoundation
  -framework QuartzCore`），故无需手动传 `SCC_LDFLAGS`。
- **渲染**：本模板只做纯窗口。要上图形，在主循环里接 gpu/gfx 模块
  （见 [templates/demo/gpu_demo.sc](../../demo/gpu_demo.sc)）。
- **输入**：sc 的 FFI 不导出「按值传结构体」的窗口回调表，故这里用状态查询
  （`wsi_key` 等）轮询输入，而非注册回调。
