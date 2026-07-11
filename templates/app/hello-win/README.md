# hello-win —— Windows 桌面 app 脚手架（Win32）

最小的 sc 原生窗口 app：打开窗口、跑主循环、响应输入、干净退出。
拷走本目录改名即为你自己 app 的起点。

在 **macOS/Linux 上交叉编译**出 Windows PE 可执行文件（`hello.exe`），全程离线，
无需 Windows 机器或 MSVC。

## 前置

- 已构建 scc 编译器（`compiler/build/scc`）。
- mingw-w64 交叉工具链：
  - macOS：`brew install mingw-w64`
  - Debian/Ubuntu：`apt install mingw-w64`
- （可选）Wine，用于在本机直接跑交叉出的 `.exe`：
  - macOS：`brew install --cask wine-stable`
  - Debian/Ubuntu：`apt install wine64`

## 构建

```sh
./build.sh
```

产物 `hello.exe`：拷到 Windows 双击运行；或在
[../../targets/windows-x64-mingw.target](../../targets/windows-x64-mingw.target)
里取消 `run = wine` 注释后，`./build.sh` 会直接在本机用 Wine 跑起窗口。

窗口打开后：按 **ESC** 或点关闭按钮退出。

## 说明

- **交叉方式**：走 [windows-x64-mingw.target](../../targets/windows-x64-mingw.target)
  （本地 mingw-w64）。另有 [windows-x64.target](../../targets/windows-x64.target)
  是走 SSH 的远程 MSVC 构建，需一台 Windows 主机——按需替换 `build.sh` 里的 `TARGET`。
- **链接**：wsi 的 win32 后端依赖 GUI 库，经 `SCC_LDFLAGS` 传入
  （`gdi32/user32/shell32/imm32/ole32/oleaut32/version/uuid/dwmapi`）；
  基线 `ws2_32` 由 target 档提供。
- **wsi 库**：`build.sh` 会为 Windows 目标交叉编译
  `libwsi.x86_64-windows-gnu.a`（`add libwsi.a` 在该目标下自动匹配此变体）。
- **主循环归属**：桌面平台由程序自己掌控（显式 `while`），与 iOS/Android 不同，
  详见 [../README.md](../README.md)。
