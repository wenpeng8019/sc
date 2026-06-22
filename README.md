# sc 语言

基于 C 的结构化语言。核心理念：程序即结构，由 `def`/`fnc`/`var`/`let` 四类程序结构对象构成树（另有 `rpc` 作为 `fnc` 的伪形参变体、以及 `tls` 作为 `var` 的线程模式变体）。
语言定义见 [syntax](syntax)，编译器参考手册见 [compiler.md](compiler.md)，交叉编译参考见 [cross-compile.md](cross-compile.md)，VS Code 调试配置见 [debugging.md](debugging.md)。

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
- `tests/` — 回归测试（`--emit-c`/`--emit-sc` 产物黄金快照，供优化后回归验证）
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
./build/scc ../examples/feature1.sc
SCC_CC=clang ./build/scc ../examples/feature1.sc -- arg1 arg2
SCC_LIB=/opt/homebrew/lib ./build/scc app.sc -l curl -lm

# 构建产物模式：可执行 / 静态库 / 动态库（按 -o 后缀决定；缺省为输入名去 .sc）
./build/scc app.sc --build -o myapp
./build/scc util.sc --build -o libutil.a      # 同时生成 libutil.h（@导出接口）
./build/scc util.sc --build -o libutil.dylib  # Linux 用 .so

# 转译为 C 源码；有 @导出对象时额外生成同名 .h（feature1.h）
./build/scc ../examples/feature1.sc --emit-c -o feature1.c
cc feature1.c -o feature1 && ./feature1

# 单元测试：编译并运行目标文件的 tst 用例（见 syntax.md §11.8），退出码=失败用例数
./build/scc ../examples/test_demo.sc --test

# 交叉编译：把工具链整套换成目标工具链，配置写进 .target 目标档（详见 cross-compile.md）
./build/scc app.sc --build -o app --target ../examples/targets/aarch64-linux.target
./build/scc fw.sc  --build -o fw.bin --target ../examples/targets/cortex-m4.target \
            --builtins boards/m4/builtins      # 裸机：.bin 镜像 + 目标适配库
```

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
- 基本类型：`i1/i2/i4/i8`、`u1/u2/u4/u8`、`f4/f8`、`bool`、`char`；省略返回类型即 void
- 强转 `expr: type&` 等价 C 的 `(type*)(expr)`，右值位置免括号（后续 `->` 操作需括号）；初始化列表 `{1, 2, 3}`；十六进制 `0xFF` 与字面量后缀 `1u/100UL/3.14f`
- 函数默认返回类型 `i4`；`fnc name -> func_type` 实现预定义函数类型
- `rpc` 伪形参函数：形式同 `fnc`，参数/返回值展开为同名结构体，实际函数为 `void name_rpc(struct name*)`，返回槽是首成员 `_`
- 方法：成员函数在结构体内直接实现（签名字段+缩进函数体）；`@fnc T::m` 仅声明（C 侧实现）；调用 `o.m()`/`p->m()` 自动注入接收者，实参不足默认补 0；`init` 声明即构造，`T()` 堆构造（malloc+init，返回 `T&`），`drop` 手动析构
- 内置 ADT：`inc adt.sc` 引入 `string`/`list`/`chain`（默认实现自动链接，`--adt` 可替换），见 `builtins/REFERENCE.md`
- 链表结构体：`def T: ~ {}` 成员末尾注入 `_prev`/`_next` 自链指针，配合内置 `chain` 双向链表使用；成员访问位 `prev`/`next` 上下文关键字等价 `_prev`/`_next`
- io 内置关键字：`print("E: %d", n)` C 风格日志输出（F/E/W/I/D/V 级别、`SC_LOG` 过滤，需 `inc io.sc`）；`stringify(值)` JSON 格式化关键字（默认多行美化，返回 `string` 用毕 `drop`；`stringify(值, 缓存, 大小)` 返回缓存 `char&`；选项块 `stringify<compact:1>(值)` 紧凑单行；需 `inc adt.sc` + `inc io.sc`，转 C 时生成独立 `stringify.h`）
- 多行子项与函数体/语句体之间以单独一行 `-` 分隔
