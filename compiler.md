# scc 编译器参考手册

scc 是 sc 语言的编译器（C++17 实现，手写词法分析 + 递归下降语法分析，AST 与后端解耦）。
本手册覆盖编译器的能力、机制、配置与用法。语言本身的语法参考见 [syntax](syntax)，
内置模块参考见 [builtins/REFERENCE.md](builtins/REFERENCE.md)。

sc 与 C 是共生关系：scc 转译为 C 后复用整个 C 工具链（编译器、调试器、库、ABI），
同时把 C 的工作流收敛为单命令（直接运行、构建产物、头文件生成、模块依赖编译链接）。

## 1. 总览

### 1.1 编译流水线

```
源码 → lex（词法）→ parse（语法）→ semanticCheck（语义检查）→ 后端
                                                              ├─ codegen_c   → C 源码（默认 / --emit-c）
                                                              ├─ ast_json    → AST JSON（--ast）
                                                              └─ codegen_sc  → 规范化 sc 源码（--emit-sc）
```

- 同一棵 AST 喂给三个后端，保证各输出语义一致。
- 所有 AST 节点携带行号，支撑精确错误定位与 IDE 跳转。
- 编译错误通过统一异常传播，在入口集中格式化输出。

### 1.2 五种运行模式

| 模式 | 选项 | 行为 |
|---|---|---|
| 运行（默认） | 无 | 转 C → 调系统 C 编译器 → 执行 → 清理临时产物，类似 python 直接运行脚本 |
| 构建产物 | `--build` | 编译链接为持久产物：可执行文件 / 静态库 / 动态库（按 `-o` 后缀决定） |
| 转译 C | `--emit-c` | 输出 C 源码（`-o` 写文件，缺省 stdout）；有 `@` 导出对象且指定 `-o` 时额外生成同名 `.h` |
| AST 导出 | `--ast` | 输出 AST JSON 树（VSCode 插件的数据源） |
| 源码再生 | `--emit-sc` | 从 AST 再生成规范化 sc 源码（格式化器的基础） |

## 2. 命令行

```
scc <input.sc | -> [选项] [-- 程序参数...]
```

| 参数 | 说明 |
|---|---|
| `input.sc` | 输入文件；`-` 表示从 stdin 读入 |
| `-o <file>` | 输出文件（`--build`/`--emit-c`/`--ast`/`--emit-sc` 模式下有效） |
| `-l <名>` | 追加链接库，可重复；紧凑写法 `-lm` 也支持（运行/构建模式） |
| `--adt <x>` | adt 自定义实现（`.c`/`.o`/`.a`，按 `builtins/adt/adt.h` 契约）；未指定时 `inc adt.sc` 自动链接内置默认实现 |
| `--build` | 构建产物：可执行 / `.a` / `.so`/`.dylib`（见 §3.5） |
| `--emit-c` | 转译为 C 源码 |
| `--ast` | 输出 AST JSON |
| `--emit-sc` | 再生规范化 sc 源码 |
| `--clang [lib]` | 用 libclang 解析 C 头 `inc` 的外部描述符（仅 `--ast`，见 §8.3）；`lib` 可省略，省略时自动检测平台默认 libclang；也可用 `SCC_CLANG` 指定 |
| `--from <path>` | stdin 输入时提供「虚拟源文件路径」，作为 `inc` 相对路径解析与外部描述符采集的基准目录（插件实时编辑场景用） |
| `--` | 其后所有参数透传给被执行的程序（仅运行模式） |
| `-h` / `--help` | 帮助 |

示例：

```sh
scc app.sc                       # 编译+执行
scc app.sc -- arg1 arg2          # 程序收到 arg1 arg2
scc app.sc -l curl -lm           # 追加链接 libcurl、libm
echo 'fnc main: i4
    return 0' | scc -            # stdin 模式
scc app.sc --build               # 构建可执行文件 ./app
scc app.sc --build -o myapp      # 指定产物名
scc util.sc --build -o libutil.a      # 静态库（同时生成 libutil.h）
scc util.sc --build -o libutil.dylib  # 动态库（macOS；Linux 用 .so）
scc app.sc --emit-c -o app.c     # 转译 C（有 @导出时同时生成 app.h）
scc app.sc --ast | python3 -m json.tool   # 查看 AST
scc app.sc --emit-sc             # 规范化格式输出
```

## 3. 运行与构建机制

### 3.1 单文件（stdin）

`scc -` 时 C 源码经管道直接送入 `cc -x c -`，不落盘中间 `.c` 文件，
编译+链接一步完成，产物为临时可执行文件（`/tmp/scc_run_XXXXXX`），运行后立即删除。

### 3.2 多模块项目（文件输入）

文件输入走"模块单元编译 + 链接"模型：

1. 从入口文件出发解析 `inc x.sc` 依赖，递归构建模块依赖图（检测循环依赖并报错）；
   同时把直接依赖的 `@` 导出声明合并进导入方 AST（标记 external，
   不参与代码生成），使跨模块语法糖（方法调用、声明即构造）生效。
2. 每个模块独立生成 `.c` 单元与接口 `.h`（`@` 导出符号），写入临时目录。
3. 逐单元编译为 `.o`（附加 `-I 临时目录` 与 `-I 源文件所在目录`，
   后者使 `inc "local.h"` 能找到与源码同目录的本地头）。
   单元图含 builtins 子项目 `x/x.sc` 且同目录存在 `x_impl.c`（自动编译，
   `-I` 自身目录与 builtins 根使 `"x.h"`/`"platform.h"` 可见）或预编译
   `x.a`（直接链接）时，实现自动参与链接；adt 可经 `--adt`/`SCC_ADT`/
   配置键 `adt` 替换实现（`.c` 自动编译，`.o`/`.a` 直接参与链接）；
   m 子项目在 Linux 等平台自动追加 `-lpthread`。
4. 统一链接为可执行文件并运行，结束后删除整个临时目录。

模块搜索路径：相对入口文件目录 → 仓库根 `builtins/` 目录（含子项目形态
`builtins/x/x.sc`）→ 环境变量 `SCC_BUILTINS` 指定目录 → 内嵌资源释放目录
（仅发行版变体，见 §10）。

### 3.3 退出码与信号

- 返回被执行程序的退出码。
- 程序被信号终止时返回 `128 + 信号值`，并输出提示。

### 3.4 调试符号与源码级调试

运行/构建模式所有编译/链接命令都带 `-g`，且生成的 C 代码中插入
`#line 行号 "源文件.sc"` 指令——调试信息（DWARF）直接映射回 `.sc` 源码，
lldb/gdb 的断点、单步、堆栈、源码窗口全部落在 sc 源文件上，而非中间 C 代码：

```sh
scc app.sc --build -o app    # macOS 上自动执行 dsymutil 生成 app.dSYM
lldb ./app
(lldb) breakpoint set -f app.sc -l 5     # 直接对 .sc 行打断点
(lldb) run
frame #0: app`main at app.sc:5:17
-> 5        var x: i4 = add(3, 4)        # 源码窗口显示 sc 源码
(lldb) bt                                 # 跨模块堆栈同样映射回各 .sc 文件
  * frame #0: app`add(a=3, b=4) at util.sc:2
    frame #1: app`main at app.sc:5
```

说明：

- `#line` 中记录源文件绝对路径，任意工作目录下调试器都能定位源码。
- macOS 调试信息保存在 `.o` 中（链接产物仅存 debug map），而临时 `.o`
  构建后即删除，故 `--build` 在链接后自动执行 `dsymutil` 打包 `.dSYM`。
- `--emit-c` 输出不插 `#line`（保持 C 代码可读，用行号注释 `/* line N */` 代替）。
- VS Code 图形化调试（断点/单步/变量面板）配置见 [debugging.md](debugging.md)。

### 3.5 构建产物模式（--build）

与运行模式共用同一套模块图编译机制，区别是：不执行、产物保留。

产物类型由 `-o` 输出文件名后缀决定：

| `-o` 后缀 | 产物 | 合成方式 |
|---|---|---|
| 其余（含无后缀） | 可执行文件 | `cc *.o -o out` + `ldflags`/`-L`/`-l` |
| `.a` | 静态库 | `ar rcs out.a *.o` |
| `.so` / `.dylib` | 动态库 | `cc -shared *.o -o out`，单元编译附加 `-fPIC` |

规则：

- `-o` 缺省为输入文件名去 `.sc` 后缀（`scc app.sc --build` → `./app`）；
  stdin 输入必须显式指定 `-o`。
- 构建库且入口模块存在 `@` 导出对象时，额外生成同名 `.h` 接口头文件
  （`libutil.a` → `libutil.h`），供 C 或其它 sc 项目引用。
- 工具链配置（§4）与运行模式完全一致：`cflags`/`-I` 作用于单元编译，
  `ldflags`/`-L`/`-l` 作用于可执行/动态库链接（静态库不链接，仅归档）。

示例：sc 库供 C 项目使用：

```sh
scc util.sc --build -o libutil.a    # 产出 libutil.a + libutil.h
cc capp.c -L. -lutil -o capp        # C 侧直接引用
```

## 4. 工具链配置

运行模式与构建模式（`--build`）生效；`--emit-c` 只产出 C 源码，不受任何配置影响。

### 4.1 配置来源与优先级

每个键独立按 **环境变量 > 当前目录 `.sc` 配置文件 > 缺省值** 取值：

| 环境变量 | `.sc` 键 | 含义 | 展开为 |
|---|---|---|---|
| `SCC_CC` / `CC` | `cc` | C 编译器（缺省 `gcc`） | — |
| `SCC_CFLAGS` | `cflags` | 额外编译选项（空格分隔） | 原样附加 |
| `SCC_LDFLAGS` | `ldflags` | 额外链接选项（空格分隔） | 原样附加 |
| `SCC_INC` | `inc` | 头文件搜索路径，`:` 分隔多路径（类似 PATH） | 逐项 `-I` |
| `SCC_LIB` | `lib` | 库搜索路径，`:` 分隔多路径 | 逐项 `-L` |
| `SCC_LIBS` | `libs` | 链接库名，空格或逗号分隔 | 逐项 `-l` |
| `SCC_ADT` | `adt` | adt 自定义实现（`.c`/`.o`/`.a`，`--adt` 优先） | 参与链接 |
| `SCC_BUILTINS` | — | 额外内置模块搜索目录 | — |

命令行 `-l <名>` / `-lm` 与 `libs` 合并，总是追加。

生效位置：`cflags` 与 `-I` 作用于每个模块单元的编译；`ldflags`、`-L`、`-l`
作用于最终链接。stdin 单步模式两类选项都附加在同一条命令上。

### 4.2 `.sc` 配置文件

当前目录下的 `.sc` 文件，每行 `key = value`，`#` 行注释，键值两侧空白忽略：

```
# 项目工具链配置
cc      = clang
cflags  = -O2 -Wall
ldflags = -framework Cocoa
inc     = vendor/inc:/opt/homebrew/include
lib     = vendor/lib:/opt/homebrew/lib
libs    = mylib, m
```

### 4.3 示例：链接外部库

```sh
# 三种等价方式链接 vendor 下的 libmylib.a 与系统 libm
scc t.sc                                              # 读取当前目录 .sc 配置
SCC_INC=vendor/inc SCC_LIB=vendor/lib SCC_LIBS="mylib m" scc t.sc
SCC_INC=vendor/inc SCC_LIB=vendor/lib scc t.sc -l mylib -lm
```

### 4.4 远程工具链构建（remote build）

把"生成 C → 编译 → 链接 → 运行/取回产物"整体放到一台远端主机上完成：本机
只做 sc → C 代码生成，**不需要本地装目标平台的工具链**。典型用途——在 macOS
上写代码，直接用一台 Linux 主机编译并运行/产出 Linux 可执行文件。

只要配置了 `build_host`（或环境变量 `SCC_BUILD_HOST`），run 模式与 `--build`
模式即自动改走远端；其余命令行用法不变。

| 环境变量 | `.sc` 键 | 含义 | 缺省 |
|---|---|---|---|
| `SCC_BUILD_HOST` | `build_host` | 远端主机名/IP（**设置即启用远程构建**） | 空（关闭） |
| `SCC_BUILD_USER` | `build_user` | 登录用户名 | 当前用户 |
| `SCC_BUILD_PORT` | `build_port` | SSH 端口 | 22 |
| `SCC_BUILD_DIR`  | `build_dir`  | 远端工作根目录 | `/tmp/scc-remote` |
| `SCC_BUILD_KEY`  | `build_key`  | 私钥路径（公钥认证） | ssh-agent / `~/.ssh/id_rsa` 等默认密钥 |
| `SCC_REMOTE_CC`  | `remote_cc`  | 远端编译器名 | `cc`（MSVC 风味默认 `cl`） |
| `SCC_SSH_BACKEND`| `ssh_backend`| SSH 实现：`libssh2` 或 `system` | 编译进 libssh2 时为 `libssh2`，否则 `system` |
| `SCC_REMOTE_OS`  | `remote_os`  | 远端操作系统：`windows`（cmd.exe shell）\| 空=POSIX | 空（目标三元组为 windows 族时自动判定） |
| `SCC_CC_STYLE`   | `cc_style`   | 编译器命令风格：`msvc`（`cl.exe` 的 `/I /c /Fo`）\| 空=gcc 风格 | 空（windows 或 `remote_cc=cl` 时自动 `msvc`） |
| `SCC_VCVARS`     | `vcvars`     | MSVC `vcvars64.bat` 路径（远端 `cl` 前 `call`，置 `INCLUDE`/`LIB`） | 空 |
| `SCC_RUN_INTERACTIVE` | `run_interactive` | 仅 Windows：run 模式经计划任务把 GUI 投递到当前登录用户的交互会话启动（见下） | 空（关闭） |
| `SCC_TARGET_SUFFIX` | `target_suffix` | `add` 预编译库的目标变体后缀 | 空时回退 `triple` |

**两种 SSH 后端**

- `libssh2`：内置实现（vendored libssh2 + mbedTLS），**无需系统装 ssh/scp**，
  自带 known_hosts TOFU 与公钥/ssh-agent 认证。默认后端。
- `system`：直接调用系统 `ssh`/`scp`。沿用本机 `~/.ssh/config`、known_hosts、
  ssh-agent；要求该主机已在 known_hosts 中（用了 `BatchMode`，不会交互式询问）。

**工作流程与上传位置**

1. 本机把内联自包含的 `main.c`、`builtins/` 目录、以及 `add` 引入的原生依赖
   打成 `bundle.tgz`；
2. 推到远端 `<build_dir>/scc-build-<pid>-<随机>/`（每次构建独立会话目录）；
3. 远端解包，用 `remote_cc` 重新编译链接为 `main.out`；
4. run 模式：远端运行并回传标准输出/退出码；`--build`：取回 `main.out` 到 `-o`
   指定路径并加可执行位；
5. 构建结束自动 `rm -rf` 该会话目录。

**依赖处理（已支持 `add`）**

- 多模块 `inc`：代码生成阶段已全部内联进单个 `main.c`，天然支持。
- `add foo.c`（原生源码）：连同其**同目录头文件**一起上传，**在远端重新编译**
  ——跨架构也正确（源码现场编译）。
- `add lib.a` / `.so` / `.o`（预编译产物）：按原样上传并参与链接。**仅当远端与
  本机架构一致时可用**；跨架构会由远端链接器报错（二进制是特定架构的物理限制，
  此时请改用源码形式的 `add`，或配 `target_suffix` 提供目标变体、或在远端自备同名库）。
- `libs`/`-l*`：远端用其原生库解析；配置里写的本机 `-L`/绝对 `.a` 路径会被忽略
  （远端无意义，会有提示）。

**`add` 预编译库的目标变体（`target_suffix`）**

预编译库是特定架构的二进制。为同一 `add lib.a` 同时备多个平台的产物时，可用
`target_suffix` 让 scc **优先匹配 `<名>.<suffix>.<ext>` 变体，找不到再回退同名文件**：

```sh
# 目录下同时备： libssh2.a（本机） 与 libssh2.aarch64-linux.a（目标）
# 源码里只写 `add libssh2.a`；设了 suffix 后远端/交叉构建自动选变体
SCC_TARGET_SUFFIX=aarch64-linux \
  scc --build -o app --target templates/targets/remote-linux.target app.sc
```

*取值约定（标准）*

`target_suffix` 是一段**自由文本**，scc 不解释其语义，只做**逐字符**的文件名匹配：把它原样
插在库文件最后一个扩展名之前，去找 `<名>.<suffix>.<ext>`。因此唯一硬性要求是——**它必须与
你给变体库起名时中间那段完全一致（区分大小写，变体须与原库同目录）**。

推荐遵循**目标三元组**约定（也是 `target_suffix` 留空时的默认回退值，与 `triple` 一致），
即 `<arch>-<os>[-<abi/libc>]`，常见取值：

| 目标平台 | 建议 `target_suffix` | 变体文件名示例 |
|---|---|---|
| ARM64 Linux（glibc） | `aarch64-linux-gnu` | `libfoo.aarch64-linux-gnu.a` |
| ARM64 Linux（musl） | `aarch64-linux-musl` | `libfoo.aarch64-linux-musl.a` |
| x86-64 Linux | `x86_64-linux-gnu` | `libfoo.x86_64-linux-gnu.a` |
| ARM64 macOS | `arm64-apple-darwin` | `libfoo.arm64-apple-darwin.a` |
| 32 位 ARM 裸机 | `arm-none-eabi` | `libfoo.arm-none-eabi.a` |

> 也可用更短的自定标签（如 `linux-arm64`、`m4`），只要**库文件名与 `target_suffix` 一致**即可；
> 但优先用三元组——这样设了 `triple` 而未设 `target_suffix` 时即自动命中，无需重复配置。

*匹配规则细节*

- 仅作用于预编译 `.a`/`.so`/`.dylib`/`.o`（源码 `add foo.c` 由现场/远端重编，无需变体，后缀对其无效）。
- 只插一层、只看最后一个扩展名：`libfoo.a` → `libfoo.<suffix>.a`（不处理 `libfoo.so.1` 这类多段后缀）。
- 不模糊匹配、不向上目录查找：变体必须与原库**同目录**且文件名严格相等。
- `target_suffix` 留空时回退为 `triple`；二者都空则纯按同名链接（即本机原生行为，不受影响）。
- 本机交叉编译与远端构建同享此机制。

**限制**

- `--build` 远程模式目前**仅支持可执行产物**（不支持 `.a`/`.so`/`.bin`/`.hex`）。
- 依赖远端已装好 `tar` 与 `remote_cc` 指定的编译器。

**Windows 远端（MSVC）**

远端为 Windows 时（`remote_os=windows` 或目标三元组为 windows 族）走 `cmd.exe` shell、
默认 `cl.exe`（`cc_style=msvc`）：需配 `vcvars` 指向 `vcvars64.bat`（`cl` 前 `call` 以置
`INCLUDE`/`LIB`）。用户模块（FFI）的手写头按「项目根相对路径」随包上传，令生成 C 里
`#include "templates/utils/wsi/wsi.h"` 及头内 `../../../builtins/…` 相对包含在远端一并解析。
`add` 的预编译库须为**目标 ABI**（MSVC 与 mingw 的 `.a` 不可混链）；`.a` 传入 `cl` 会有
`D9024`（无法识别 `.a` 源类型）告警，但 `link.exe` 照常接受归档，可忽略。

**交互会话运行（`run_interactive`，仅 Windows）**

OpenSSH 在 Windows 上启动的进程隶属**会话 0**（Session 0，服务/非交互会话），自 Vista 起
它与用户交互会话（物理控制台、每个 RDP 连接各一会话）彼此隔离——**会话 0 里创建的 GUI
窗口位于不可见的独立窗口站**，用户在控制台/RDP 桌面上看不到，且事件循环会一直阻塞。故默认
的远端 run 对控制台程序完好（回传 stdout/退出码），但对 GUI「不可见」。

置 `SCC_RUN_INTERACTIVE=1` 后，run 模式改用计划任务把产物投递到**当前登录用户的交互会话**
启动（`schtasks /create … /it` → `/run` → `/delete`），窗口即出现在其控制台/RDP 桌面。
代价（有意为之）：**发射即忘**——不回传程序 stdout/退出码；且产物运行中其 `.exe` 被占用，
远端会话目录不清理（保留待产物退出后手清）。

> **跨平台性**：会话 0 不可见是 **Windows 专有**问题。POSIX 远端（Linux/macOS）无此机制，
> 忽略本开关：Linux 图形程序经 SSH 需 `DISPLAY`/X11 转发（`ssh -X` 把窗口转到**本机**显示，
> 或设 `DISPLAY` 显示在远端 X 服务器）；macOS/Cocoa 无内建 GUI 转发。真正**跨平台一致**的
> 看图方案是 VNC/RDP 等远程桌面（直接观看远端屏幕）；「投递到交互会话」只是各 OS 各自的实现。

**示例**

```sh
# 目标档 templates/targets/remote-linux.target 写好 build_host/user/port/key
scc --target templates/targets/remote-linux.target app.sc            # 远端编译并运行
scc --build -o app-linux --target templates/targets/remote-linux.target app.sc  # 取回 Linux 产物

# 纯环境变量一次性远程构建
SCC_BUILD_HOST=build.lan SCC_BUILD_USER=ci SCC_REMOTE_CC=gcc scc app.sc

# Windows 远端（MSVC）：在其 RDP/控制台桌面直接弹出 GUI 窗口
SCC_BUILD_HOST=10.0.0.9 SCC_BUILD_USER=me SCC_BUILD_PORT=2222 \
  SCC_REMOTE_OS=windows SCC_CC_STYLE=msvc SCC_REMOTE_CC=cl \
  SCC_VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" \
  SCC_SSH_BACKEND=system SCC_TARGET_TRIPLE=x86_64-windows-msvc \
  SCC_LDFLAGS="-luser32 -lgdi32 -lshell32" SCC_RUN_INTERACTIVE=1 \
  scc templates/demo/ui_demo.sc
```

## 5. C 代码生成（codegen_c）

### 5.1 类型映射

| sc | C |
|---|---|
| `i1/i2/i4/i8` | `int8_t/int16_t/int32_t/int64_t` |
| `u1/u2/u4/u8` | `uint8_t/uint16_t/uint32_t/uint64_t` |
| `f4/f8` | `float/double` |
| `bool` | `uint8_t` |
| `char` | `char` |
| 无类型对象 | `char*` |
| 无类型指针 | `void*` |
| 省略返回类型 | `void` |
| `name&` / `name&&` | `T*` / `T**` |
| `name[x][y]` | `T name[x][y]` |
| `expr: type&`（右值裸形态） | `((type*)(expr))` |
| `nil` | `NULL` |

生成的 C 文件头部自动包含 `stdint.h`、`stddef.h`、`stdbool.h`、`stdarg.h`。

### 5.2 链接可见性（static 默认）

顶层未 `@` 导出的 `fnc`/`var`/`let` 生成为 C `static`（文件内可见）；
`@` 导出符号保持外部链接，供其它模块或 C 代码引用。

### 5.3 定义顺序无关

生成 C 时自动输出结构/联合前置声明与函数原型，sc 源码支持先使用后定义
（含递归 / 互递归函数）。

### 5.4 伪类（成员函数与函数指针字段）

- 成员函数在结构体定义内实现（签名字段 + 缩进函数体），生成带接收者的
  C 函数（名字修饰 `Obj_method`，首参 `Obj *_this`），函数体内 `this`
  映射为参数 `_this`；`o.m(...)` / `p->m(...)` 调用时自动注入接收者
  （`&o` / `p`）。`@fnc Obj::m` 仅声明形态生成 extern 原型（C 侧实现）。
- 普通函数指针字段 `cb: fnc: ...`（无函数体）展开为普通 C 函数指针，
  调用不注入接收者。
- 调用实参不足时默认补 0：指针/数组/函数指针补 `NULL`，按值聚合补
  `(T){0}`，标量补 `0`；覆盖普通函数、成员函数、函数指针、rpc/run。
- 字段默认值：为含默认值的类型生成 `static inline T T__default(void)` 初始化器，
  未指定字段零初始化。

### 5.5 伪形参函数（rpc）

`rpc name: ret, a: T, b: T` 展开为“三件套”：

- `struct name { ret _; T a; T b; };` —— 同名参数结构体，返回槽 `_` 为首成员
  （省略返回类型时无 `_`；无参且无返回时生成 `char _;` 占位）；
- `void name_rpc(struct name *_p);` —— 实际函数（本模块未导出时 `static`；
  仅声明形态为 extern，由外部 C 侧实现）；
- `static inline ret name(...)` —— 调用包装：装填结构体 → 执行 → 取返回槽，
  即 `sync work(args)` 当前线程驱动的落地。

实现要点：结构体仅用 struct tag（不 typedef），C 中 tag 与函数名分属不同命名
空间，故可同名。函数体内参数引用改写为 `_p->x`，`return e` 改写为
`_p->_ = e; return;`。`@rpc` 导出时头文件包含完整三件套。

rpc 是「流程」原语，**禁止裸 `rpc()` 直接调用**：codegen 在 emitExpr 的 Call
分支拦截「目标名在 rpcs 表且非驱动上下文」并报错。须经驱动选定执行形态——
`sync`（当前线程直接执行，经上面的调用包装，发出与旧裸调用字节一致的 C）、
`async`（事件循环）、`run`（独立线程/池）、队列 `<<`（投递）。`sync E` 由
`Expr::Sync` 承载，仅放行其内层 rpc 调用（`emitRpcCallOK` 标志）。

### 5.6 run 语句（多线程）

`run rpc调用[, &t]` 以 rpc 调用创建线程（目标必须是 rpc，需 `inc mt.sc`）：

```c
{   /* run work(a, b), &t */
    struct work _rp = {0};
    _rp.x = a; _rp.y = b;
    thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&t));
}
```

出参为空（无 `, &t`）时传 `NULL` → detach 自释放。`thread_run` 为线程原语
（op_impl 实现）：单次 `malloc(sizeof(thread) + psize + 实现私有区)` 的联合
实体，参数 memcpy 到 thread 紧随位置；joinable 由 `thread_join` 等待并整块
回收。程序含 run 语句时自动输出 `thread_run` 的 extern 原型。

### 5.7 头文件生成（@ 导出）

`--emit-c -o x.c` 且存在 `@` 导出对象时生成 `x.h`：

- `@def` → 类型定义；`@var`/`@let` → `extern` 声明；`@fnc` → 函数原型；
- `@inc` → 同步输出该 include；
- 自动生成 include guard（文件名大写、非字母数字转 `_`）。

运行模式下每个模块单元也按同一机制生成接口头，模块间通过头文件连接。

`inc x.sc` 依赖的 `#include` 生成规则：

- 带手写 C ABI 头的子项目模块（`<root>/<name>/<name>.sc` + 同目录 `<name>.h`，
  如 builtins 的 `adt`/`io`）→ `#include "<root>/<name>/<name>.h"`（含根目录名以
  明确归属，如 `"builtins/adt/adt.h"`），随 `-I <root的上级>` 可见，不生成内部
  `scm_<token>.h`；编译时同时加入 `-I <builtins>` 与 `-I <builtins的上级>`。
- 其余用户模块 → `#include "scm_<token>.h"`（`<token>` 为路径转义的合法标识符）。
  `--emit-c -o` 模式下，这些用户模块的接口头会写到输出 `.c` 同级目录，使其自包含。

> **手写头是可选覆盖，自动生成才是默认通路**：上面第一条只是为「带手写头」的模块开的
> 特例分支。删掉该手写 `<name>.h`，`inc <name>.sc` 立即回退到第二条——由 `@` 导出对象
> 自动生成 `scm_<token>.h`。手写头存在的唯一理由，是承载 sc 表达不了的纯 C 细节（宏族、
> 平台 `#if`、`union`/匿名布局等）；其接口形状必须与 scc 本会生成的一致。该自动生成机制
> 对**所有** `inc` 的 `.sc` 模块通用，并非 builtins 专属。

### 5.8 链表结构体（def T: ~）与 chain 偏移注入

- 解析期：`def T: ~ {}` 置 `Decl::linked`，并在字段表**末尾**追加两个
  `synthetic` 真实字段 `_prev`/`_next`（`T*`）——后续成员访问、零初始化、
  头文件导出均按普通字段处理，`--emit-sc` 跳过 synthetic 字段还原原貌。
  `~` 仅允许 `{}` 结构体；显式定义 `_prev`/`_next` 报错。
- 代码生成期：`chain::append/push` 方法调用糖处，编译器取实参静态类型 T
  （须为 `linked` 结构体一级指针，否则报错），自动追加尾参
  `offsetof(T, _prev)`。chain 以 `_off` 记录该偏移，其余无类型实参的
  操作（pop/last/revert/cut 等）经 `_off` 间接寻址 `_prev`/`_next`。
- `prev`/`next` 上下文关键字：成员访问位（`.`/`->`）且基址静态类型为
  `linked` 结构体时，语义推断与 C 生成统一映射为 `_prev`/`_next`
  （codegen_c `memberFieldName`）；普通结构体的同名字段不受影响。
  解析期链表结构体禁止显式定义 `prev`/`next`（与 `_prev`/`_next` 同列）。

### 5.9 print 与 stringify(...) 格式化关键字

- `print(fmt, ...)`：成员表/全局表/函数表均无 `print` 时按关键字处理，
  生成 `print(fmt, ...)` 调用并在单元头部输出 extern 原型；要求单元
  `inc io.sc`（拉入 `builtins/io/io_impl.c` 链接），否则编译报错。
  级别前缀解析、SC_LOG 过滤、时间戳格式化全部在运行时 `print` 内完成。
- `stringify(值[, 缓存, 大小])`：JSON 格式化关键字（空括号 `string()`
  仍走 T() 堆构造糖；同名定义遮蔽时按普通调用），要求 `inc adt.sc`（依赖内置
  `string`）与 `inc io.sc`（依赖选项类型 `stringify_t`）。可选选项块
  `stringify<key:val, ...>(...)`：parser 在 `parsePostfix` 中遇 `stringify` 后紧跟 `<`
  时解析键值对（值限整数字面量）挂到 `Expr::sofOpts`；codegen 据此构造
  `(stringify_t){ .compact = N }` 作为末参传入格式化器（当前仅 `compact` 键）。
  代码生成按实参静态类型（`exprVType` + 数组维度表 `varDims`）登记格式化请求
  `sofReqs`（key = 规范类型名 + `_p`×指针级 + `_a`+维长），按实参个数静态派发：
  1 参→`stringify_KEY(值, opt)` 返回 `string`；3 参→`stringify_KEY_buf(值, 缓存, 大小, opt)`
  在缓存内构建（截断保证 NUL 结尾）返回 `char *`；其他个数报错。
  函数体先写入暂存流，结束后回填支撑代码：格式化原语
  （`sc__sof_i64/u64/f64/bool/char/cstr/ptr/named_ptr/amp_*/str`）、缩进原语
  `sc__sof_nl(string*, stringify_t, int)`、按值字段闭包递归生成的聚合格式化器
  `sc__sof_T(string*, T*, stringify_t, int depth)`（输出 JSON 对象，键加双引号；
  `compact:1` 紧凑单行，否则按 `_depth` 逐层 2 空格缩进多行美化）、
  每请求包装 `static string stringify_KEY(..., stringify_t)`（聚合一级指针含 nil 检查后解引用）
  及按需的缓存变体 `static char *stringify_KEY_buf(...)`。结构体指针成员→`"类型名@0x地址"`，
  标量指针成员→`"&值"`。转 C 时支撑代码写入独立 `stringify.h`（含 include guard），
  生成的 `.c` 在类型定义之后 `#include`（编译单元 `<token>_stringify.h`；
  `--emit-c -o` 同级 `stringify.h`；输出到 stdout 时回退内联自包含）。
  多维数组、未知类型编译报错；枚举按 i64 处理。

## 6. 语义检查（semanticCheck）

在代码生成前做静态检查，当前覆盖：

- **指针安全**：非指针/数组操作数的解引用、下标报错；
  `nil` 只能赋给指针/数组；指针与标量互相赋值报错。
- **地址逃逸**：禁止返回局部变量地址、禁止将局部变量地址写入全局存储。
- **void 边界**：void 返回值不能作为表达式使用。
- **类型推断边界**：`var p: = nil` 这类无法推断的写法要求显式声明指针类型。
- **聚合循环检测**：结构/联合按值递归包含报错，提示改为指针字段 `&`。

## 7. 错误诊断

输出格式：`文件:行号: 错误: 消息`，附出错行源码展示与修复提示（如有）：

```
feature1.sc:31: 错误: 期望 ':'，得到 ''
  |          1
  提示: case 分支标签需以 ':' 结尾
```

词法阶段还强制布局规则：缩进必须为 4 空格的倍数、禁止 Tab、禁止跳级缩进。

## 8. AST JSON（--ast）

### 8.1 节点格式

节点格式：`{"k":kind, "n":name, "d":detail, "l":line, "c":[children]}`，外部描述符
相关扩展字段：

| 字段 | 位置 | 含义 |
|---|---|---|
| `"x":1` | 节点 | 该符号来自外部（其它 `.sc` 模块或 C 头），不参与本单元代码生成 |
| `"o":"..."` | 节点 | 来源（`.sc` 模块路径，或 C 头裸名如 `stdio.h`） |
| `"u":1/0` | 外部节点 | 该外部符号是否被本单元引用（`1` 已用 / `0` 仅导入未用） |
| `"t":N` | 外部 `inc` 节点 | 来源声明总数（`-1` = 未知，退化文本匹配无法枚举时） |
| `"e":[...]` | 根节点 | 外部符号表（合并自依赖模块导出 + C 头描述符） |
| `"w":[{"m","l"}]` | 根节点 | 使用分析警告（如「导入但未使用」），`m` 消息、`l` 行号 |

是 VSCode 两个插件的数据源：

- **sc-lang**（vscode-sc）：作用域感知自动完成、实时诊断、悬停、跳转定义、
  文档符号、格式化（基于 `--emit-sc`）。
- **sc-ast-view**（vscode-ast）：AST 树视图与树结构源码对照（按 `"e"`/`"u"`/`"t"`
  渲染「外部描述符」分组与「已用 N / 共 M」统计）。支持 `.sc` 与 `.ss`。

插件通过 `scc - --ast`（stdin）调用，按文档版本缓存。

### 8.2 外部描述符与使用分析

`inc` 依赖引入的对外符号统称**外部描述符**，采集后做使用分析，供插件展示与诊断：

- **`.sc` 模块**：§3.2 解析依赖图时把直接依赖的 `@` 导出声明合并进导入方 AST
  并标记 `external`，来源 `origin` 为解析到的模块绝对路径，声明总数已知
  （`externDeclared` = 导出符号数，`externAnalyzed` = true）。
- **C 头**（非 `.sc`，见 §8.3）：由 `gatherCHeaderDescriptors` 单独采集。
- **使用分析**（`analyzeExternalUsage`）：扫描本单元引用集合（`collectExternalRefs`），
  逐个外部描述符标记 `used`；当某来源「声明集合完整可知」（`externAnalyzed`）且
  「总数非 0」却「无任何引用」时，产出一条「导入但未使用」警告（进根节点 `"w"`）。
  这是 lenient（仅警告、不报错）策略，避免误伤条件包含等场景。

### 8.3 C 头解析（--clang 与 libclang）

sc 与 C 共生（§总览），`inc stdio.h` / `inc "my.h"` 这类 **C 头依赖**同样可被采集为
外部描述符，与 `.sc` 模块走同一套使用分析与插件展示。该采集**仅在 `--ast` 模式**
触发（编译/运行不受影响，避免每次都去解析系统头），有两条精度不同的路径：

**1. libclang 精确枚举（指定 `--clang`）**

- libclang 通过 `dlopen` 在**运行时按需加载**——编译期不依赖 clang-c，二进制不增加
  任何链接依赖；只内联声明用到的最小 ABI（`clang_parseTranslationUnit` 等十余个
  符号 + 几个 `CXCursorKind` 稳定枚举值）。
- 对每个 C 头单独构造 `#include <X>` 翻译单元，解析后遍历顶层游标，按类别映射为
  `Decl::Kind`：函数→`fnctype`（仅签名）、`typedef`→`alias`、`struct`/`union`/`enum`
  原样、变量→`var`、宏→`let`（当常量展示）。聚合头（递归包含大量子头，如
  `windows.h`）也能枚举到真实符号。
- 解析选项 `DetailedPreprocessingRecord`（取得宏）`| SkipFunctionBodies`（提速）。
- **预定义符号过滤**：编译器内建宏 / 命令行定义的 presumed 文件名为空或形如
  `<built-in>`，不属于任何真实头文件，统计会严重失真，故按来源位置剔除。
- **macOS 自动 sysroot**：未显式给出目标三元组/sysroot 时，自动 `xcrun --show-sdk-path`
  注入 `-isysroot`，否则 libclang 只能枚举到预定义宏而找不到系统头。

**2. 退化文本匹配（不带 `--clang`）**

无 libclang 时，在能定位到的头文件文本里查本单元引用集合中的标识符是否出现——
只能识别「已被引用」的符号，**无法枚举未使用符号**（此时 `externDeclared` 记为 `-1`，
插件不显示「共 M」）。无需任何外部依赖，是默认兜底行为。

**降噪**：总是合成 `used` 符号；`unused` 符号仅当该头声明总数 ≤ 阈值（64）时才逐个
灌入 AST，否则只在 `inc` 节点记录总数 `externDeclared`，供插件显示「已用 N / 共 M」，
避免 `windows.h` 这类聚合头产出数千节点撑爆树视图。

**libclang 路径解析优先级**（`--clang` 出现时）：

```
--clang <path>  >  SCC_CLANG 环境变量  >  detectLibclang() 平台默认位置自动检测
```

- `--clang` 后**省略路径**：自动检测平台默认 libclang（macOS：Xcode /
  CommandLineTools / Homebrew；Linux：常见库目录与 `llvm-*` 工具链；Windows：LLVM
  安装目录；以及交由动态链接器搜索的 soname），检测失败**报错退出**。
- `--clang <path>` 给出**显式路径**但无法 `dlopen` 加载 → **报错退出**。
- **完全不带 `--clang`** → 退化为文本匹配（不报错）。

**交叉编译 / 目标配置**：`--clang` 解析 C 头时复用与编译/链接相同的工具链配置
（§4，按 `环境变量 > .sc 配置` 取值），翻译为 libclang 参数，使其按**目标平台**解析头：

| 配置键（env / `.sc`） | 传给 libclang |
|---|---|
| `SCC_TARGET_TRIPLE` / `triple` | `-target <triple>` |
| `SCC_SYSROOT` / `sysroot` | `--sysroot=<path>` |
| `SCC_TARGET_FLAGS` / `target_flags` | 原样附加 |
| `SCC_INC` / `inc` | 逐项 `-I` |
| `SCC_CFLAGS` / `cflags` | 原样附加 |
| `SCC_FREESTANDING` / `freestanding` | `-ffreestanding`（值为 `1`/`true`/`yes`） |
| `SCC_CLANG_ARGS` | 原样附加，**最高优先**（直接透传任意 clang 参数） |

显式给出目标三元组/sysroot 时不再自动注入本机 macOS SDK，避免污染目标平台头解析。

**用法示例**：

```sh
scc app.sc --ast --clang                       # 自动检测平台默认 libclang，精确枚举 C 头
scc app.sc --ast --clang /opt/homebrew/opt/llvm/lib/libclang.dylib   # 指定动态库
SCC_CLANG=/usr/lib/llvm-18/lib/libclang.so.1 scc app.sc --ast --clang # 环境变量指定
scc app.sc --ast                               # 不带 --clang：退化文本匹配
# 交叉编译目标（按目标平台头解析）：
SCC_TARGET_TRIPLE=x86_64-linux-gnu SCC_SYSROOT=/path/to/sysroot \
  scc app.sc --ast --clang
# stdin（插件实时编辑场景）：--from 提供基准目录
echo '...' | scc - --ast --from /abs/path/app.sc --clang
```

> 注：`--clang` 采用类似 `-o` 的取值启发——仅当紧跟的下一个 token 不以 `-` 开头时
> 才当作 `lib` 路径吞掉；故 `scc app.sc --ast --clang` 安全（末尾裸 `--clang`），
> 而 `scc --clang app.sc --ast` 会把 `app.sc` 误当 lib。建议把裸 `--clang` 放在参数末尾。

## 9. 源码再生（--emit-sc）

从 AST 输出规范化缩进的 sc 源码，满足往返性质：再生源码可重新编译执行，
语义与原源码一致。用作 `Format Document` 的实现基础。

## 10. 构建、安装与测试

仓库根 `build.sh` 一键脚本：

```sh
./build.sh build      # 构建 scc（CMake Release，产物 compiler/build/scc）
./build.sh dist       # 构建发行版 scc（内嵌 builtins，产物 compiler/build-dist/scc）
./build.sh test       # 构建 + examples/feature*.sc 端到端验证（运行模式 + emit-c 模式 + 负向用例）
./build.sh install    # 安装 scc 到 $PREFIX/bin（默认 /usr/local/bin）+ VSCode 插件软链
./build.sh uninstall  # 卸载
./build.sh clean      # 清理构建产物
```

发行版变体（`-DSCC_EMBED_BUILTINS=ON`）：构建时把 `builtins/` 下全部
`.sc`/`.h` 资源与预编译的 `adt.a`（`adt_impl.c` 提前编为静态库）内嵌进
scc 二进制；运行时首次使用释放到 `~/.cache/scc/builtins-<内容哈希>`
（已存在则复用，内容变化自动换目录），作为优先级最低的模块搜索路径；
adt 链接阶段发现释放目录无 `adt_impl.c` 时回退到 `adt.a` 直接参与链接。
效果：scc 单二进制即可发行，无需携带 `builtins/` 目录；源码仓库内
开发时仓库 `builtins/` 优先生效，行为不变。`--adt` 自定义实现仍可用
（释放目录含 `adt.h` 供自定义 `.c` 编译）。注：builtins 增删文件后需
重新 cmake configure（资源清单在 configure 时收集）。

手动构建：

```sh
cd compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

注意：项目目录移动后需 `rm -rf compiler/build` 重新配置（CMakeCache 记录绝对路径）。

## 11. 源码地图

| 文件 | 职责 |
|---|---|
| `compiler/src/lexer.cpp` | 词法分析：缩进 INDENT/DEDENT、关键字、字面量（含十六进制、后缀）、最长匹配运算符 |
| `compiler/src/parser.cpp` | 递归下降语法分析：声明 / 语句 / 表达式（含强转、初始化列表） |
| `compiler/src/ast.h` | AST 节点定义：`Program` → `Decl` → `Stmt` → `Expr`，`TypeRef` 统一类型表示 |
| `compiler/src/semantic.cpp` | 语义检查 |
| `compiler/src/codegen_c.cpp` | C 后端（含头文件生成 `emitCHeader`） |
| `compiler/src/codegen_sc.cpp` | sc 源码再生后端 |
| `compiler/src/ast_json.cpp` | AST JSON 后端 |
| `compiler/src/cheaders.cpp` | C 头外部描述符采集（dlopen libclang 枚举 / 退化文本匹配，见 §8.3） |
| `compiler/src/ast_print.cpp` | 表达式/类型/字段文本序列化（sc 后端与 JSON 后端共用） |
| `compiler/src/main.cpp` | 入口：参数解析、配置加载、模块图、编译+执行、错误格式化 |

## 12. 路线

- 一期（当前）：sc → C 转译。
- 二期：基于同一 AST 增加 LLVM IR 后端，直接作为 LLVM 前端。
