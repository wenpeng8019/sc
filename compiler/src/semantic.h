#pragma once
#include "ast.h"
#include <string>
#include <unordered_set>
#include <vector>

// 语义分析检查
void semanticCheck(const Program& prog);

// 注册「编译器默认 #include 的内置 C 头」（platform.h）所声明的符号，使其函数 /
// 宏 / 类型名在语义检查中视为已知——区别于写死的 libc 白名单：platform.h 是可编辑、
// 可扩展的「我们自己的」头，故按内容动态扫描，往后增删符号无需再改编译器。
// 传入头文件全文；重复调用幂等（集合去重）。
void registerBuiltinHeaderSymbols(const std::string& headerText);

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
