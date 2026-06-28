# workflow-graph —— 工作流计算图（驱动模型三连）

把一条数据处理流水线表达为一张**静态 tok 依赖图（DAG）**：每个处理阶段是一个 `tok` 节点、
阶段间数据流是一条 `dep…map` 有向边。本目录收录**驱动同一张工作流图的三种执行模型**——
拓扑、烘焙度量、`dep` 路由、节点 kernel 全部共享，**唯独「图怎么被驱动跑起来」不同**。

## 三种驱动模型

| 子模板 | 驱动模型 | 如何发现/触发下一个节点 | 形态 |
|--------|----------|--------------------------|------|
| [back-drain/](back-drain/) | **拉取式 drain**（demand-driven） | worker 调 `back` 沿反向邻接**遍历整张图**，按反拓扑序认领最深的待处理节点（O(图规模) 反向扫描） | 异步、多线程、缓冲队列 |
| [list-schedule/](list-schedule/) | **列表调度**（demand-driven） | 节点输入就绪即按**权重(depth)**入一条全局**就绪优先队列**；worker pop 队顶（O(log n) 出堆） | 异步、多线程、缓冲队列 |
| [push-reactive/](push-reactive/) | **推送式**（data-driven） | 源 `set` 沿图**同步级联**，上游算出即点燃下游 `combine`，节点 `exec` 即时反应 | 同步、单线程、无队列 |

back-drain 与 list-schedule 是**同一拉取语义、不同发现机制**（反向扫描 vs 就绪队列），结果完全
一致；push-reactive 则是相反方向的推送驱动。三者并列即「数据流执行模型」的完整光谱：

```
        发现机制                         驱动方向
  back-drain ──┐
               ├─ 拉取（下游按需取）  ←──┐
  list-schedule┘                        │  同一张 tok DAG
                                        │
  push-reactive ── 推送（上游产出即推）←─┘
```

## 运行

```sh
./compiler/build/scc templates/workflow-graph/back-drain/workflow.sc
./compiler/build/scc templates/workflow-graph/list-schedule/schedule.sc
./compiler/build/scc templates/workflow-graph/push-reactive/reactive.sc
```

back-drain 与 list-schedule 输出**完全一致**（完成帧数=8、校验和=770，证明换发现机制不改语义）；
push-reactive 演示同步级联 + 反应式记忆化（级联随饱和逐级截断）。各子目录 README 详述其架构与扩展。

## 内置流水线（三者同形，便于对照）

```
capture ─► gray ─► blur ─► edges ─► fuse(sink)
```

复制任一子模板改名即可起一个真实项目：加节点 = 加 `tok` + 一条 `dep…map` 边 + 写 kernel。
