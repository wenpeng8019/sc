#pragma once
#include "ast.h"

// ============================================================
// shader_sema —— GPU/着色器扩展（syntax-s §9）语义子集强制
// ============================================================
// 在 .ss 源解析后、codegen_glsl 发射前运行，把 sc 在 shader 语境下收窄到
// SPIR-V 可安全下译的子集：遇到禁用构造（堆/指针/递归/rpc/async/print/...）
// 即抛 CompileError，文案对齐主手册「零误报、明确指出不支持什么」的风格。
// 同时做基础结构检查（阶段签名、swizzle 字母合法性、绑定冲突）。
//
// 独立于核心 semantic.cpp（见 syntax-s §12）。
// ============================================================

// 对已按 shaderMode 解析的 Program 做 shader 子集语义检查。
// 违反即 throw CompileError（带行号）。
void shaderSemaCheck(const Program& prog);
