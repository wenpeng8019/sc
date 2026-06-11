#pragma once
#include "ast.h"

// 轻量语义检查：
// - 基础类型兼容（赋值/返回）
// - nil 仅可赋给指针/数组
// - 基础指针安全（* 与 [] 操作数必须可解引用）
void semanticCheck(const Program& prog);
