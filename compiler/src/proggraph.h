// ============================================================
// 程序结构依赖图（Structure Dependency Graph）—— 编译期只读分析
// ============================================================
// 把 AST 烘焙成「结构对象 = 节点、相互调用/引用 = 有向边」的依赖图：
//   · 节点粒度 = Decl 级（顶层 def/fnc/var/let/tls + 方法/维度 + tok/dep/test/macro）
//   · 边种类   = call/type/read/write/method/construct/macro/tokdep/import
//   · 激活分析 = 从 main（可执行）或 @导出（库）递归可达 → active
//   · 导出     = JSON（数据契约）/ 自包含 HTML（离线可视化）
// 术语体系独立于运行时 tok 机制。只读 AST，不触碰 codegen。
// 详见子项目 proggraph/。
// ============================================================
#pragma once
#include "ast.h"
#include <string>
#include <vector>

// 参与建图的一个编译单元（整程序模式下每个 inc 依赖各一份）。
struct GraphUnit {
    std::string    path;   // 规范化源路径（模块标识）
    const Program* prog;   // 该单元的完整 AST（含函数体）
};

// 建图并导出 JSON。
//   units：参与建图的单元集合（整程序=入口+全部递归依赖；单元=仅一个）
//   rootPath：入口单元路径（用于 root 字段与 main 归属）
//   whole：true=整程序模式，false=单元模式（外部引用建为叶子节点）
std::string emitGraphJson(const std::vector<GraphUnit>& units,
                          const std::string& rootPath, bool whole);

// 把 JSON 图数据包进自包含 HTML 查看器（数据内嵌，离线可用）。
std::string emitGraphHtml(const std::string& json);
