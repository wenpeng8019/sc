# 开发进度 —— 程序结构依赖图

> 阶段定义见 [docs/roadmap.md](docs/roadmap.md)。每完成一项勾选，并在变更日志追加一行。

## 阶段总览

| 阶段 | 描述 | 状态 |
|---|---|---|
| P0 | 单元调用图骨架（节点 + `call`/`type` 边 + `--graph=unit` JSON）| 已完成 |
| P1 | 激活分析（根可达 + 死对象 + 反向图 + 环检测）| 已完成 |
| P2 | 整程序图（默认；复用 `loadUnitGraph`）| 已完成 |
| P3 | 全边种类补全（read/write/method/construct/macro/tokdep/import）| 已完成 |
| P4 | HTML 可视化直出（自包含离线页面）| 已完成 |
| P5 | 增强（两层视图 / 插件 Webview / 反查 / 性能）| 未开始 |

状态取值：未开始 / 进行中 / 已完成 / 已验收。

## 当前焦点

- P0–P4 已实现并通过回归（`tests/run.sh` 全绿，含 `graph_demo` 专项用例）。
- 编译器侧落地：`compiler/src/proggraph.{h,cpp}` + `proggraph_viewer.inc`，`main.cpp` 派发 `--graph`/`--graph=unit`。
- 下一步：进入 **P5** 增强，或补充 viewer 浏览器实测与两层视图。

## 关键设计锚点（实现时对照）

- 整程序加载复用 `main.cpp` 的 `loadUnitGraph()` + `struct UnitInfo`。
- 引用采集改造自 `semantic.cpp` 的 `usageType/usageExpr/usageStmt`（加作用域 + 回调）。
- 符号表复用 `Checker::collectTop()` 思路；环检测复用 `checkTokDepGraphCycles()`。
- 节点元数据直接取 `Decl` 的 `exported/external/origin/inlinedFrom/methodOwner/line`。
- 模式派发/`-o` 后缀对齐 `--ast`(`emitAstJson`)、`--api`(`emitScApi`)。

## 变更日志

- 2026-07-01 — 建立子项目 `proggraph/`：README + requirements + design + roadmap + progress。
  设计取舍定型（Decl 级一期、整程序默认、全边种类、库以导出为根、HTML 默认+JSON、术语独立于 tok）。
- 2026-07-01 — 实现 P0–P4：
  - 新增 `compiler/src/proggraph.{h,cpp}`（`GraphUnit`/`emitGraphJson`/`emitGraphHtml`），
    `proggraph_viewer.inc`（自包含 canvas 力导向查看器），并接入 `CMakeLists.txt`。
  - `main.cpp` 新增 `--graph`（整程序，默认）与 `--graph=unit`（仅当前单元）模式；
    `-o *.html` 直出可视化页面，`-o *.json`/stdout 输出 JSON；裸 `-o` 后缀 `.graph.json`。
  - 节点覆盖 module/struct/union/enum/alias/functype/fnc/rpc/method/dim/var/let/tls/tok/test/macro/dep；
    全局 `var/let/tls` 按字段拆为独立节点。边种类：call/type/read/write/method/construct/import/tokdep。
  - 激活分析：`main`(fnc) 或全部 `@导出` 为根，前向 BFS 标记 `active`+`depth`；Tarjan SCC 求环。
  - 新增回归用例 `tests/cases/graph_demo.sc` + 黄金 `tests/golden/graph_demo.graph.json`
    （`tests/run.sh` 加 `--graph=unit` 快照，`norm_graph` 归一化绝对路径）。回归 242 全绿。
