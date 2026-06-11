#pragma once
#include "ast.h"
#include <string>

// ============================================================
// C 代码生成器 —— AST → C 源码（一期后端）
// ============================================================
// 将解析完成的 AST 转译为等价的 C 语言源码。
//
// 类型映射规则：
//   sc 内置类型 → C 标准类型（通过 mapBase() 映射）：
//     i1→int8_t, i2→int16_t, i4→int32_t, i8→int64_t
//     u1→uint8_t, u2→uint16_t, u4→uint32_t, u8→uint64_t
//     f4→float, f8→double, v→void
//   未指定类型 → 默认推断（无指针→char*, 有指针→void*）
//
// 函数类型展开：
//   fnc name -> func_type   → 从已注册的函数类型表中查找签名并展开为 C 函数
//
// 输出结构：
//   1. 头文件 #include
//   2. 类型定义（typedef enum/struct/union/alias）
//   3. 全局变量声明
//   4. 非 main 函数的原型声明（forward declaration）
//   5. 函数体实现（包括 main）
//
// 后续规划：二期基于同一 AST 增加 LLVM IR 后端，AST 与后端完全解耦。
// ============================================================
std::string emitC(const Program& prog);
