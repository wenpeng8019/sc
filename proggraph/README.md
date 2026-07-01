# proggraph —— 程序结构依赖图（Structure Dependency Graph）

> sc 的核心哲学是「**程序即结构**」：程序是结构对象（`def`/`fnc`/`var`/`let`）的层级树，
> 而结构对象之间的相互调用/引用构成一张有向依赖图。
> 本子项目为编译器新增一个**编译期分析能力**：为每个结构对象烘焙其依赖关系，
> 以 `main`（可执行）或 `@导出`（库）为根做**激活分析**，并把结果导出为 JSON、可视化为 HTML。

这是一个作为**独立子项目**维护的编译器特性：自带需求、路线、设计、进度文档，
迭代节奏与实现分阶段推进，主编译器（`compiler/`）保持 AST 与后端解耦。

## 目录结构

```
proggraph/
  README.md            本文件：子项目总览与导航
  PROGRESS.md          开发进度（阶段勾选 + 变更日志）
  docs/
    requirements.md    需求文档（用户诉求 + 已定型的设计取舍）
    design.md          设计文档（模型 / 边采集 / 激活 / 接口 / 可视化 / 落点）
    roadmap.md         路线图（P0–P5 分阶段计划与验收标准）
  viewer/              自包含 HTML 可视化模板（后续 P4 落地）
  samples/             示例导出（JSON / HTML）用于回归与演示（后续）
```

## 快速导航

- 想知道**要做什么、为什么** → [docs/requirements.md](docs/requirements.md)
- 想知道**怎么做（技术方案）** → [docs/design.md](docs/design.md)
- 想知道**分几步、每步验收** → [docs/roadmap.md](docs/roadmap.md)
- 想知道**现在做到哪了** → [PROGRESS.md](PROGRESS.md)

## 一句话规格

编译器新增 `--graph` 模式：默认对**整程序**（递归解析全部 `inc` 依赖）建立
**Decl 级**结构依赖图，采集调用/类型/读写/方法/构造/宏/token/模块等**全部边种类**，
从 `main`（或库的 `@导出` 集合）做激活可达分析，`-o *.html` 直出自包含可视化页面，
`-o *.json` 输出原始图数据供自定义扩展。术语体系独立于运行时 `tok` 机制。
