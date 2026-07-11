# hello-linux —— Linux 桌面 app 脚手架（X11 · Wayland）

最小的 sc 原生窗口 app：打开窗口、跑主循环、响应输入、干净退出。
拷走本目录改名即为你自己 app 的起点。

## 前置

- 已构建 scc 编译器（`compiler/build/scc`）。
- 窗口开发库与 `wayland-scanner`（Debian/Ubuntu）：

  ```sh
  apt install libwayland-bin libwayland-dev libx11-dev libxkbcommon-dev \
              libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
  ```

## 构建并运行

```sh
./build.sh
```

窗口打开后：按 **ESC** 或点关闭按钮退出。终端每秒打印一次帧率作自检。

## 说明

- **wsi 库**：Linux 变体不随仓库入库（`libwsi.<triple>.a` 需按发行版现场编），
  故 `build.sh` 每次调 `wsi/build.sh` 本机构建——其中会先用 `wayland-scanner`
  从 `wsi/wayland-protocols/*.xml` 生成协议头。
- **X11 / Wayland 选择**：wsi 沿用 glfw 的运行时 `dlopen` 策略，同一个库同时含
  两后端，启动时按环境自动选择；链接期只需 `-lm -ldl`（由 `inc wsi.sc` 自动注入）。
- **主循环归属**：桌面平台由程序自己掌控（显式 `while`），与 iOS/Android 不同，
  详见 [../README.md](../README.md)。
- **渲染**：本模板只做纯窗口。要上图形，在主循环里接 gpu/gfx 模块。
