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

# .sc -> .c
./build/scc ../examples/demo.sc -o demo.c
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
- `name&` 指针、`name[n]` 数组；无类型对象默认 `char*`，无类型指针默认 `void*`
- 基本类型：`i1/i2/i4/i8`、`u1/u2/u4/u8`、`f4/f8`、`v`(void)
- 函数默认返回类型 `i4`；`fnc name -> func_type` 实现预定义函数类型
- 多行子项与函数体/语句体之间以单独一行 `-` 分隔
