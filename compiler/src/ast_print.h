#pragma once
#include "ast.h"
#include <string>

// ============================================================
// AST 文本序列化工具 —— AST 子树 → 规范化文本片段
// ============================================================
// 这是 codegen_sc 和 ast_json 的公共底层工具。
// 提供表达式、类型、字段的字符串化能力，避免在多个后端中重复写打印逻辑。
//
// 函数说明：
//   exprToStr(e)     — 表达式 → 文本（递归输出，自动加括号控制优先级）
//   typeToStr(t)     — 类型引用 → 文本（如 "i4&" "MyType&&"）
//   inlineStr(t)     — 内联结构/联合 → 文本（如 "{ x:i4, y:i4 }"）
//   fieldDetail(f)   — 字段的类型部分 → 文本（如 "&: i4 = 1"）
//   fieldToStr(f)    — 完整字段 → 文本（如 "name&: i4 = 1"）
// ============================================================
std::string exprToStr(const Expr& e);                    // 表达式→字符串（含自动加括号）
std::string typeToStr(const TypeRef& t);                 // 类型引用→字符串 eg. "i4&&"
std::string inlineStr(const TypeRef& t);                 // 内联类型→字符串 eg. "{x:i4,y:i4}"
std::string fieldDetail(const Field& f, bool withInit);  // 字段类型与初值部分 eg. "&:i4 = 1"
std::string fieldToStr(const Field& f, bool withInit);   // 完整字段名+类型+初值
