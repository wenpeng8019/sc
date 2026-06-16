#pragma once
#include "ast.h"
#include <string>
#include <unordered_set>
#include <vector>

// 语义分析检查
void semanticCheck(const Program& prog);

// 采集当前单元"自身代码"（非 external 声明）引用到的所有名字
//（标识符 / 类型名 / 成员名 / 表达式 op/text）。供外部描述符使用分析与
// C 头描述符匹配（cheaders）共用，避免重复遍历逻辑。
std::unordered_set<std::string> collectExternalRefs(const Program& prog);

// 外部描述符使用分析：
//   1. 统计当前单元（非 external 声明）引用到的标识符/类型名/成员名；
//   2. 据此把每个 external 描述符标记为 used（已引用）或未用；
//   3. 对"贡献了描述符却全部未被引用"的来源（.sc 模块 / C 头）返回导入未使用警告。
// 会就地修改 prog（设置 Decl::used / externDeclared / externAnalyzed），
// 返回非致命警告列表（供 --ast 携带给插件）。
std::vector<Diagnostic> analyzeExternalUsage(Program& prog);
