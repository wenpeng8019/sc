# sc 语言

基于 C 理念的结构化语言。核心：程序即结构，由 `def`/`fnc`/`var`/`let` 四类程序结构对象构成树。
语言定义见 [syntax](syntax)。

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
# .sc 配置文件格式（每行 key = value，# 注释）：cc = clang
# '--' 后的参数透传给程序
./build/scc ../examples/demo.sc
SCC_CC=clang ./build/scc ../examples/demo.sc -- arg1 arg2

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
- `inc stdio.h` 引入头文件（对齐 C 的 `#include`）
- `@def`/`@fnc`/`@var`/`@let` 导出对象：`--emit-c -o x.c` 时额外生成 `x.h` 声明
- 基本类型：`i1/i2/i4/i8`、`u1/u2/u4/u8`、`f4/f8`、`v`(void)
- 函数默认返回类型 `i4`；`fnc name -> func_type` 实现预定义函数类型
- 多行子项与函数体/语句体之间以单独一行 `-` 分隔
