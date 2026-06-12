# sc 语言

基于 C 的结构化语言。核心理念：程序即结构，由 `def`/`fnc`/`var`/`let` 四类程序结构对象构成树（另有 `rpc` 作为 `fnc` 的伪形参变体）。
语言定义见 [syntax](syntax)，编译器参考手册见 [compiler.md](compiler.md)，VS Code 调试配置见 [debugging.md](debugging.md)。

## 定位：与 C 共生，而非取代

- **基于 C，而非取代 C**：sc 转译为 C，复用 C 的编译器、调试器、库与 ABI 生态，不另建世界。
- **C 的前台语言 + 工作流工具**：日常代码用 sc 书写；scc 同时简化 C 的工作流——
  直接运行（类解释器）、一命令出产物（`--build`）、自动生成头文件、模块依赖自动编译链接，
  免写 Makefile 与重复声明。
- **分工明确**：sc 专注更高层的语言逻辑特性（结构化程序树、伪类方法、模块导出、
  定义顺序无关、语义检查）；低级的平台/设备能力（宏适配、内联汇编、位域、
  volatile、ABI 细节）仍由 C 完成，写在 C 头文件/源文件里经 `inc` 直接使用。
- **双向互操**：sc 用 C 的一切（libc/POSIX/任意 C 库、宏常量、函数式宏）；
  C 也能用 sc 产出的库与头文件（`@` 导出 + `--build -o lib*.a`）。

## 目录

- `compiler/` — scc 编译器（C++17，手写词法 + 递归下降，AST 与后端解耦）
- `examples/` — 示例源码（`.sc`）
- `vscode-sc/` — VSCode 语法高亮插件

## 构建与使用

```sh
cd compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 默认：编译+执行（类似解释器，不保存中间文件）
# C 编译器优先级：环境变量 SCC_CC > CC > 当前目录 .sc 配置文件 > gcc
# 工具链配置（运行/构建模式；环境变量 > .sc 配置文件，逐键独立）：
#   SCC_CFLAGS/cflags 编译选项 | SCC_LDFLAGS/ldflags 链接选项
#   SCC_INC/inc 头文件路径(:分隔→-I) | SCC_LIB/lib 库路径(:分隔→-L) | SCC_LIBS/libs 库名(→-l)
# .sc 配置文件格式（每行 key = value，# 注释）：cc = clang
# '--' 后的参数透传给程序；-l <名>/-lm 追加链接库
./build/scc ../examples/demo.sc
SCC_CC=clang ./build/scc ../examples/demo.sc -- arg1 arg2
SCC_LIB=/opt/homebrew/lib ./build/scc app.sc -l curl -lm

# 构建产物模式：可执行 / 静态库 / 动态库（按 -o 后缀决定；缺省为输入名去 .sc）
./build/scc app.sc --build -o myapp
./build/scc util.sc --build -o libutil.a      # 同时生成 libutil.h（@导出接口）
./build/scc util.sc --build -o libutil.dylib  # Linux 用 .so

# 转译为 C 源码；有 @导出对象时额外生成同名 .h（demo.h）
./build/scc ../examples/demo.sc --emit-c -o demo.c
cc demo.c -o demo && ./demo
```

## 路线

- 一期（当前）：sc → C 转译（`codegen_c`）
- 二期：基于同一 AST 增加 LLVM IR 后端，直接作为 LLVM 前端

## VSCode 插件安装

```sh
ln -s "$(pwd)/vscode-sc" ~/.vscode/extensions/sc-lang-0.1.0
```

重启 VSCode 后 `.sc` 文件自动高亮。

## 约定速记

- 缩进表示结构（Tab 或 4 空格为一级），`#` 行注释
- `name&` 指针、`name&&` 指针的指针、`name[x][y]` 多维数组；无类型对象默认 `char*`，无类型指针默认 `void*`
- `inc stdio.h` 引入头文件（对齐 C 的 `#include`）；`inc x.sc` 导入 sc 模块（单元编译+链接）
- 无预处理器设计：sc 不提供宏/条件编译，平台适配写在 C 头文件里，`inc` 导入后直接用适配结果（C 宏常量/函数式宏可直接使用）
- `@def`/`@fnc`/`@var`/`@let` 导出对象：`--emit-c -o x.c` 时额外生成 `x.h` 声明；未导出顶层符号默认 C `static`
- 基本类型：`i1/i2/i4/i8`、`u1/u2/u4/u8`、`f4/f8`、`v`(void)、`b`(bool)
- 强转 `(expr: type&)` 等价 C 的 `(type*)(expr)`；初始化列表 `{1, 2, 3}`；十六进制 `0xFF` 与字面量后缀 `1u/100UL/3.14f`
- 函数默认返回类型 `i4`；`fnc name -> func_type` 实现预定义函数类型
- `rpc` 伪形参函数：形式同 `fnc`，参数/返回值展开为同名结构体，实际函数为 `void name_rpc(struct name*)`，返回槽是首成员 `_`
- 多行子项与函数体/语句体之间以单独一行 `-` 分隔
