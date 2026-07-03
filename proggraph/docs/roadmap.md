# 路线图 —— 程序结构依赖图

分阶段推进，每阶段可独立验证、可回归锁定。当前进度见 [../PROGRESS.md](../PROGRESS.md)。

## P0 · 单元调用图骨架
**目标**：跑通最小闭环 —— 单元内 Decl 级节点 + `call`/`type` 边 + `--graph=unit` JSON。
- [x] 新增 `proggraph.h`/`proggraph.cpp`，`struct GNode`/`GEdge` + `Builder`。
- [x] 从单个 `Program` 建节点（顶层 Decl + 方法）。
- [x] 建全局符号表；采集 `call` + `type` 边；实现局部名作用域排除。
- [x] `emitGraphJson()` 输出 schema v1（nodes/edges/stats）。
- [x] `main.cpp` 加 `--graph` 模式派发，`-o *.json` 后缀推导。
- **验收**：✅ `graph_demo` 导出 JSON 节点/边正确；回归用例 `tests/cases/graph_demo.sc` 锁定。

## P1 · 激活分析
**目标**：根可达 + 死对象标记 + 反向图 + 环检测。
- [x] 根集合：`main`（可执行）/ `@导出`（库）。
- [x] 正向 BFS 标 `active`/`depth`；反向图建入边。
- [x] SCC/环检测（Tarjan），填 `stats.cycles`。
- **验收**：✅ 未被 main 引用的对象标 `active:0`（whole-program 死对象计数正确）。

## P2 · 整程序图（默认模式）
**目标**：`--graph` 默认递归解析全部 `inc` 依赖，union 成整程序图。
- [x] 复用 `loadUnitGraph()` 得到全部 `UnitInfo`。
- [x] 跨模块符号解析：`external` 影子归并到定义单元节点。
- [x] 按规范化路径去重；`module` 字段填充。
- **验收**：✅ `ws_echo.sc` 整程序图 123 节点/75 边/35 激活/82 死，跨模块 `call` 连通。

## P3 · 全边种类补全
**目标**：`read`/`write`/`method`/`construct`/`macro`/`tokdep`/`import` 全部落地。
- [x] 全局量读/写区分（赋值左值上下文）。
- [x] 方法调用 `Member` 解析（含维度分派保守边）。
- [x] 构造点 `construct`；`MixD` `macro`；`DepD` `tokdep`；`IncD`/`AddD` `import`。
- **验收**：✅ `graph_demo` 覆盖 type/read/write/call/method；whole-program 另见 construct/import。

## P4 · HTML 可视化（默认交付）
**目标**：`--graph -o g.html` 直出自包含离线页面。
- [x] `compiler/src/proggraph_viewer.inc` 模板（无依赖 canvas 力导向；内联进编译器）。
- [x] 编译器按 `-o` 后缀切换：`.html` 内嵌 JSON + 模板；`.json`/stdout 出原始数据。
- [x] 交互：边种类过滤、模块着色/折叠、激活高亮、点节点看源码信息、邻居高亮、搜索。
- **验收**：✅ `-o *.html` 自包含页面（marker 替换、内嵌 JSON 合法）；同一 JSON 可被外部工具消费。

## P5 · 增强（可选，按需）
- [ ] 模块间 / 模块内 两层视图切换（在 Decl 级图上按 module 折叠）。
- [ ] VS Code `vscode-ast` 插件内嵌 Webview（点节点跳转源码、hover 签名）。
- [ ] 「谁依赖我」反向查询 API / CLI 子命令。
- [ ] 增量/缓存；大图性能优化（分层/聚类/虚拟化渲染）。

## 贯穿性约束（每阶段都要守）
- 全量回归 `./tests/run.sh` 保持 0 fail；金标用 `--update` 更新前先人工核对。
- `graph.cpp` 只读 AST，不改 codegen / 运行时行为。
- 术语独立于 `tok`；遵守 sc 书写规范（4 空格缩进、保留字 `run`/`done`/`for` 等）。
- macOS 无 `timeout`，用 `gtimeout`（coreutils）。
