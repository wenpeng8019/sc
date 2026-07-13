# scc 编译器参考手册

scc 是 sc 语言的编译器（C++17 实现，手写词法分析 + 递归下降语法分析，AST 与后端解耦）。
本手册覆盖编译器的能力、机制、配置与用法。语言本身的语法参考见 [syntax](syntax)，
内置模块参考见 [builtins/REFERENCE.md](builtins/REFERENCE.md)，语言机制的运行时原理与
C 落地见 [builtins/MECHANISM.md](builtins/MECHANISM.md)。

sc 与 C 是共生关系：scc 转译为 C 后复用整个 C 工具链（编译器、调试器、库、ABI），
同时把 C 的工作流收敛为单命令（直接运行、构建产物、头文件生成、模块依赖编译链接、
交叉编译、远程构建、通用 C/C++ 模块库构建）。

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
- `.ss` 着色器源走独立子管线（GLSL 发射 + 可选 SPIR-V/MSL 转译），见 §9。

### 1.2 运行模式

| 模式 | 选项 | 行为 |
|---|---|---|
| 运行（默认） | 无 | 转 C → 调系统 C 编译器 → 执行 → 清理临时产物，类似 python 直接运行脚本 |
| 构建产物 | `--build` | 编译链接为持久产物：可执行 / 静态库 / 动态库 / 裸机镜像（按 `-o` 后缀，§3.5） |
| 模块库构建 | 模块目录 + `--build` | 通用 C/C++ 模块库构建：编译目录 `src/` 下原生源为 `lib<名>.a` 或动态库（§7.6） |
| 转译 C | `--emit-c` | 输出 C 源码（`-o` 写文件，缺省 stdout）；有 `@` 导出对象且指定 `-o` 时额外生成同名 `.h` |
| AST 导出 | `--ast` | 输出 AST JSON 树（VSCode 插件的数据源，§12） |
| 源码再生 | `--emit-sc` | 从 AST 再生成规范化 sc 源码（格式化器的基础，§14） |
| 接口摘要 | `--api` | 输出模块 `@` 导出接口签名摘要（§13.1） |
| 结构依赖图 | `--graph` | 程序结构依赖图 proggraph：JSON 或自包含 HTML 可视化（§13.2） |
| 单元测试 | `--test` | 编译目标文件的 `tst` 用例为测试 runner 并运行（§3.7） |

## 2. 命令行

```
scc <input.sc | 模块目录 | -> [选项] [-- 程序参数...]
```

| 参数 | 说明 |
|---|---|
| `input.sc` | 输入文件；`-` 表示从 stdin 读入；含 `src/` 子目录的**模块目录**配合 `--build` 走模块库构建（§7.6） |
| `-o <file>` | 输出文件（`--build`/`--emit-c`/`--ast`/`--emit-sc`/`--api` 有效）。**裸 `-o` 不带值**时按输入文件名 + 模式后缀推导，写入输入文件所在目录：`--emit-c`→`.c`、`--ast`→`.json`、`--emit-sc`→`.out.sc`、`--api`→`.api.sc`、`--build`→无后缀 |
| `-l <名>` | 追加链接库，可重复；紧凑写法 `-lm` 也支持，与配置的 `libs` 合并 |
| `-D<宏>[=值]` | 透传宏定义给 C 编译器（可重复；`-D FOO=1` 分写亦可）。**最高优先级**，追加在命令末尾，可覆盖配置与内置默认（如 `-DSC_PRINT_BUF=4096` 调大 print 单行缓冲） |
| `--cflags <opts>` | 透传任意 C 编译选项给 C 编译器（可重复，如 `--cflags -O3`） |
| `--adt <x>` | adt 自定义实现（`.c`/`.o`/`.a`，按 `builtins/adt/adt.h` 契约）；未指定时 `inc adt.sc` 自动链接内置默认实现 |
| `--target <档\|裸名>` | 加载交叉编译目标档（`key = value`，同 `.sc` 配置语法；`SCC_TARGET` 亦可，§5）。**裸名**（如 `android`）在 `.scenv/targets/<名>.target` 内解析（§4.4） |
| `--env <dir>` | 显式指定 `.scenv` 虚拟环境根（覆盖自动发现；`SCC_ENV` 亦可，§4.4） |
| `--builtins <dir>` | 目标适配 builtins 目录（**最高优先级**，替换默认库实现，§5.5/§7.1） |
| `--build` | 构建产物模式（§3.5）；输入为模块目录时走模块库构建（§7.6） |
| `--kind <exe\|shared\|static>` | 强制产物类型，覆盖「有无 `main` 自动判定」（§5.8 移动 app pkg/run 一条龙用；缺省 exe） |
| `--emit-c` | 转译为 C 源码（不受工具链配置影响） |
| `--ast` | 输出 AST JSON |
| `--emit-sc` | 再生规范化 sc 源码 |
| `--api` | 输出模块导出接口摘要（仅 `@` 导出定义项签名，形如 C 头） |
| `--graph[=unit]` | 程序结构依赖图；缺省整程序（递归全部 `inc`），`=unit` 仅当前单元 |
| `--check=ref` | 自动指针 `T@` 栈悬挂检查（§3.6） |
| `--check=mem` | 越界 canary 检查（§3.6） |
| `--check=ptr` | 运行时指针/下标守卫（§3.6） |
| `--test` | 单元测试模式（§3.7） |
| `--clang [lib]` | 用 libclang 解析 C 头 `inc` 的外部描述符（仅 `--ast`，§12.3）；`lib` 可省略，省略时自动检测平台默认 libclang；也可用 `SCC_CLANG` 指定 |
| `--from <path>` | stdin 输入时提供「虚拟源文件路径」，作为 `inc` 相对路径解析与外部描述符采集的基准目录（插件实时编辑场景用） |
| `--` | 其后所有参数透传给被执行的程序（仅运行模式） |
| `-h` / `--help` | 帮助 |

示例：

```sh
scc app.sc                       # 编译+执行
scc app.sc -- arg1 arg2          # 程序收到 arg1 arg2
scc app.sc -l curl -lm           # 追加链接 libcurl、libm
scc app.sc -DDEBUG=1 --cflags -O3   # 透传宏与编译选项
echo 'fnc main: i4
    return 0' | scc -            # stdin 模式
scc app.sc --build               # 构建可执行文件 ./app
scc app.sc --build -o myapp      # 指定产物名
scc util.sc --build -o libutil.a      # 静态库（同时生成 libutil.h）
scc util.sc --build -o libutil.dylib  # 动态库（macOS；Linux 用 .so）
scc builtins/gpu --build         # 模块库构建 → builtins/gpu/libgpu.a（§7.6）
scc app.sc --emit-c -o app.c     # 转译 C（有 @导出时同时生成 app.h）
scc app.sc --ast | python3 -m json.tool   # 查看 AST
scc app.sc --emit-sc             # 规范化格式输出
scc util.sc --api                # 导出接口摘要
scc app.sc --graph -o app.html   # 结构依赖图可视化
scc app.sc --check=ptr           # 带运行时指针守卫地运行
scc util.sc --test               # 运行 util.sc 的 tst 用例
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
2. 每个模块独立生成 `.c` 单元与接口 `.h`（`@` 导出符号），写入临时目录
   （`/tmp/scc_units_XXXXXX`）。
3. 逐单元编译为 `.o`（附加 `-I 临时目录` 与 `-I 源文件所在目录`，
   后者使 `inc "local.h"` 能找到与源码同目录的本地头）。模块实现的自动拾取
   （`x_impl.c` 拼接编译、`add` 源码动态编译、预编译 `x.a` 回退）与链接注入
   （模块 `.sc` 段配置、平台线程库等）见 §7。
4. 统一链接为可执行文件并运行，结束后删除整个临时目录。

模块搜索路径：`--builtins` 指定目录（最高优先级，整体替换）→ 相对入口文件目录 →
`.scenv/modules`（虚拟环境模块根，若发现 `.scenv`，§4.4）→
仓库根 `builtins/` 目录（含子项目形态 `builtins/x/x.sc`）→ 环境变量 `SCC_BUILTINS`
指定目录 → 内嵌资源释放目录（仅发行版变体，见 §7.7）。

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

与运行模式共用同一套模块图编译机制，区别是：不执行、产物保留（临时目录
`/tmp/scc_build_XXXXXX`）。

产物类型由 `-o` 输出文件名后缀决定：

| `-o` 后缀 | 产物 | 合成方式 |
|---|---|---|
| 其余（含无后缀 / `.elf`） | 可执行文件 | `cc *.o -o out` + `ldflags`/`-L`/`-l` |
| `.a` | 静态库 | `ar rcs out.a *.o` |
| `.so` / `.dylib` | 动态库 | `cc -shared *.o -o out`，单元编译附加 `-fPIC` |
| `.bin` | 裸机原始镜像 | 先链接临时 `.elf`，再 `objcopy -O binary`（交叉裸机场景，§5） |
| `.hex` | Intel-HEX 镜像 | 先链接临时 `.elf`，再 `objcopy -O ihex` |

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

### 3.6 运行时检查（--check=*）

三档按需开启的运行时安全检查（默认全部关闭；堆 ARC 自动回收始终生效，
机制原理见 [builtins/MECHANISM.md](builtins/MECHANISM.md) §1）：

| 选项 | 等价环境变量 | 检查内容 |
|---|---|---|
| `--check=ref` | `SCC_REF_CHECK=1` | 自动指针 `T@` 栈悬挂：注入栈对象引用头并在退域处断言悬挂（含源码定位） |
| `--check=mem` | `SCC_MEM_CHECK=1` | 越界 canary：ref 头堆对象注入头尾哨兵（地址派生魔数）、一维栈数组注入尾哨兵、托管目标注入 `-fstack-protector-strong` 保护返回地址；越界损坏报告并定位 |
| `--check=ptr` | `SCC_PTR_CHECK=1` | 运行时指针/下标守卫：解引用与指针下标处校验 nil、编译期已知维度的栈数组下标处校验越界；命中报告并 abort |

环境变量取非空且非 `0` 的值即开启，便于 CI 全量套用而不改命令行。

### 3.7 单元测试模式（--test）

编译目标文件中的 `tst` 用例为测试 runner 并运行：

- 逐用例**隔离执行**：assert 失败软中止本例，继续下一例；
- 汇总 通过/失败/跳过；
- 退出码为**失败用例数**（0 = 全过）。

```sh
scc util.sc --test
```

## 4. 工具链配置

运行模式与构建模式（`--build`）生效；`--emit-c` 只产出 C 源码，不受任何配置影响。

### 4.1 配置来源与优先级

每个键独立按 **环境变量 `SCC_*` > `--target` 目标档 > 当前目录 `.sc` 配置文件 >
`.scenv/env.sc`（虚拟环境默认层，§4.4）> 内置默认** 取值：

| 环境变量 | `.sc` 键 | 含义 | 展开为 |
|---|---|---|---|
| `SCC_CC` / `CC` | `cc` | C 编译器（缺省 `gcc`） | — |
| `SCC_CXX` / `CXX` | `cxx` | C++ 编译器（缺省 `g++`；模块库构建含 C++ 源时用，§7.6） | — |
| `SCC_CFLAGS` | `cflags` | 额外编译选项（空格分隔） | 原样附加 |
| `SCC_LDFLAGS` | `ldflags` | 额外链接选项（空格分隔） | 原样附加 |
| `SCC_INC` | `inc` | 头文件搜索路径，`:` 分隔多路径（类似 PATH） | 逐项 `-I` |
| `SCC_LIB` | `lib` | 库搜索路径，`:` 分隔多路径 | 逐项 `-L` |
| `SCC_LIBS` | `libs` | 链接库名，空格或逗号分隔 | 逐项 `-l` |
| `SCC_ADT` | `adt` | adt 自定义实现（`.c`/`.o`/`.a`，`--adt` 优先） | 参与链接 |
| `SCC_BUILTINS` | — | 额外内置模块搜索目录 | — |

交叉编译相关配置键（`ar`/`objcopy`/`triple`/`sysroot`/`target_flags`/`threads`/
`debug`/`freestanding`/`platforms`/`run`/`target_suffix`）见 §5.2；远程构建配置键
（`build_host` 等）见 §6。命令行 `-l <名>` / `-lm` 与 `libs` 合并，总是追加；
`-D`/`--cflags` 追加在最末（可覆盖前面所有来源）。

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

> **模块目录下的 `.sc` 是另一种形态**：除全局键外还支持 `[target]` INI 段
> （按目标注入编译/链接选项），见 §7.4。当前目录 `.sc` 作为工具链配置读取时
> 遇 `[` 段头即停，段内容不会被误读。

### 4.3 示例：链接外部库

```sh
# 三种等价方式链接 vendor 下的 libmylib.a 与系统 libm
scc t.sc                                              # 读取当前目录 .sc 配置
SCC_INC=vendor/inc SCC_LIB=vendor/lib SCC_LIBS="mylib m" scc t.sc
SCC_INC=vendor/inc SCC_LIB=vendor/lib scc t.sc -l mylib -lm
```

### 4.4 `.scenv` 虚拟环境（域基）

类比 Python venv：目录里放一个 `.scenv/` 子目录，即把该目录树声明为一个 **sc 域基**。
scc 从**输入源文件所在目录**（其次 cwd）逐级向上寻找最近的 `.scenv/`（同 `.git`/`.venv`
的发现方式），命中即令其下的约定子目录自动生效——无需 `source activate`。

| 约定路径 | 作用 | 类比 |
|---|---|---|
| `.scenv/modules/` | 额外模块搜索根（`inc x.sc` 裸名在此找 `modules/x.sc` 或子项目 `modules/x/x.sc`） | site-packages |
| `.scenv/targets/` | `--target` 裸名地址基（`--target android` → `targets/android.target`） | — |
| `.scenv/lib/` | 存在则自动追加 `-L`（链接库搜索路径） | — |
| `.scenv/env.sc` | 环境级 `.sc` 配置（工具链键默认层，见下方优先级） | — |
| `.scenv/cache/` | scc 中间/临时产物落此（`scc_units`/`scc_build`/`scc_run` 等），免污染 `/tmp` | `__pycache__` |

**发现与覆盖**：`--env <dir>` > 环境变量 `SCC_ENV` > 源文件目录向上 > cwd 向上。
显式指定的目录直接作为 `.scenv` 根；自动发现找的是名为 `.scenv` 的目录。未命中 →
零影响（完全退回无环境的旧行为）。

**配置优先级**（§4.1 的完整链）：
环境变量 `SCC_*` > `--target` 目标档 > 当前目录 `./.sc` > `.scenv/env.sc` > 内置默认。
即 `.scenv/env.sc` 是「环境默认层」，可被项目级 `./.sc` 或命令行/环境变量覆盖。

**`--target` 裸名解析**：值不含路径分隔符（`/`）且不以 `.target` 结尾时视为裸名，在
`.scenv/targets/<名>.target` 查找，命中即展开为该绝对路径；含路径或 `.target` 后缀者
视为显式路径原样使用（旧行为不变）。无环境或未命中亦原样返回。

```sh
# templates/ 下建有 .scenv/（modules/ = wsi/ui/… 组件；targets/ = android/ios-sim/…）
cd templates/app/hello-android
scc app.sc --target android      # 向上发现 templates/.scenv：
                                 #   inc wsi.sc → .scenv/modules/wsi/wsi.sc
                                 #   --target android → .scenv/targets/android.target
                                 #   中间产物 → .scenv/cache/

scc app.sc --env /path/to/.scenv --target android   # 显式指定环境根
```

> `--emit-c` 纯转译不受工具链配置影响，但 `.scenv` 的模块搜索（`inc`）与 `--target`
> 裸名解析仍生效（决定交叉生成哪套目标的 C）。


## 5. 交叉编译

scc 转译为 C 后调用 C 工具链，因此交叉编译的本质就是**把 scc 调用的工具链整套换成
目标工具链**：目标 `gcc`/`ar`/`objcopy`、目标机器选项、目标 sysroot，以及（裸机时）
目标适配的 builtins 库。本机构建（不指定目标）行为完全不变；只有在给出目标三元组/
工具时才进入交叉路径。

### 5.1 一分钟上手（目标档 --target）

把所有目标设置写进一个**目标档**（`.target` 文件，语法同 `.sc` 配置：每行
`key = value`），用 `--target` 加载：

```sh
# 64 位 ARM Linux（托管 OS）
scc app.sc --build -o app --target templates/.scenv/targets/aarch64-linux.target

# Cortex-M4 裸机，产出可烧录镜像
scc fw.sc --build -o fw.bin --target templates/.scenv/targets/cortex-m4.target \
          --builtins path/to/m4-builtins
```

> 若从 `templates/` 域基内运行，可用**裸名**替代全路径：`--target aarch64-linux`、
> `--target cortex-m4`（经 `.scenv/targets/<名>.target` 解析，见 §4.4）。

示例目标档见 [templates/.scenv/targets/aarch64-linux.target](templates/.scenv/targets/aarch64-linux.target)
与 [templates/.scenv/targets/cortex-m4.target](templates/.scenv/targets/cortex-m4.target)。

### 5.2 配置项

每一项都可三种方式给出，优先级同 §4.1（环境变量 > 目标档 > `.sc` 配置 > 内置默认）：

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
| `target_suffix` | `SCC_TARGET_SUFFIX` | `add` 预编译库优先匹配 `<名>.<suffix>.<ext>` 变体（空=回退 `triple`，§7.3）；也是模块 `.sc` 段匹配键（§7.4） |
| `threads` | `SCC_THREADS` | 线程库链接选项（`-lpthread` / `none`），显式覆盖平台表 |
| `debug` | `SCC_DEBUG` | 链接后调试打包（`dsymutil` / `none`），显式覆盖平台表 |
| `freestanding` | `SCC_FREESTANDING` | `1` 表示裸机目标（无托管运行时） |
| `platforms` | `SCC_PLATFORMS` | 外置平台表文件（见 §5.3） |
| `pkg` | `SCC_PKG` | 打包器命令/脚本（构建后打成可部署包，见 §5.8；空=不打包） |
| `run` | `SCC_RUN` | 运行包装器/模拟器/部署器（run 模式用，见 §5.4 与 §5.8） |

> 目标档里 `cc`/`ar`/`objcopy`/`pkg`/`run` 若写的是**相对路径且该文件存在于目标档
> 所在目录**，scc 会自动解析为绝对路径（相对目标档，而非相对当前工作目录）。因此
> 目标档可与它引用的工具链包装脚本（`cc-android.sh`、`android-pkg.sh` 等）同放
> `templates/.scenv/targets/`，从任意目录 `--target` 都能正确定位。以 `/` 开头的绝对路径
> 或 `PATH` 中的裸命令名不受影响。

> 为什么 `target_flags`/`sysroot` 要同时进入链接？因为交叉 `gcc` 在**链接**时也据
> 机器选项与 sysroot 选择 multilib、`libgcc`、`crt0` 等启动/运行时组件；只在编译
> 阶段给会导致链接错配。

### 5.3 平台表：自动推导线程库与调试打包

不同目标对"线程怎么链""调试信息怎么打包"有固定差异。scc 内置一张**平台表**，
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
- 不写 `triple` 即本机构建，使用本机平台族的行为。

**外置平台表**（可选）：需要扩展/覆盖内置表时，提供一个平台表文件，用 `platforms`
指向它。每行 `pattern : threads : debug`（`pattern` 为三元组子串，命中即覆盖内置表；
`threads` 写 `none` 或留空表示无需线程库）：

```
# pattern : threads : debug
linux-musl   : -lpthread : none
myrtos       :           : none
```

### 5.4 运行交叉产物（run 模式）

默认 run 模式会 `fork+exec` 直接执行产物——但交叉产物**本机跑不了**。两种处理：

- 配置 `run = <模拟器>`（如 `run = qemu-aarch64 -L /opt/sysroots/aarch64`），
  scc 在 run 模式下用它包装执行产物。
- 不配置且目标与本机不同族时，run 模式直接**报错**，提示改用 `--build` 生成产物。

```sh
# 经 qemu 在本机跑 aarch64 产物
scc app.sc --target templates/.scenv/targets/aarch64-linux.target -- arg1 arg2
```

### 5.5 裸机/freestanding 与 --builtins

裸机目标无托管 libc：`print`、文件、时间等内置库的默认实现（依赖 `stdio`/`malloc`/
`pthread`）无法直接用。做法是**另建一套目标适配的 builtins 目录**，里面 `.sc`/`.h`
接口契约不变，只把 `*_impl.c`（与按需的 `platform.h`）替换为面向目标的实现（如把
`print` 写到 UART），再用 `--builtins <dir>` 以最高优先级指向它：

```sh
scc fw.sc --build -o fw.bin \
    --target templates/.scenv/targets/cortex-m4.target \
    --builtins boards/m4/builtins
```

`--builtins` 同时影响 `inc` 模块解析、自动链接的实现拾取、以及 `.ss` 能力档案
（caps，§7.8）的搜索，因此交叉时会自动用目标适配实现，而非默认主机实现。
启动文件与链接脚本经 `ldflags` 传入（见 cortex-m4 示例档）。

### 5.6 速查

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

### 5.7 另一条路：远程工具链构建

交叉编译要在本机备齐目标平台的 `cc`/sysroot/`ar`/`objcopy`。若手头**正好有一台
目标平台的主机**（比如要 Linux 产物且有 Linux 机器），可以不装交叉工具链，直接把
编译/链接放到那台主机上做——见 §6。要点对比：

| | 交叉编译 | 远程构建 |
| --- | --- | --- |
| 本机需求 | 目标 `cc` + sysroot | 仅 sc→C 代码生成 |
| 适用 | 任意目标（含裸机） | 有可 SSH 登录的目标平台主机 |
| `add` 原生源码 | 本机交叉编译 | 远端现场重编（跨架构也对） |
| `add` 预编译库 | 需目标架构版本 | 同架构可用，跨架构需远端自备 |
| 产物 | 全部类型 | 目前仅可执行 |

### 5.8 打包与部署运行（pkg / run，移动 app 一条龙）

桌面产物 `fork+exec` 即可跑（§5.4）；但**移动 app 多了两步**：产物不是可直接执行的
宿主程序，须先**打包**（Android→APK、iOS→`.app` bundle），再经**部署器**（`adb`、
`simctl`）装到设备/模拟器上由框架拉起。于是把「移动 app 构建」看成一种交叉构建：
目标档除工具链外，再配 `pkg`（打包器）与 `run`（部署运行器），scc 自动串起
**构建 → 打包 → 部署启动**一条龙：

```sh
cd templates/app/hello-android   # 域基 templates/.scenv 自动发现
scc app.sc --target android    # 建 .so → 打 APK → adb 装启（裸名 → .scenv/targets/android.target）
scc app.sc --target ios-sim    # 建 exe → 拼 .app → simctl 启（裸名 → .scenv/targets/ios-sim.target）
```

**输出类型自动判定 / `--kind` 覆盖**：scc 按源码是否含 `main` 决定产物类型——
有 `main`→可执行文件（iOS：Mach-O 打进 `.app`）；无 `main`→共享库
（Android：`ANativeActivity_onCreate` 导出进 `libapp.so`，由 `NativeActivity` 加载）。
可用 `--kind exe|shared|static` 强制指定。产物落在 `<app 目录>/build/` 下，基名取
`-o` 的名字，缺省取源文件名（如 `app.sc`→`libapp.so`/`app`）。

**触发条件**：目标档配了 `pkg`，或产物类型非可执行（如共享库），即进入 pkg/run
一条龙；否则退化为 §5.4 的 host/模拟器直跑。

**调用约定与环境变量契约**：scc 先构建产物，再依次调用（缺省任一为空则跳过该步）：

- 打包：`<pkg 命令> <产物路径>`
- 部署运行：`<run 命令> <产物路径> [-- 透传给 app 的参数…]`

构建产物的路径/名称、目标信息经**环境变量**传给 pkg/run 脚本（不靠命令行位置约定，
跨平台一致，Windows 亦可用）：

| 环境变量 | 含义 |
| --- | --- |
| `SCC_ARTIFACT` | 构建产物绝对路径（同时作为 pkg/run 的位置参数 `$1`） |
| `SCC_APP_DIR` | app 源目录（含 `AndroidManifest.xml`/`Info.plist` 等） |
| `SCC_APP_NAME` | app 基名（产物名去前缀去扩展，如 `app`） |
| `SCC_BUILD_DIR` | 构建输出目录（`<app 目录>/build`），打包中间物落此 |
| `SCC_TARGET_DIR` | 目标档所在目录（脚本据此定位同目录的 dex/垫片等资源） |
| `SCC_TARGET_TRIPLE` | 目标三元组 |
| `SCC_TARGET_SUFFIX` | 目标库变体后缀（§7.3） |

**职责边界**：pkg/run 脚本只消费产物与 wsi 等模块**预编交付**的资源（Android 的
`wsi-android.dex` 由 wsi 自身 `build.sh` 产出并入库，app 不重编 wsi 的 Java 垫片）。
app 侧 `build.sh` 因此可薄成一条 `scc … --target …`（外加为该目标重编 wsi 变体库）。

示例目标档与脚本见 [templates/.scenv/targets/](templates/.scenv/targets/)（`android.target`、
`ios-sim.target` 及配套 `*-pkg.sh`/`*-run.sh`、NDK/xcrun 工具链包装）。

## 6. 远程工具链构建（remote build）

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

**两种 SSH 后端**

- `libssh2`：内置实现（vendored libssh2 + mbedTLS），**无需系统装 ssh/scp**，
  自带 known_hosts TOFU 与公钥/ssh-agent 认证。默认后端。
- `system`：直接调用系统 `ssh`/`scp`。沿用本机 `~/.ssh/config`、known_hosts、
  ssh-agent；要求该主机已在 known_hosts 中（用了 `BatchMode`，不会交互式询问）。

**工作流程与上传位置**

1. 本机生成各单元 `.c`/`.h`（暂存 `/tmp/scc_runits_XXXXXX`），连同 `add` 引入的
   原生依赖打成 `bundle.tgz`（暂存 `/tmp/scc_stage_XXXXXX`）；
2. 推到远端 `<build_dir>/scc-build-<pid>-<随机>/`（每次构建独立会话目录）；
3. 远端解包，用 `remote_cc` 重新编译链接为 `main.out`；
4. run 模式：远端运行并回传标准输出/退出码；`--build`：取回 `main.out` 到 `-o`
   指定路径并加可执行位；
5. 构建结束自动 `rm -rf` 该会话目录。

**builtins 的远端缓存**

POSIX 远端不再把 `builtins/` 整目录塞进每次的 bundle，而是按**内容哈希**缓存：

- 远端缓存位置 `$HOME/.sc/cache/builtins-<hash>/`（`.ok` 标记保证幂等）；
- 本机先 `test` 远端缓存命中与否，未命中才上传 builtins 压缩包
  （发行版 scc 直接传内嵌的 tgz blob，开发版现场打包）并解压、`touch .ok`；
- 会话目录内 `ln -sfn` 软链 `builtins` 指向缓存，编译命令照常 `-I builtins`。

内容哈希（FNV-1a，覆盖 `.sc`/`.h`/`.c`/`.m`/`.caps`/`.a` 与模块隐藏 `.sc` 配置的
排序相对路径+内容）保证 builtins 变更后自动换缓存目录。Windows 远端保持整目录
入包的旧路径。

**依赖处理（`add`）**

- 多模块 `inc`：各单元 `.c` 随包上传，远端逐单元编译链接，天然支持。
- `add foo.c`（原生源码）：连同其**同目录头文件**一起上传，**在远端重新编译**
  ——跨架构也正确（源码现场编译）；`.m` 源在 darwin 远端按 ObjC 编译，其余按 C。
- `add lib.a` / `.so` / `.o`（预编译产物）：按原样上传并参与链接。**仅当远端与
  本机架构一致时可用**；跨架构会由远端链接器报错（二进制是特定架构的物理限制，
  此时请改用源码形式的 `add`，或配 `target_suffix` 提供目标变体（§7.3）、或在
  远端自备同名库）。
- `libs`/`-l*`：远端用其原生库解析；配置里写的本机 `-L`/绝对 `.a` 路径会被忽略
  （远端无意义，会有提示）。

**限制**

- `--build` 远程模式目前**仅支持可执行产物**（不支持 `.a`/`.so`/`.bin`/`.hex`）。
- 依赖远端已装好 `tar` 与 `remote_cc` 指定的编译器。

**Windows 远端（MSVC）**

远端为 Windows 时（`remote_os=windows` 或目标三元组为 windows 族）走 `cmd.exe` shell、
默认 `cl.exe`（`cc_style=msvc`）：需配 `vcvars` 指向 `vcvars64.bat`（`cl` 前 `call` 以置
`INCLUDE`/`LIB`）。用户模块（FFI）的手写头按「项目根相对路径」随包上传，令生成 C 里
`#include "templates/.scenv/modules/wsi/wsi.h"` 及头内 `../../../../builtins/…` 相对包含在远端一并解析。
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
# 目标档 templates/.scenv/targets/remote-linux.target 写好 build_host/user/port/key
scc --target templates/.scenv/targets/remote-linux.target app.sc            # 远端编译并运行
scc --build -o app-linux --target templates/.scenv/targets/remote-linux.target app.sc  # 取回 Linux 产物

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

## 7. builtins 集成与跨平台构建

本章是「语言内核」与「外部扩展」的分界线说明：

- **builtins** = 随编译器发行的模块集，分两层——**语言机制运行时**（`op`/`mem`/
  `adt`/`m`/`async` 等，语法糖与内存/线程/异步机制的落地，见
  [builtins/MECHANISM.md](builtins/MECHANISM.md)）与**标准库**（`io`/`ts`/`nn`/
  `gpu`/`gfx`/`spc` 等，见 [builtins/REFERENCE.md](builtins/REFERENCE.md)）。
- **外部扩展** = 任意用户模块（如 `templates/.scenv/modules/wsi`）。它们与 builtins 用
  **同一套机制**（`inc` 解析、`add` 原生依赖、模块 `.sc` 段配置、模块库构建），
  没有特权差异；builtins 唯一的特殊点是搜索路径兜底与随编译器发行（内嵌）。

### 7.1 模块搜索与子项目形态

`inc x.sc` 裸名解析顺序（首个命中生效）：

1. `--builtins <dir>`（最高优先级，**整体替换**默认实现——交叉/裸机适配用）；
2. 相对入口文件目录；
3. 仓库根 `builtins/`——先平铺 `x.sc`，再**子项目形态** `builtins/x/x.sc`
   （模块自带目录：`.sc` 契约 + 可选手写 `x.h` + 实现源）；
4. `SCC_BUILTINS` 环境变量指定目录；
5. 内嵌资源释放目录（仅发行版 scc，§7.7）。

### 7.2 实现拾取（链接自动化）

单元图含子项目模块 `x/x.sc` 时，其原生实现自动参与编译链接，按序拾取：

| 形态 | 拾取行为 |
|---|---|
| 同目录 `x_impl.c` | 自动编译（`-I` 自身目录与 builtins 根，使 `"x.h"`/`"platform.h"` 可见）。语言内核模块（op/mem/adt 等）的经典形态——实现与根单元同 TU 拼接 |
| 模块 `.sc` 内 `add src/*.c` | 逐文件**动态编译**（§7.3）——gpu/gfx/spc 等多源模块的现行形态，无需预编译静态库，交叉目标天然正确 |
| 预编译 `x.a` | 直接参与链接（回退路径；按 `target_suffix`/`triple` 优先匹配 `x.<suffix>.a` 变体） |

`adt` 可整体替换实现：`--adt <x>` > `SCC_ADT` > 配置键 `adt`（`.c` 自动编译，
`.o`/`.a` 直接参与链接，契约见 `builtins/adt/adt.h`）。

### 7.3 add 原生依赖

模块 `.sc` 里 `add <路径>` 引入原生源码或预编译产物：

**源码**（`.c` / `.m` / `.cpp` / `.cc` / `.cxx`）——现场编译，交叉/远程构建下
天然正确：

- `.m`（Objective-C）：darwin 目标附加 `-fobjc-arc -x objective-c`；非 darwin
  目标按 `-x c` 编译（源文件内部以平台宏自守卫空化）。apple 的 ObjC 与 C 工具链
  同源（xcode clang 同时发行两者），故 `.m` 后端可作为 builtins 默认集成；
- 编译时附加该源所在模块的 `.sc` 段配置 `cflags`（§7.4），后端宏等按目标注入。

**预编译**（`.a`/`.so`/`.dylib`/`.o`）——按原样参与链接，且支持**目标变体**：
设 `target_suffix`（留空回退 `triple`）后优先匹配 `<名>.<suffix>.<ext>`，找不到
再回退同名文件。源码里只写一行 `add libfoo.a`，本机/交叉/远程共用：

| 目标平台 | 建议 `target_suffix` | 变体文件名示例 |
|---|---|---|
| ARM64 Linux（glibc） | `aarch64-linux-gnu` | `libfoo.aarch64-linux-gnu.a` |
| ARM64 Linux（musl） | `aarch64-linux-musl` | `libfoo.aarch64-linux-musl.a` |
| x86-64 Linux | `x86_64-linux-gnu` | `libfoo.x86_64-linux-gnu.a` |
| ARM64 macOS | `arm64-apple-darwin` | `libfoo.arm64-apple-darwin.a` |
| 32 位 ARM 裸机 | `arm-none-eabi` | `libfoo.arm-none-eabi.a` |

匹配规则：`target_suffix` 是自由文本，scc 不解释语义只做逐字文件名匹配（区分
大小写，变体须与原库同目录）；只插一层、只看最后一个扩展名（不处理 `libfoo.so.1`
多段后缀）；也可用自定标签（如 `m4`），但优先用三元组——设了 `triple` 未设
`target_suffix` 时自动命中，无需重复配置。

### 7.4 模块 `.sc` 目标段配置（INI 段）

模块目录下的隐藏 `.sc` 配置文件，在全局键（§4.2）之外支持 **`[pattern]` INI 段**
——按目标注入编译/链接选项，是模块「按平台自描述」的机制（替代了以前编译器内
写死的注入表）：

```ini
# builtins/gpu/.sc
[darwin]
cflags  = -DSC_GPU_METAL -DSC_GPU_GL
ldflags = -framework Metal -framework QuartzCore -framework Cocoa -framework OpenGL

[*gles*]
cflags  = -DSC_GPU_GL -DSC_GPU_GLES
inc     = khr
libs    = EGL, GLESv2

[linux]
cflags  = -DSC_GPU_GL
libs    = GL, EGL, gbm
```

- **匹配键**（依次尝试，`fnmatch` 通配）：`target_suffix`（可含形态标签，如
  `aarch64-linux-gnu-gles` 命中 `[*gles*]`）→ `triple` → 平台族（host 构建用
  宿主族，如 `darwin`）。**首个命中的段独占生效**，后续段跳过。
- **段键**：`cflags` / `ldflags` / `inc` / `lib` / `libs`（语义同 §4.1；
  `inc`/`lib` 的相对路径**相对模块目录**展开）。
- **两个消费方**：① 程序 `inc <模块>.sc` 时——段 `cflags` 注入该模块 `add`
  源的编译，段 `ldflags`/`lib`/`libs` 注入最终链接（`-framework X` 两词元成对
  查重，不重复注入）；② 模块库构建（§7.6）编译/链接时同样套用。

### 7.5 隐式依赖与模块特判

编译器内保留的少量模块特判（写死量，均在 `compiler/src/main.cpp`）：

| 特判 | 内容 |
|---|---|
| `op` 自动导入 | 每个程序默认纳入 `op`（语言运行时：chain、异步内核等），无需用户 `inc` |
| 隐式依赖 | `adt`→`mem`；`op`→`mem`、`op`→`adt`（自动纳入单元图） |
| `m` | 链接平台线程库（平台表 §5.3） |
| `mem` | Linux 目标追加 `-lrt`（glibc < 2.34 的 `shm_open` 在 librt） |
| `async` | scc 以 `-DSCC_WITH_UV` 构建时链接 vendored libuv |
| `ssl` | 按 `SCC_SSL_BACKEND` 链 mbedTLS（vendored，首次构建缓存 `~/.cache/scc/mbedtls-<hash>/`）或 OpenSSL（`-lssl -lcrypto`，`SCC_OPENSSL_INC`/`SCC_OPENSSL_LIBDIR` 定位） |
| `op` 链接 | 平台线程库 + libuv（若启用）+ 平台框架（darwin CoreFoundation） |

其余平台条件注入（`-fstack-protector-strong`、`dsymutil` 等）见 §15.4–15.6。

### 7.6 通用模块库构建（scc <目录> --build）

输入为**含 `src/` 子目录的模块目录**时，scc 作为通用 C/C++ 模块构建工具——
把以前各模块 `build.sh` 的能力收敛进编译器：

```sh
scc builtins/gpu --build                       # → builtins/gpu/libgpu.a
scc builtins/gpu --build --target my.target    # → libgpu.<target_suffix|triple>.a
scc mymod --build -o libmymod.dylib            # 动态库（-fPIC -shared + 段 ldflags）
```

- **源集合**：`src/` 下 `.c`/`.m`/`.cpp`/`.cc`/`.cxx`（`.m` 的 ObjC/`-x c`
  处理同 §7.3）；
- **头搜索** `-I`：模块根（公开 `.h`）+ `src/` + builtins 根 + builtins 上级；
- **选项**：工具链配置（§4/§5）+ 模块 `.sc` 段配置（§7.4）；
- **C++ 编译器**：`SCC_CXX` > `CXX` > 配置键 `cxx` > `g++`；含 C++ 源的动态库
  用 C++ 驱动链接；
- **产物命名**：缺省 `lib<模块名>[.<suffix>].a`（`ar rcs`，交叉时自动带
  `target_suffix`/`triple` 后缀，即 §7.3 变体命名）；`-o *.so`/`*.dylib` 产
  动态库。中间产物在 `/tmp/scc_modlib_XXXXXX`。

> 注意：模块被 `inc` 时默认走 `add` 源码**动态编译**（§7.2），并不需要预编译库；
> 模块库构建用于向 C 项目交付、或预热 `add lib<名>.a` 回退路径的目标变体。

### 7.7 内嵌发行（dist）

发行版变体（CMake `-DSCC_EMBED_BUILTINS=ON`，`./build.sh dist`）让 scc
**单二进制发行**，无需携带 `builtins/` 目录：

- configure 时把 `builtins/` 全部资源（`.sc`/`.h`/`.c`/`.m`/`.caps`/模块隐藏
  `.sc` 配置）打成**单一 `builtins.tgz` blob**（约 485 KB）内嵌进 scc，并记录
  内容 MD5；
- 运行时首次使用释放（`tar xzf`）到 `~/.cache/scc/builtins-<内容哈希>/`
  （`.ok` 标记幂等；内容变化自动换目录），作为优先级最低的模块搜索路径；
- 全源码内嵌意味着 `add src/*.c` 动态编译、`--adt` 自定义、caps 档案在发行版
  同样可用；
- 可选 `-DSCC_EMBED_PREBUILT=ON`（默认 OFF）额外内嵌 host 平台预编译的子项目
  静态库（加速首次构建）；
- 源码仓库内开发时仓库 `builtins/` 优先生效，行为不变；builtins 增删文件后需
  重新 cmake configure（资源清单在 configure 时收集）。

### 7.8 能力档案（caps）

`builtins/gpu/caps/` 是标准设备能力档案库（`.ss` 着色器源里 `tar "名.caps"`
引用，§9）：搜索顺序为 **相对 `.ss` 源文件目录 → `builtins/gpu/caps/`**；
随 `--builtins` 目录整体替换，交叉/板级适配时可自带板卡档案。

### 7.9 预编交付产物与构建链（kernels/out）

`builtins/spc/kernels/*.ss` 经 `kernels/build.sh` 预编为 `out/*.shader.h/.c`
（多目标字节数组 + 反射，§9），**产物随 git 入库**，`spc.c` 编译期 include/
`add` 进用户程序——消费方无需具备 `.ss` 离线编译能力。因产物是 builtins 内的
`.h`/`.c`，它命中 §7.7 的内嵌资源清单，**随 dist 一并嵌入**，也随 `--builtins`
目录整体替换。

> 区别于 `templates/.scenv/modules/` 下 wsi/ui 的预编 `lib*.a`：那只是工作区
> 层面的 git 入库交付（改源码后重跑各自 `build.sh`），在 builtins 之外，
> 与 scc 资源内嵌无关。

**依赖链是线性的（非循环）**：

```
compiler/src（纯 C++，不依赖任何产物）
  → scc 二进制
  → kernels/build.sh 刷新 out/（git 入库 = 自举锚点）
  → 用户程序编译期经 spc.c 消费
```

构建 scc 本身不需要 `out/`——include 它的是 spc 运行时源码，只在用户程序
编译期被拉入，彼时产物已在仓库中。产物需要刷新的时机仅两个：改了
`kernels/*.ss`，或编译器着色 codegen 改动影响产物。为防**静默过期**，
根 `build.sh` 已把刷新挂进构建链：

- `./build.sh build`：构建 scc 后自动重跑 `kernels/build.sh`（用刚构建的 scc）；
- `./build.sh dist`：先走完整 `build`（含内核刷新）再做内嵌构建——因为
  §7.7 的内嵌快照取磁盘现状，若本次改了 codegen/`.ss`，嵌入前必须先刷新，
  "两遍"顺序由脚本形式化；初始构建仍是一条命令（git 快照天然自举）。

## 8. C 代码生成（codegen_c）

### 8.1 类型映射

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

### 8.2 链接可见性（static 默认）

顶层未 `@` 导出的 `fnc`/`var`/`let` 生成为 C `static`（文件内可见）；
`@` 导出符号保持外部链接，供其它模块或 C 代码引用。

### 8.3 定义顺序无关

生成 C 时自动输出结构/联合前置声明与函数原型，sc 源码支持先使用后定义
（含递归 / 互递归函数）。

### 8.4 头文件生成（@ 导出）

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

### 8.5 语言机制的 C 落地（→ MECHANISM）

各语法机制如何生成 C（伪类成员函数、rpc 三件套、`run` 线程语句、链表 `~` 与
chain 偏移注入、`print`/`stringify` 格式化关键字）属机制规格，统一收录在
[builtins/MECHANISM.md](builtins/MECHANISM.md) **§13**；各机制的运行时原理见
其对应章节（内存安全 §1、自动指针 §2、链接 §3、线程 §6、异步 §7、类 §9 等）。

## 9. 着色器编译（.ss）

`.ss` 是 GPU 空间计算方言 syntax-s（sc 的严格子集，vert/frag/comp 三 stage），
语法手册见 [syntax-s.md](syntax-s.md)。scc 对 `.ss` 输入走独立子管线：

```sh
./compiler/build/scc file.ss -o outdir/stem
# → 每 stage 一份目标代码 + <stem>.reflect.json（多 tar 时产物带标签，
#    如 vs_main.glcore410.vert、vs_main.metal20000.metal）
```

- **目标声明** `tar`：`metal@2.0`（MSL，入口=stage 函数名；GLSL→glslang→SPIR-V→
  SPIRV-Cross→MSL 全在 scc 内离线完成）/ `glcore@410` / `gles@100·300`（ES 版本
  白名单 100/300/310/320）/ `vulkan@450`（GLSL，入口恒 `main`）/ `webgl@100·300`；
  `tar "名.caps"` 引用能力档案约束目标能力（搜索顺序见 §7.8）。
- **反射 JSON**：`resources[]{name,kind:uniform|storage|push|sampler,set,binding,size}`
  + `stages[]{name,stage,entry,inputs[]{name,type,location},local_size(comp)}`。
  绑定清单是 Vulkan set/binding 风格，各运行时后端按此对位。
- SPIR-V 由 `glslangValidator` 离线编译（Vulkan 后端消费）；转译组件按 CMake
  选项 `SCC_WITH_GLSLANG`/`SCC_WITH_SPIRV_CROSS` 编入。

## 10. 语义检查（semanticCheck）

在代码生成前做静态检查，当前覆盖：

- **指针安全**：非指针/数组操作数的解引用、下标报错；
  `nil` 只能赋给指针/数组；指针与标量互相赋值报错。
- **地址逃逸**：禁止返回局部变量地址、禁止将局部变量地址写入全局存储。
- **void 边界**：void 返回值不能作为表达式使用。
- **类型推断边界**：`var p: = nil` 这类无法推断的写法要求显式声明指针类型。
- **聚合循环检测**：结构/联合按值递归包含报错，提示改为指针字段 `&`。

## 11. 错误诊断

输出格式：`文件:行号: 错误: 消息`，附出错行源码展示与修复提示（如有）：

```
feature1.sc:31: 错误: 期望 ':'，得到 ''
  |          1
  提示: case 分支标签需以 ':' 结尾
```

词法阶段还强制布局规则：缩进必须为 4 空格的倍数、禁止 Tab、禁止跳级缩进。

## 12. AST JSON（--ast）

### 12.1 节点格式

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

### 12.2 外部描述符与使用分析

`inc` 依赖引入的对外符号统称**外部描述符**，采集后做使用分析，供插件展示与诊断：

- **`.sc` 模块**：§3.2 解析依赖图时把直接依赖的 `@` 导出声明合并进导入方 AST
  并标记 `external`，来源 `origin` 为解析到的模块绝对路径，声明总数已知
  （`externDeclared` = 导出符号数，`externAnalyzed` = true）。
- **C 头**（非 `.sc`，见 §12.3）：由 `gatherCHeaderDescriptors` 单独采集。
- **使用分析**（`analyzeExternalUsage`）：扫描本单元引用集合（`collectExternalRefs`），
  逐个外部描述符标记 `used`；当某来源「声明集合完整可知」（`externAnalyzed`）且
  「总数非 0」却「无任何引用」时，产出一条「导入但未使用」警告（进根节点 `"w"`）。
  这是 lenient（仅警告、不报错）策略，避免误伤条件包含等场景。

### 12.3 C 头解析（--clang 与 libclang）

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
（§4/§5，按 `环境变量 > .sc 配置` 取值），翻译为 libclang 参数，使其按**目标平台**解析头：

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

## 13. 接口摘要与程序结构图

### 13.1 --api：导出接口摘要

输出模块的 `@` 导出定义项签名摘要（形如 C 头的 sc 视角），供快速查阅模块对外
契约或生成文档：

```sh
scc util.sc --api            # 缺省 stdout
scc util.sc --api -o         # 裸 -o → util.api.sc
```

### 13.2 --graph：程序结构依赖图（proggraph）

Decl 级节点 + 调用/类型/读写/方法/构造/宏/token/模块边，从 `main`（可执行）或
`@` 导出（库）做**激活分析**（识别死代码/未达符号）：

```sh
scc app.sc --graph -o app.html   # 自包含 HTML 可视化（内嵌 viewer）
scc app.sc --graph               # JSON 到 stdout
scc app.sc --graph=unit          # 仅当前单元（外部引用建为叶子节点）
```

缺省整程序（递归解析全部 `inc` 依赖）；`-o *.html` 产自包含可视化页面，
其余/缺省输出 JSON。

## 14. 源码再生（--emit-sc）

从 AST 输出规范化缩进的 sc 源码，满足往返性质：再生源码可重新编译执行，
语义与原源码一致。用作 `Format Document` 的实现基础。

## 15. 编译器默认行为（隐式操作一览）

scc 在转译与构建过程中**自动注入**一批用户源码里看不见、但对正确运行必不可少的
操作。本章汇总这些「默认执行的操作」，便于理解生成产物、排查问题、判断某行为是否
符合预期。除特别标注外，**无需配置、自动生效**；多数在非相关平台上退化为空操作。

> 落点索引：代码生成见 [compiler/src/codegen_c.cpp](compiler/src/codegen_c.cpp)
> （`emitMainPrologue` / `emitMainEpilogue` 等）；链接/平台配置见
> [compiler/src/main.cpp](compiler/src/main.cpp)（`loadToolConfig` /
> `compileUnitsToObjects`）；平台宏见 [builtins/platform.h](builtins/platform.h)。

### 15.1 每个生成单元的头部

| 操作 | 说明 |
| --- | --- |
| `#include "platform.h"` | 每个生成的 `.c` 顶部统一引入，带来标准 C 头、跨平台宏（`P_WIN`/`P_LINUX`/TLS 等）、以及 Windows 下的 `<windows.h>`。 |
| 自动导入 `op.sc` | `op` 是默认导入的语言运行时模块（chain、异步内核等机制），自动纳入单元图，无需用户 `inc`（§7.5）。 |
| 结构/联合前置声明 | 为所有聚合类型默认输出前置声明，消除定义顺序依赖。 |
| 根模块导出接口头注入 | 非根单元在所有 `inc` 之后追加 `#include "scm_<root>.h"`，使集成单元的全局定义/操作可见。 |

### 15.2 `main` 入口序言（按执行顺序）

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

### 15.3 `main` 尾声（逆序析构）

`main` 返回前按与初始化相反的顺序清理（`emitMainEpilogue`）：

1. 全局析构 `fn(&var)`（**逆序**）；
2. `sc_mod_<t>_drop()` 各依赖模块销毁（**逆序**）；
3. 退出钩子 `__sc_gcanary_fini()` / `__sc_gfat_fini()`（退化路径，在所有用户析构之后）。

### 15.4 链接期自动注入的选项

根据**目标平台**与**用到的模块**，编译器自动向最终链接命令追加选项（用户无需手写）：

| 注入 | 触发条件 | 原因 |
| --- | --- | --- |
| `-lpthread` | Linux 目标 + 用到 `m`/`op`（线程/异步内核） | POSIX 线程库。 |
| `-lrt` | Linux 目标 + 用到 `mem`（跨进程共享内存） | glibc < 2.34 的 `shm_open`/`shm_unlink` 在 librt。详见 [troubleshooting.md](troubleshooting.md) §1。 |
| `-lssl -lcrypto` 或 `mbedtls.a` | 用到 `ssl` 模块 | TLS 后端：OpenSSL 链系统动态库；mbedTLS 静态烘进产物（构建 scc 时 CMake 固化选择）。 |
| libuv（`libuv.a` + 头/框架） | `-DSCC_WITH_UV` 构建 + 用到 `op`/`async` | 异步 I/O 的 libuv 后端。 |
| 模块 `.sc` 段 `ldflags`/`libs` | 单元图含该模块 + 目标命中段 | 模块自描述的平台链接需求（§7.4，如 gpu 的 `-framework Metal`）。 |
| `-fstack-protector-strong` | `--check=mem` 且非裸机 | C 编译器插入栈哨兵，捕获栈溢出破坏返回地址。 |

平台线程库来自平台表（§5.3）：Linux=`-lpthread`，macOS/Windows/裸机=空。
配置优先级：`SCC_*` 环境变量 > `--target` 目标档 > `.sc` 配置 > 内置默认。

### 15.5 目标平台定向与裸机

| 操作 | 条件 | 作用 |
| --- | --- | --- |
| `-D SC_TARGET_WIN` / `_DARWIN` / `_LINUX` | 显式指定交叉 `triple` 时 | 令 `platform.h` 的平台分支以「目标平台」为准，而非回退到 C 编译器默认目标。 |
| 自动 `freestanding` | 目标族为 `bare`（裸机 `*-none-eabi`/`*-elf`） | 关闭托管运行时相关注入（线程库、栈哨兵运行时等）。 |
| 未知三元组校验 | 交叉目标未被平台表覆盖且未声明 | 报错要求声明 `threads`/`debug` 或提供平台表（§5.3）。 |

### 15.6 链接后步骤

| 操作 | 条件 | 作用 |
| --- | --- | --- |
| `dsymutil` | macOS 目标（平台表 `debug=dsymutil`） | 链接后生成 `.dSYM` 调试符号包。 |

## 16. 写死路径与默认值清单

编译器源码内固定的路径、工具与阈值（排查问题、清缓存、审计行为时查这里）：

**临时目录**（`mkdtemp`/`mkstemp` 模板，用完即删）：

| 模板 | 用途 |
|---|---|
| `/tmp/scc_run_XXXXXX` | stdin 单步模式的临时可执行文件 |
| `/tmp/scc_units_XXXXXX` | 运行模式的模块单元 `.c`/`.h`/`.o`（产物 `run.out`） |
| `/tmp/scc_build_XXXXXX` | `--build` 的中间单元 |
| `/tmp/scc_img_XXXXXX` | 裸机镜像的临时 `.elf`（objcopy 前） |
| `/tmp/scc_modlib_XXXXXX` | 模块库构建（§7.6）的中间 `.o` |
| `/tmp/scc_runits_XXXXXX` | 远程构建的单元生成暂存（prepareRemoteUnits） |
| `/tmp/scc_stage_XXXXXX` | 远程构建的 bundle 打包暂存 |

**持久缓存**（内容哈希寻址，可安全整目录删除，下次自动重建）：

| 路径 | 用途 |
|---|---|
| `~/.cache/scc/builtins-<hash>/`（及 `.tgz`） | 发行版内嵌 builtins 的释放目录（§7.7） |
| `~/.cache/scc/mbedtls-<hash>/` | ssl 模块 vendored mbedTLS 的一次性构建产物 |
| 远端 `$HOME/.sc/cache/builtins-<hash>/` | 远程构建的 builtins 缓存（§6） |
| 远端 `/tmp/scc-remote/` | 远程构建默认工作根（`build_dir` 可改） |

**默认工具与数值**：

| 项 | 默认 | 覆盖方式 |
|---|---|---|
| C 编译器 | `gcc` | `SCC_CC`/`CC`/配置 `cc` |
| C++ 编译器 | `g++` | `SCC_CXX`/`CXX`/配置 `cxx` |
| 归档/转换器 | `ar` / `objcopy` | `SCC_AR`/`SCC_OBJCOPY` |
| 远端编译器 | `cc`（MSVC 风味 `cl`） | `SCC_REMOTE_CC` |
| C 头 unused 符号灌入阈值 | 64（超过只记总数，§12.3） | 无（写死） |
| print 单行缓冲 | 2048 字节（`op_impl.c` 的 `SC_PRINT_BUF`） | `-DSC_PRINT_BUF=N`（§2） |

**SCC_* 环境变量索引**（运行时读取；`SCC_WITH_*`/`SCC_EMBED_*` 是构建 scc 自身的
CMake 编译期选项，不在此列）：

| 分组 | 变量 |
|---|---|
| 工具链（§4） | `SCC_CC` `SCC_CXX` `SCC_CFLAGS` `SCC_LDFLAGS` `SCC_INC` `SCC_LIB` `SCC_LIBS` `SCC_ADT` `SCC_BUILTINS` |
| 交叉（§5） | `SCC_TARGET` `SCC_AR` `SCC_OBJCOPY` `SCC_TARGET_FLAGS` `SCC_SYSROOT` `SCC_TARGET_TRIPLE` `SCC_TARGET_SUFFIX` `SCC_THREADS` `SCC_DEBUG` `SCC_FREESTANDING` `SCC_PLATFORMS` `SCC_RUN` |
| 远程（§6） | `SCC_BUILD_HOST` `SCC_BUILD_USER` `SCC_BUILD_PORT` `SCC_BUILD_DIR` `SCC_BUILD_KEY` `SCC_REMOTE_CC` `SCC_REMOTE_OS` `SCC_CC_STYLE` `SCC_VCVARS` `SCC_SSH_BACKEND` `SCC_RUN_INTERACTIVE` |
| 检查（§3.6） | `SCC_REF_CHECK` `SCC_MEM_CHECK` `SCC_PTR_CHECK` |
| AST（§12.3） | `SCC_CLANG` `SCC_CLANG_ARGS` |
| ssl（§7.5） | `SCC_SSL_BACKEND` `SCC_OPENSSL_INC` `SCC_OPENSSL_LIBDIR` |

## 17. 构建、安装与测试

仓库根 `build.sh` 一键脚本：

```sh
./build.sh build      # 构建 scc（CMake Release，产物 compiler/build/scc）
./build.sh dist       # 构建发行版 scc（内嵌 builtins tgz，产物 compiler/build-dist/scc，§7.7）
./build.sh test       # 构建 + examples/feature*.sc 端到端验证（运行模式 + emit-c 模式 + 负向用例）
./build.sh install    # 安装 scc 到 $PREFIX/bin（默认 /usr/local/bin）+ VSCode 插件软链
./build.sh uninstall  # 卸载
./build.sh clean      # 清理构建产物
```

手动构建：

```sh
cd compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

CMake 选项：`SCC_EMBED_BUILTINS`（内嵌 builtins 源码 tgz，§7.7）、
`SCC_EMBED_PREBUILT`（额外内嵌 host 预编译库，默认 OFF）、`SCC_WITH_UV`（libuv
异步后端）、`SCC_WITH_LIBSSH`（内置 SSH）、`SCC_WITH_MBEDTLS`/`SCC_WITH_OPENSSL`
（TLS 后端）、`SCC_WITH_GLSLANG`/`SCC_WITH_SPIRV_CROSS`（着色器转译，§9）。

注意：项目目录移动后需 `rm -rf compiler/build` 重新配置（CMakeCache 记录绝对路径）。

## 18. 源码地图

| 文件 | 职责 |
|---|---|
| `compiler/src/lexer.cpp` | 词法分析：缩进 INDENT/DEDENT、关键字、字面量（含十六进制、后缀）、最长匹配运算符 |
| `compiler/src/parser.cpp` | 递归下降语法分析：声明 / 语句 / 表达式（含强转、初始化列表） |
| `compiler/src/ast.h` | AST 节点定义：`Program` → `Decl` → `Stmt` → `Expr`，`TypeRef` 统一类型表示 |
| `compiler/src/semantic.cpp` | 语义检查 |
| `compiler/src/codegen_c.cpp` | C 后端（含头文件生成 `emitCHeader`、main 序言/尾声注入） |
| `compiler/src/codegen_sc.cpp` | sc 源码再生后端 |
| `compiler/src/ast_json.cpp` | AST JSON 后端 |
| `compiler/src/ast_print.cpp` | 表达式/类型/字段文本序列化（sc 后端与 JSON 后端共用） |
| `compiler/src/cheaders.cpp` | C 头外部描述符采集（dlopen libclang 枚举 / 退化文本匹配，§12.3） |
| `compiler/src/proggraph.cpp`（+ `proggraph_viewer.inc`） | 程序结构依赖图与 HTML 可视化（§13.2） |
| `compiler/src/remote.cpp/.h` | 远程工具链构建：SSH 后端、bundle 打包、builtins 远端缓存（§6） |
| `compiler/src/codegen_glsl.cpp` | `.ss` 着色器 GLSL 发射（§9） |
| `compiler/src/shader_sema.cpp` | `.ss` 语义检查与能力表（caps profile）校验 |
| `compiler/src/shader_spv.cpp` / `shader_msl.cpp` | SPIR-V 编译（glslang）与 MSL 转译（SPIRV-Cross） |
| `compiler/src/main.cpp` | 入口：参数解析、配置加载（含模块 `.sc` 段）、模块图、编译+执行、模块库构建、错误格式化 |
| `compiler/cmake/embed_builtins.cmake` | 发行版 builtins tgz blob 生成（§7.7） |

## 19. 路线

- 一期（当前）：sc → C 转译。
- 二期：基于同一 AST 增加 LLVM IR 后端，直接作为 LLVM 前端。



