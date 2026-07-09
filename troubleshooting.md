# 坑与解（Troubleshooting）

本文汇总 scc 交叉编译 / 远程工具链构建中踩过的坑及其根因与解法。
按「现象 → 根因 → 解法」组织，便于按报错关键字检索。

---

## 1. Linux 目标链接报 `undefined reference to shm_open / shm_unlink`

**现象**：交叉（或本机）编译到 **Linux** 的程序，只要用到 `mem` 模块（跨进程共享内存），
链接期报：

```
undefined reference to `shm_open'
undefined reference to `shm_unlink'
```

**根因**：`builtins/mem/mem_impl.c` 的 POSIX 共享内存实现调用 `shm_open`/`shm_unlink`。
在 **glibc < 2.34** 上这两个符号位于 `librt`（非默认链接的 `libc`），需显式 `-lrt`。
macOS 把它们放在 `libc`、Windows 走 Win32 API，故本机 macOS 构建从未暴露此依赖。

**解法**：**已内建修复**。编译器在链接 `mem` 模块且目标平台族为 `linux` 时自动追加 `-lrt`
（判定按「显式 `triple`，缺省回退宿主」，兼顾本机 Linux 构建与交叉到 Linux）。
见 [compiler/src/main.cpp](compiler/src/main.cpp) `compileUnitsToObjects` 中 `scStem == "mem"` 分支。

无需再手动 `SCC_LDFLAGS=-lrt`。若使用极老工具链或自定义平台仍缺符号，可临时叠加：

```sh
SCC_LDFLAGS=-lrt scc app.sc --build -o app --target your-linux.target
```

---

## 2. 对比远程 MSVC 输出时结果被截断 / `Illegal byte sequence`

**现象**：把远端 Windows（MSVC `cl.exe`）跑出的输出抓回本机 `diff`，发现输出在中途被
**截断**，或管道里的 `tr` 直接报 `tr: Illegal byte sequence` 后什么都不输出。

**根因**：两条独立的坑叠加：

1. **locale 坑**：MSVC 的中文（GBK）警告/诊断字节流经 macOS 的 `tr`（默认 `C`/UTF-8 locale）时，
   遇到非法多字节序列即 **中止并截断其后全部输出**。
2. **流混淆坑**：MSVC 逐文件编译回显（`scm__..._sc.c`）与警告走 **stderr**，
   若把 stderr 一并纳入对比，会把编译噪声当成程序输出，导致「假不一致」。

**解法**：

```sh
# 1) 强制字节级 locale，避免 tr 因非法字节中止
... | LC_ALL=C tr -d '\r'

# 2) 只比较程序 stdout，丢弃编译回显/警告
scc app.sc --target templates/targets/windows-x64.target 2>/dev/null | LC_ALL=C tr -d '\r'
```

对比本机与远端时，两侧都用同一套 `2>/dev/null | LC_ALL=C tr -d '\r'` 规整后再 `diff`。

---

## 3. 远程 MSVC 构建：部分机制/模块暂不支持

远程工具链构建把本机生成的 C 推到 Windows 主机用原生 `cl.exe` 编译。以下情形当前不支持，
需规避（其余语言机制与标准模块均可远程 MSVC 构建）：

| 不支持项 | 根因 | 规避 |
| --- | --- | --- |
| `sync<q>`（队列定向同步调用） | 仍发出 GNU 语句表达式 `({...})`，`cl` 不支持 | 改用普通 `sync` |
| `forin` 显式 `step N` | 同上，`({...})` | 用默认步长 |
| `ssl` 模块（mbedTLS/OpenSSL） | vendor 静态库需为目标现场编译并烘进产物，远程分发只上传生成的 `.c`，未搬运/远端重编 vendor 静态库 | 远程构建避开 TLS |
| 异步（`async` / `op` 的 libuv 后端） | 同上，依赖 `libuv.a` vendor 静态库 | 远程构建避开异步机制 |

MSVC 远端编译配方要点（见 [templates/targets/windows-x64.target](templates/targets/windows-x64.target)）：
`cl.exe` 无法读 stdin（需落临时 `.c`）；`/utf-8` 消非 ASCII 注释的 C4819；
`/experimental:c11atomics` 启用 `<stdatomic.h>`/`_Atomic`；链接 `-l<name>` 自动转 `<name>.lib`，
本机 `-L`/绝对 `.a`/`.o` 路径远端忽略。

---

## 4. Windows 控制台中文输出乱码（UTF-8 vs GBK）

**现象**：sc 程序在中文 Windows 控制台（cmd/PowerShell）输出中文时显示为乱码（`??` 或花码），
而重定向到文件、或在 macOS/Linux 终端运行则正常。

**根因**：sc 程序内部字符串一律以 **UTF-8** 字节输出，但简体中文 Windows 控制台默认代码页是
**GBK（936）**，控制台按 GBK 解释这些 UTF-8 字节 → 乱码。这是**控制台渲染层**问题，
字节本身是正确的 UTF-8（故重定向到文件后用 UTF-8 查看正常）。

**解法**：**已内建修复**。编译器在每个程序 `main` 入口注入 `SC_CONSOLE_UTF8()`
（见 [builtins/platform.h](builtins/platform.h)，属编译器默认注入行为之一，参见
[compiler.md](compiler.md) §15.2），在任何输出前把本进程控制台的输出/输入代码页
切到 UTF-8（65001）：

```c
/* platform.h */
#if P_WIN
#define SC_CONSOLE_UTF8() do { SetConsoleOutputCP(65001u); SetConsoleCP(65001u); } while (0)
#else
#define SC_CONSOLE_UTF8() ((void)0)   /* 非 Windows：空操作 */
#endif
```

- 三种场景全覆盖：远程 MSVC 构建、原生 Windows（MinGW/MSVC）、以及非 Windows（空操作）。
- 只作用于当前进程的**控制台句柄**；stdout 被重定向到文件/管道时不改变其字节。
- 无需用户改源码或手动 `chcp 65001`。

> 备选方案（未采用）：① 编译期把字符串字面量由 UTF-8 转 GBK——有损、不可移植，且破坏
> 文件重定向；② 应用清单 `activeCodePage=UTF-8`（Win10 1903+）——需嵌入 manifest，构建更重。
> 运行时切码页最简单、跨工具链、零源码侵入，故采用。

---

## 参考


- 交叉编译总览：[compiler.md](compiler.md) §5
- 调试技巧：[debugging.md](debugging.md)
- 内置模块契约：[builtins/REFERENCE.md](builtins/REFERENCE.md)
