# plib — 跨平台原生库模板

展示 sc 项目如何引入独立编译的跨平台原生库（C/ObjC 实现，`.a` 链接）。

## 目的

sc 是 C 前端，编译管线是 `sc → C → cc`。但部分平台能力（macOS 窗口、系统框架等）必须用 Objective-C 或特定编译器编译，**无法通过 sc 的 `add` 直接编译源码**。

本模板的解法：**库独立编译，sc 只做 FFI 声明 + `add` 链接**。

## 目录结构与职责

```
plib/
├── plib.h              # 公共 C API（所有平台共用同一个）
├── plib_darwin.m        # macOS 实现（ObjC）
├── plib_linux.c         # Linux 实现
├── plib_win.c           # Windows 实现
├── plib.sc              # sc FFI 侧（inc plib.h + @fnc + add libplib.a）
├── build.sh             # 平台检测 → 编译 → 打包 .a
├── hello_demo.sc        # 演示入口
└── README.md
```

## 使用方法

```bash
# 1. 编译原生库
cd templates/plib
./build.sh                        # 宿主构建，自动检测平台和工具链

# 2. 运行演示
scc hello_demo.sc                 # libplib.a 由 add 指令自动链接
```

交叉编译（双方指定同一 `triple`）：

```bash
CC=aarch64-linux-gnu-gcc \
  ./build.sh --target aarch64-linux-gnu    # 产出 libplib.aarch64-linux-gnu.a

SCC_TARGET_TRIPLE=aarch64-linux-gnu \
  scc hello_demo.sc                        # add libplib.a 自动匹配变体
```

## 平台适配机制

- **`build.sh`** 从 `--target` 或本机 `cc -dumpmachine` 取得三元组（如 `arm64-apple-darwin24.6.0`），按前缀选择源文件（`*-apple-darwin*` → `.m` / `*-linux-*` → `.c` / `*-windows-*` → `.c`），产出 `libplib.<triple>.a`
- **sc `add libplib.a`** 在交叉编译时（`SCC_TARGET_TRIPLE` 非空），编译器优先匹配 `libplib.<triple>.a` 变体（`resolveAddArtifact`）；本机构建则通过软链接 `libplib.a -> libplib.<triple>.a` 直接解析
- **`@fnc name::`** 声明 C 函数，sc 编译器生成 `sc_` 前缀的外部原型，C 侧函数名需对应 `sc_` 前缀

## 工具链

| 平台 | 编译器 | 打包工具 | 源文件 |
|---|---|---|---|
| macOS | `cc`（clang + ObjC） | `ar` | `plib_darwin.m` |
| Linux | `cc`（gcc/clang） | `ar` | `plib_linux.c` |
| Windows MSVC | `cl` | `lib` | `plib_win.c` |
| Windows MinGW | `cc`（gcc） | `ar` | `plib_win.c` |

工具链自动检测（Windows 上优先检测 `cl`）；也可通过 `--cc` / `--ar` 显式指定。

## 基于此模板的其他库

- `templates/win/` —— 跨平台 UI 窗口（计划）
- `templates/gfx/` —— 跨平台 GL Context 创建（计划）
