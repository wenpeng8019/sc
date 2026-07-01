# 设计文档 —— 程序结构依赖图

> 本文是实现蓝图。术语体系独立于运行时 `tok` 机制。

## 0. 与现有编译器的对应（复用点）

| 需求 | 现有可复用件（文件·符号） |
|---|---|
| 结构树 | `Program.decls`（顶层）+ `Decl.body`/`structCommon`/嵌套 `Stmt.decl`（`ast.h`）|
| 引用采集骨架 | `semantic.cpp` 的 `usageType/usageExpr/usageStmt`（已递归遍历类型/表达式/语句）|
| 顶层符号表 | `Checker::collectTop()`（`semantic.cpp`）；方法按 `methodOwner+methodName` 索引 |
| 图分析先例 | `checkTokDepGraphCycles()`（有向环检测）、`checkDeadCode()`（不可达）|
| 整程序加载 | `loadUnitGraph()` + `struct UnitInfo`（`main.cpp`）：递归解析全部 `inc` 依赖、去重、各单元完整 AST |
| 模块路径解析 | `resolveModulePath()` / `resolveUnitDeps()`（`main.cpp`）|
| 导出模式派发 | `--ast`→`emitAstJson`、`--api`→`emitScApi`（`main.cpp` 参数解析 + `-o` 后缀推导）|
| 节点元数据 | `Decl` 已带 `exported`/`external`/`used`/`origin`/`inlinedFrom`/`methodOwner`/`line` |

> **关键复用**：`loadUnitGraph()` 已经能把入口单元 + 全部递归 `inc` 依赖解析成
> `unordered_map<string, UnitInfo>`（每个 `UnitInfo.prog` 是**含函数体的完整 AST**）。
> 整程序图直接在这份 map 上建即可，无需自己写模块发现。

## 1. 核心模型

### 1.1 节点（Node）= 一个程序结构对象（Decl 级）

```
Node {
  id        : 稳定唯一键。跨模块用 "<moduleToken>::<name>"；方法用 "<owner>::<method>"
  kind      : def(enum/struct/union/alias) | fnc | rpc | var | let | tls
              | method | dim | tok | dep | test | macro
  name      : 显示名
  module    : 所属单元规范化路径 / origin（跨模块聚合、按模块折叠用）
  file,line : 源码定位（跳转）
  exported  : @ 导出（库根 / 模块间边判定）
  external  : 来自 inc 的外部符号（整程序模式下应能解析到真实定义单元；unit 模式为叶子）
  parent    : 容器节点 id（树的父边：方法→owner、内嵌 def→外层 fnc）；无则空
  active    : 激活分析结果（被根递归可达）
  depth     : 距最近根的最短依赖深度（分层布局用；未激活为 -1）
  fanIn     : 入度（被依赖次数）
  fanOut    : 出度（依赖他者次数）
}
```

粒度：**Decl 级**。方法/维度是归属其 owner 的独立节点，用 `parent` 记录树的包含关系。

### 1.2 边（Edge）= 一次依赖，带种类

```
Edge { from: NodeId, to: NodeId, kind: EdgeKind, line: int }
```

`EdgeKind`（一次到位，全部实现）：

| kind | 含义 | 采集点 |
|---|---|---|
| `call` | 函数/rpc 调用 | `Expr::Call`，callee 为 `Ident` 解析到函数节点 |
| `type` | 类型引用 | 字段/参数/返回/别名基类型/`cast`/`sizeof(T)`（`TypeRef.name`）|
| `read` | 读全局 `var`/`let`/`tls` | `Expr::Ident` 命中全局量（右值上下文）|
| `write` | 写全局量 | 赋值左值命中全局量 |
| `method` | 方法调用 / 维度分派 | `Expr::Member`（`.`/`->`）+ callee 解析到 owner 方法 |
| `construct` | 声明即构造 / `T()` / `T@` | 构造点解析到类型节点 |
| `macro` | `mix` / 宏展开引用 | `MixD.expr`（`Expr::Call`）→ 宏节点 |
| `tokdep` | `dep…map/loop` 声明的 token 边 | `DepD.depItems`/`depTargets`（把静态声明的 token 依赖并入）|
| `import` | 模块级 `inc`/`add` 边（粗粒度）| `IncD`/`AddD` → 模块节点（可视化模块层）|

> 边按 `kind` 可过滤：只看调用图、只看类型图，或全量叠加。

## 2. 整程序 vs 单元图

- **整程序（默认）**：调用 `loadUnitGraph(entry, units, …)` 得到全部单元的完整 AST；
  对每个单元的**本地（非 external）声明**建节点，跨模块引用经全局符号表解析到定义单元的节点。
  `external` 描述符不建独立节点（它只是同一符号在消费单元的影子），统一归并到定义单元节点。
- **单元图（`--graph=unit`）**：只对当前 TU（含 `add` 内联）建节点；未解析到本地定义的
  引用落到「外部叶子节点」（`external=true`，`module=origin`）。

## 3. 边采集与解析（核心难点）

现有 `collectExternalRefs` 只产出**扁平名字集合**。改造为**带上下文的访问器**：

1. **全局符号表**（每单元一份 + 整程序合并）：
   - `name → NodeId`：顶层 `fnc`/`def`/`var`/`let`/`tls`/`enum`…
   - `(owner, method) → NodeId`：方法 / 维度
   - 冲突/重名：沿用 `checkDuplicateDefs` 的判定；跨模块同名归并到定义单元。
2. **带作用域遍历**每个源节点的签名 + body：
   - `Expr::Call`.callee(`Ident`) → `call`；`Member` callee → `method`。
   - `TypeRef.name` → `type`（字段/参数/返回/别名/cast/sizeof）。
   - `Expr::Ident` 命中全局量 → `read`（赋值左值 → `write`）。
   - 构造点 → `construct`；`MixD` → `macro`；`DepD` → `tokdep`；`IncD`/`AddD` → `import`。
3. **排除局部作用域假边（正确性关键）**：
   - 维护局部名栈（形参、局部 `var`、`for`/`case` 绑定名）；命中局部名 **不建边**，
     避免把 `for i` 误连到全局 `i`。这是 `collectExternalRefs`「宁多勿少」策略**不做**、
     但依赖图**必须做**的精化。
4. **动态点保守处理**（不追求完备）：
   - 方法/维度动态分派、函数指针：解析到「同名方法全集」或标注 `dynamic`。
   - 泛型 `mix` 单态化：`--graph` 走 `expandGenericMixes` 展开后的具体 Decl 建边。

## 4. 激活分析（从根递归可达）

照搬 `checkDeadCode`/`checkTokDepGraphCycles` 的图遍历套路：

1. **确定根集合 roots**：
   - 可执行单元：`main`（`FuncD`, name=="main", 非 external、非方法）。
   - 库单元（无 main）：全部 `@导出` 对象（多个顶级入口）。
   - 附加根（可选）：`test` 块（`--test` 视图下）。
2. **正向 BFS** 沿出边标记 `active=true`，记录 `depth`。
3. 未标记者 = **未激活/死对象**（可视化灰显；也是比 `analyzeExternalUsage` 更细的「未使用」信号）。
4. **反向图** 同步烘焙（每节点入边），支持「谁依赖我」查询。
5. **环检测/SCC**：复用 tok 那套；标记依赖环（递归/互递归），SCC 缩点用于分层布局。

## 5. 编译器接口

对齐 `--ast`/`--api`：

```
scc app.sc --graph             # 整程序，JSON 到 stdout
scc app.sc --graph -o g.json   # JSON 文件
scc app.sc --graph -o g.html   # 自包含 HTML（数据内嵌 + 查看器），按后缀切换
scc app.sc --graph=unit        # 仅当前单元
```

`-o` 裸值后缀推导：`--graph → .graph.json`（默认），显式 `.html` 后缀则出 HTML。

### JSON schema（草案，数据契约）

```jsonc
{
  "schema": "sc-proggraph/1",
  "root": "app.sc",
  "mode": "whole" | "unit",
  "entry": { "kind": "main" | "exports", "roots": ["app::main"] },
  "nodes": [
    { "id":"app::main", "kind":"fnc", "name":"main", "module":"app.sc",
      "file":"app.sc", "line":42, "exported":false, "external":false,
      "parent":null, "active":true, "depth":0, "fanIn":0, "fanOut":3 }
  ],
  "edges": [
    { "from":"app::main", "to":"utils::parse", "kind":"call", "line":45 }
  ],
  "stats": { "nodes":.., "edges":.., "active":.., "dead":.., "cycles":[["a","b"]] }
}
```

## 6. 可视化（HTML，默认交付）

- **scc 直出自包含 HTML**：`--graph -o g.html` 时把 JSON 内嵌进内置模板
  （`proggraph/viewer/` 维护模板；编译进二进制或运行期读取）。零外部依赖、离线双击可用。
- 渲染优先**无依赖手写 canvas 力导向**（最稳、可内联）；允许 CDN 时可换 d3-force/cytoscape。
- 交互（渐进）：
  - 力导向布局；按 `kind` 过滤边；按 `module` 分组/着色。
  - 激活高亮：`active` 实心、死对象灰显；点节点高亮其正/反向可达路径。
  - 树↔图切换：折叠成 module/owner 层级 vs 展开全边（承接「模块间/模块内」两层视图）。
  - 从根的 BFS 分层（用 `depth`）。
- **JSON 优先**：HTML 只是 JSON 的一个查看器；用户可拿 JSON 自建可视化。

## 7. 实现落点（保持解耦）

```
compiler/src/graph.h / graph.cpp   新增：建图 + 激活 + JSON/HTML 导出
compiler/src/semantic.*            小改：把 usage 访问器提取为可复用（带回调/上下文）
compiler/src/main.cpp              小改：新增 --graph 模式派发 + -o 后缀 + 调用 loadUnitGraph
proggraph/viewer/viewer.html        可视化模板（P4）
tests/cases/graph_*.sc + golden    回归用例（JSON 稳定性）
```

原则：`graph.cpp` 只读 AST，产出数据 + HTML，**不触碰 codegen**，与三后端并列解耦。
