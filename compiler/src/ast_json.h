#pragma once
#include "ast.h"
#include <string>

// ============================================================
// AST JSON 导出器 —— AST → JSON 树（供 VSCode AST 视图插件渲染）
// ============================================================
//
// 输出格式：单行 JSON，根节点为 "program"，每个节点的结构：
//   {
//     "k": "节点种类" ,    // kind: program/enum/struct/union/alias/fnctype/fnc/
//                         //       var/let/if/else/while/for/return/break/
//                         //       continue/expr/field/param/item
//     "n": "节点名称",     // name（可选，变量名/函数名/类型名等）
//     "d": "详情文本",     // detail（可选，类型信息/值/条件表达式等）
//     "l": 行号,           // line（可选，用于 IDE 跳转到源码行）
//     "c": [子节点, ...]   // children（可选，嵌套子节点数组，形成树结构）
//   }
//
// VSCode 插件 extension.js 解析此 JSON 并在 TreeView 中渲染为可折叠树。
// 点击带 line 的节点可跳转到源码对应行。
//
// 外部描述符（external，来自 inc 导入）节点附带 "x":1 与 "u":0/1（是否被引用）；
// 根节点可附带 "w":[{"m":消息,"l":行}] 携带非致命警告（如导入未使用）。
// ============================================================
#include <vector>
std::string emitAstJson(const Program& prog, const std::vector<Diagnostic>& warnings = {});
