#pragma once
#include "ast.h"
#include "lexer.h"

// ============================================================
// 语法分析器 —— 将 Token 序列转换为 AST（Program 树）
// ============================================================
// 采用经典递归下降（Recursive Descent）算法。
//
// 表达式解析层次（从低到高优先级）：
//   parseExpr()    → 赋值（=, +=, -= ...）          右结合
//     parseTernary() → 条件（? :）                   右结合
//       parseBinary() → 二元运算                       左结合，按优先级递归
//         parseUnary() → 前缀一元（! - ~ * & ++ --）
//           parsePostfix() → 后缀（++ --）+ 调用链（() [] . ->）
//             parsePrimary() → 原子（字面量/标识符/括号）
//
// 语句/声明解析：
//   parseProgram() → 遍历 token，按首关键字分发到 parseDef/Fnc/VarList
//   parseStmt()    → 按首关键字分发到各语句解析函数
//
// 错误处理：
//   所有语法错误通过 throw CompileError 报告，携带行号，main 统一捕获输出。
//
// shaderMode（GPU/着色器扩展 syntax-g）：仅用于 .sg 源。为真时，顶层标识符
//   vert/frag/comp 被识别为着色阶段入口声明（否则仍是普通标识符，保持 .sc 方言
//   与全部回归不受影响）。默认 false，现有调用点无需改动。
// ============================================================
Program parse(const std::vector<Token>& toks, bool shaderMode = false);
