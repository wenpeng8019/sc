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
- `static inline ret name(...)` —— 调用包装：装填结构体 → 执行 → 取返回槽。

实现要点：结构体仅用 struct tag（不 typedef），C 中 tag 与函数名分属不同命名
空间，故可同名——调用点无需任何改写。函数体内参数引用改写为 `_p->x`，
`return e` 改写为 `_p->_ = e; return;`。`@rpc` 导出时头文件包含完整三件套。

### 5.6 run 语句（多线程）

`run rpc调用[, &t]` 以 rpc 调用创建线程（目标必须是 rpc，需 `inc m.sc`）：

```c
{   /* run work(a, b), &t */
    struct work _rp = {0};
    _rp.x = a; _rp.y = b;
    thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&t));
}
```

出参为空（无 `, &t`）时传 `NULL` → detach 自释放。`thread_run` 为线程原语
（m_impl 实现）：单次 `malloc(sizeof(thread) + psize + 实现私有区)` 的联合
实体，参数 memcpy 到 thread 紧随位置；joinable 由 `thread_join` 等待并整块
回收。程序含 run 语句时自动输出 `thread_run` 的 extern 原型。

### 5.7 头文件生成（@ 导出）

`--emit-c -o x.c` 且存在 `@` 导出对象时生成 `x.h`：

- `@def` → 类型定义；`@var`/`@let` → `extern` 声明；`@fnc` → 函数原型；
- `@inc` → 同步输出该 include；
- 自动生成 include guard（文件名大写、非字母数字转 `_`）。

运行模式下每个模块单元也按同一机制生成接口头，模块间通过头文件连接。

### 5.8 链表结构体（def T: ~）与 chain 偏移注入

- 解析期：`def T: ~ {}` 置 `Decl::linked`，并在字段表**末尾**追加两个
  `synthetic` 真实字段 `_prev`/`_next`（`T*`）——后续成员访问、零初始化、
  头文件导出均按普通字段处理，`--emit-sc` 跳过 synthetic 字段还原原貌。
  `~` 仅允许 `{}` 结构体；显式定义 `_prev`/`_next` 报错。
- 代码生成期：`chain::append/push` 方法调用糖处，编译器取实参静态类型 T
  （须为 `linked` 结构体一级指针，否则报错），自动追加尾参
  `offsetof(T, _prev)`。chain 以 `_off` 记录该偏移，其余无类型实参的
  操作（pop/last/revert/cut 等）经 `_off` 间接寻址 `_prev`/`_next`。

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

节点格式：`{"k":kind, "n":name, "d":detail, "l":line, "c":[children]}`，
扩展字段：`"x":1` 表示外部模块符号，`"o"` 为来源模块路径，根节点 `"e"` 为外部符号表。

是 VSCode 两个插件的数据源：

- **sc-lang**（vscode-sc）：作用域感知自动完成、实时诊断、悬停、跳转定义、
  文档符号、格式化（基于 `--emit-sc`）。
- **sc-ast-view**（vscode-sc-ast）：AST 树视图与树结构源码对照。

插件通过 `scc - --ast`（stdin）调用，按文档版本缓存。

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
| `compiler/src/ast_print.cpp` | 表达式/类型/字段文本序列化（sc 后端与 JSON 后端共用） |
| `compiler/src/main.cpp` | 入口：参数解析、配置加载、模块图、编译+执行、错误格式化 |

## 12. 路线

- 一期（当前）：sc → C 转译。
- 二期：基于同一 AST 增加 LLVM IR 后端，直接作为 LLVM 前端。
