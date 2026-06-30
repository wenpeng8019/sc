# 交叉编译参考

scc 转译为 C 后调用 C 工具链，因此交叉编译的本质就是**把 scc 调用的工具链整套换成目标工具链**：
目标 `gcc`/`ar`/`objcopy`、目标机器选项、目标 sysroot，以及（裸机时）目标适配的 builtins 库。

本机构建（不指定目标）行为与以往完全一致；只有在给出目标三元组/工具时才进入交叉路径。

---

## 1. 一分钟上手

把所有目标设置写进一个**目标档**（`.target` 文件，语法同 `.sc` 配置：每行 `key = value`），
用 `--target` 加载：

```sh
# 64 位 ARM Linux（托管 OS）
scc app.sc --build -o app --target examples/targets/aarch64-linux.target

# Cortex-M4 裸机，产出可烧录镜像
scc fw.sc --build -o fw.bin --target examples/targets/cortex-m4.target \
          --builtins path/to/m4-builtins
```

示例目标档见 [examples/targets/aarch64-linux.target](examples/targets/aarch64-linux.target)
与 [examples/targets/cortex-m4.target](examples/targets/cortex-m4.target)。

---

## 2. 配置项

每一项都可三种方式给出，**优先级：环境变量 `SCC_*` > `--target` 目标档 > 当前目录 `.sc` 配置 > 内置默认**。

| 目标档键 | 环境变量 | 作用 |
| --- | --- | --- |
| `cc` | `SCC_CC`（亦认 `CC`） | 目标 C 编译器，如 `aarch64-linux-gnu-gcc` |
| `ar` | `SCC_AR` | 静态库归档器（构建 `.a` 时用），默认 `ar` |
| `objcopy` | `SCC_OBJCOPY` | 目标转换器（产 `.bin`/`.hex` 时用），默认 `objcopy` |
| `target_flags` | `SCC_TARGET_FLAGS` | 目标机器选项，**同时进入编译与链接两步**，如 `-march=armv8-a` |
| `sysroot` | `SCC_SYSROOT` | 目标 sysroot → `--sysroot`，同样进入编译与链接 |
| `cflags` | `SCC_CFLAGS` | 编译附加选项（如 `-ffreestanding -Os`） |
| `ldflags` | `SCC_LDFLAGS` | 链接附加选项（如 `-nostartfiles -T linker.ld`） |
| `triple` | `SCC_TARGET_TRIPLE` | 目标三元组，驱动**平台表**自动推导线程库/调试打包 |
| `target_suffix` | `SCC_TARGET_SUFFIX` | `add` 预编译库优先匹配 `<名>.<suffix>.<ext>` 变体（空=回退 `triple`） |
| `threads` | `SCC_THREADS` | 线程库链接选项（`-lpthread` / `none`），显式覆盖平台表 |
| `debug` | `SCC_DEBUG` | 链接后调试打包（`dsymutil` / `none`），显式覆盖平台表 |
| `freestanding` | `SCC_FREESTANDING` | `1` 表示裸机目标（无托管运行时） |
| `platforms` | `SCC_PLATFORMS` | 外置平台表文件（见 §4） |
| `run` | `SCC_RUN` | 运行包装器/模拟器（run 模式用，见 §5） |

> 为什么 `target_flags`/`sysroot` 要同时进入链接？因为交叉 `gcc` 在**链接**时也据机器选项与
> sysroot 选择 multilib、`libgcc`、`crt0` 等启动/运行时组件；只在编译阶段给会导致链接错配。

---

## 3. 平台表：自动推导线程库与调试打包

不同目标对“线程怎么链”“调试信息怎么打包”有固定差异。scc 内置一张**平台表**，
按 `triple` 的平台族自动推导，免去逐项手写：

| 平台族（三元组含） | 线程库 `threads` | 调试打包 `debug` |
| --- | --- | --- |
| `darwin`/`apple` | （内置，无需） | `dsymutil` |
| `linux` | `-lpthread` | `none` |
| `mingw`/`windows` | （无需） | `none` |
| `none`/`eabi`/`elf`（裸机） | （无需） | `none` |

- **谁说了算**：显式 `threads`/`debug` 配置 > 外置平台表 > 内置平台表。
- **未知目标**：若 `triple` 不被任何平台表覆盖、又未显式声明 `threads`/`debug`，
  scc 报错并要求你声明（裸机档 `freestanding = 1` 天然免此校验）。
- 不写 `triple` 即本机构建，使用本机平台族的行为（与以往一致）。

---

## 4. 外置平台表（可选）

需要扩展/覆盖内置表时，提供一个平台表文件，用 `platforms` 指向它。每行：

```
# pattern : threads : debug
linux-musl   : -lpthread : none
myrtos       :           : none
```

`pattern` 为三元组子串，命中即覆盖内置表；`threads` 写 `none` 或留空表示无需线程库。

---

## 5. 运行交叉产物（run 模式）

默认 run 模式会 `fork+exec` 直接执行产物——但交叉产物**本机跑不了**。两种处理：

- 配置 `run = <模拟器>`（如 `run = qemu-aarch64 -L /opt/sysroots/aarch64`），
  scc 在 run 模式下用它包装执行产物。
- 不配置且目标与本机不同族时，run 模式直接**报错**，提示改用 `--build` 生成产物。

```sh
# 经 qemu 在本机跑 aarch64 产物
scc app.sc --target examples/targets/aarch64-linux.target -- arg1 arg2
```

---

## 6. 产物类型

由 `-o` 后缀决定（与本机构建一致，新增裸机镜像）：

| 后缀 | 产物 | 合成方式 |
| --- | --- | --- |
| 无后缀 / `.elf` | 可执行文件 | 目标 `cc` 链接 |
| `.a` | 静态库 | 目标 `ar rcs` |
| `.so` / `.dylib` | 动态库 | 目标 `cc -shared` |
| `.bin` | 裸机原始镜像 | 先链接临时 `.elf`，再 `objcopy -O binary` |
| `.hex` | Intel-HEX 镜像 | 先链接临时 `.elf`，再 `objcopy -O ihex` |

---

## 7. 裸机/freestanding 与 `--builtins`

裸机目标无托管 libc：`print`、文件、时间等内置库的默认实现（依赖 `stdio`/`malloc`/`pthread`）
无法直接用。做法是**另建一套目标适配的 builtins 目录**，里面 `.sc`/`.h` 接口契约不变，
只把 `*_impl.c`（与按需的 `platform.h`）替换为面向目标的实现（如把 `print` 写到 UART），
再用 `--builtins <dir>` 以最高优先级指向它：

```sh
scc fw.sc --build -o fw.bin \
    --target examples/targets/cortex-m4.target \
    --builtins boards/m4/builtins
```

`--builtins` 同时影响 `inc` 模块解析与自动链接的实现拾取，因此交叉时会自动用目标适配实现，
而非默认主机实现。启动文件与链接脚本经 `ldflags` 传入（见 cortex-m4 示例档）。

---

## 8. 速查

```sh
# 仅用环境变量做一次性交叉（不写目标档）
SCC_CC=aarch64-linux-gnu-gcc \
SCC_TARGET_FLAGS=-march=armv8-a \
SCC_SYSROOT=/opt/sysroots/aarch64 \
SCC_TARGET_TRIPLE=aarch64-linux-gnu \
  scc app.sc --build -o app

# 目标档 + 环境变量临时覆盖某项（环境变量优先）
SCC_SYSROOT=/other/sysroot scc app.sc --build -o app --target my.target
```

---

## 9. 另一条路：远程工具链构建

交叉编译要在本机备齐目标平台的 `cc`/sysroot/`ar`/`objcopy`。若手头**正好有一台目标
平台的主机**（比如要 Linux 产物且有 Linux 机器），可以不装交叉工具链，直接把编译/链接
放到那台主机上做——见 [compiler.md](compiler.md) §4.4「远程工具链构建」。

要点对比：

| | 交叉编译 | 远程构建 |
| --- | --- | --- |
| 本机需求 | 目标 `cc` + sysroot | 仅 sc→C 代码生成 |
| 适用 | 任意目标（含裸机） | 有可 SSH 登录的目标平台主机 |
| `add` 原生源码 | 本机交叉编译 | 远端现场重编（跨架构也对） |
| `add` 预编译库 | 需目标架构版本 | 同架构可用，跨架构需远端自备 |
| 产物 | 全部类型 | 目前仅可执行 |

> `add` 预编译库的多平台：源码里只写 `add libfoo.a`，再配 `target_suffix`（或留空用 `triple`），
> scc 会优先链接同目录的 `libfoo.<suffix>.a` 目标变体，找不到才回退 `libfoo.a` —— 本机原生、
> 交叉编译、远程构建共用同一份 `add` 行。详见 [compiler.md](compiler.md) §4.4。

```sh
# 不装 Linux 交叉工具链，借一台 Linux 主机产出 Linux 可执行文件
scc --build -o app-linux --target examples/targets/remote-linux.target app.sc
```
