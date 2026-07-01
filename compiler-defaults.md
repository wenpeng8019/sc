# 编译器默认行为（隐式操作一览）

scc 把 sc 转译为 C，再驱动 C 工具链构建。这一过程中，编译器会**自动注入**一批用户源码里
看不见、但对正确运行必不可少的操作：入口初始化、模块生命周期、平台适配、链接选项等。

本文汇总这些「默认执行的操作」，便于理解生成产物、排查问题、以及判断某行为是否符合预期。
除特别标注外，这些操作**无需配置、自动生效**；多数在非相关平台上退化为空操作。

> 落点索引：代码生成见 [compiler/src/codegen_c.cpp](compiler/src/codegen_c.cpp)（`emitMainPrologue`
> / `emitMainEpilogue` 等）；链接/平台配置见 [compiler/src/main.cpp](compiler/src/main.cpp)
> （`loadToolConfig` / `compileUnitsToObjects`）；平台宏见 [builtins/platform.h](builtins/platform.h)。

---

## 1. 每个生成单元的头部

| 操作 | 说明 |
| --- | --- |
| `#include "platform.h"` | 每个生成的 `.c` 顶部统一引入，带来标准 C 头、跨平台宏（`P_WIN`/`P_LINUX`/TLS 等）、以及 Windows 下的 `<windows.h>`。 |
| 自动导入 `op.sc` | `op` 是默认导入的语言运行时模块（chain、异步内核等机制），自动纳入单元图，无需用户 `inc`。 |
| 结构/联合前置声明 | 为所有聚合类型默认输出前置声明，消除定义顺序依赖。 |
| 根模块导出接口头注入 | 非根单元在所有 `inc` 之后追加 `#include "scm_<root>.h"`，使集成单元的全局定义/操作可见。 |

---

## 2. `main` 入口序言（按执行顺序）

编译器在 `main` 函数体最前面按固定顺序注入以下语句（`emitMainPrologue`）：

| 顺序 | 注入 | 条件 | 作用 |
| --- | --- | --- | --- |
| 1 | `SC_CONSOLE_UTF8();` | 恒注入（非 Windows 空操作） | 把 Windows 控制台输出/输入代码页切到 UTF-8（65001），避免中文乱码。详见 [troubleshooting.md](troubleshooting.md) §4。 |
| 2 | `__sc_gcanary_init();` | `--check=mem` 且退化路径 | 栈哨兵填充，须先于任何全局构造以捕获构造期溢出。 |
| 3 | `sc_mod_<t>_init();` | 每个依赖模块 | 按依赖序初始化各模块（分配/注册运行时资源）。 |
| 4 | `X._class = Y_hyper_impl;` | 有 class 安装 | 安装 hyper/class 虚表。 |
| 5 | 全局构造 `fn(&var, …)` | 有需构造的全局 | 运行全局变量的构造器。 |
| 6 | token 注册 | 有 tok/dep 图 | `token_bind` 绑定句柄 + `token_depend` 注册依赖，并把编译期烘焙的图属性（深度/关键路径/扇入扇出等）写入句柄。 |

> 退化路径：`SC_HAVE_AUTO_HOOKS==0` 时（平台无自动构造器机制）才显式发出钩子调用；
> GCC/Clang/MSVC 由平台构造器机制自动触发，不重复产出（`#if !SC_HAVE_AUTO_HOOKS` 包裹）。

---

## 3. `main` 尾声（逆序析构）

`main` 返回前按与初始化相反的顺序清理（`emitMainEpilogue`）：

1. 全局析构 `fn(&var)`（**逆序**）；
2. `sc_mod_<t>_drop()` 各依赖模块销毁（**逆序**）；
3. 退出钩子 `__sc_gcanary_fini()` / `__sc_gfat_fini()`（退化路径，在所有用户析构之后）。

---

## 4. 链接期自动注入的选项

根据**目标平台**与**用到的模块**，编译器自动向最终链接命令追加选项（用户无需手写）：

| 注入 | 触发条件 | 原因 |
| --- | --- | --- |
| `-lpthread` | Linux 目标 + 用到 `m`/`op`（线程/异步内核） | POSIX 线程库。 |
| `-lrt` | Linux 目标 + 用到 `mem`（跨进程共享内存） | glibc < 2.34 的 `shm_open`/`shm_unlink` 在 librt。详见 [troubleshooting.md](troubleshooting.md) §1。 |
| `-lssl -lcrypto` 或 `mbedtls.a` | 用到 `ssl` 模块 | TLS 后端：OpenSSL 链系统动态库；mbedTLS 静态烘进产物（构建 scc 时 CMake 固化选择）。 |
| libuv（`libuv.a` + 头/框架） | `-DSCC_WITH_UV` 构建 + 用到 `op`/`async` | 异步 I/O 的 libuv 后端。 |
| `-fstack-protector-strong` | `--check=mem` 且非裸机 | C 编译器插入栈哨兵，捕获栈溢出破坏返回地址。 |

平台线程库来自平台表：Linux=`-lpthread`，macOS/Windows/裸机=空（见 `builtinPlatform`）。
配置优先级：`SCC_*` 环境变量 > `--target` 目标档 > `.sc` 配置 > 内置默认。

---

## 5. 目标平台定向与裸机

| 操作 | 条件 | 作用 |
| --- | --- | --- |
| `-D SC_TARGET_WIN` / `_DARWIN` / `_LINUX` | 显式指定交叉 `triple` 时 | 令 `platform.h` 的平台分支以「目标平台」为准，而非回退到 C 编译器默认目标。 |
| 自动 `freestanding` | 目标族为 `bare`（裸机 `*-none-eabi`/`*-elf`） | 关闭托管运行时相关注入（线程库、栈哨兵运行时等）。 |
| 未知三元组校验 | 交叉目标未被平台表覆盖且未声明 | 报错要求声明 `threads`/`debug` 或提供平台表。 |

---

## 6. 链接后步骤

| 操作 | 条件 | 作用 |
| --- | --- | --- |
| `dsymutil` | macOS 目标（平台表 `debug=dsymutil`） | 链接后生成 `.dSYM` 调试符号包。 |

---

## 参考

- 交叉编译总览：[cross-compile.md](cross-compile.md)
- 坑与解：[troubleshooting.md](troubleshooting.md)
- 编译器参考手册：[compiler.md](compiler.md)
- 内置模块契约：[builtins/REFERENCE.md](builtins/REFERENCE.md)
