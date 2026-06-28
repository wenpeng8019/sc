# list-schedule —— 工作流计算图框架（就绪优先队列 + 列表调度）

与 [back-drain](../back-drain/) 是**同一张依赖图、同一条流水线、同样的确定性结果**
的姊妹模板，唯独**「如何发现下一个要处理的节点」**不同。它演示经典的**列表调度
（list scheduling）**：维护一张 **ready-list**，节点入度归零（输入就绪）即按**权重**入列，
线程从表头取「最该先跑」的节点执行。

## 核心对照：发现机制

| | back-drain | **list-schedule（本模板）** |
|---|----------------|------------------------------|
| 发现下一个节点 | `back fuse` 沿反向邻接**遍历整张图**，按反拓扑序逐节点查待处理帧 | **pop 一条全局优先队列的队顶** |
| 发现成本 | O(图规模)，每 worker 每轮一次反向扫描 | O(log n) 出堆 |
| 排序依据 | 反拓扑序（最深先行，固定） | **节点权重可插拔**（depth / critical / slack…） |
| 额外结构 | 无（与记忆化天然耦合） | 一张就绪堆（`heap`） |

两者**同享** tok 拓扑 + 编译期烘焙度量 + `dep` 前向路由 + 节点 `kernel`，区别仅在发现。

## 节点依赖性权重 = 编译期烘焙的 `depth()`

list scheduling 的关键是 ready-list 的排序键——**节点依赖性权重**。本模板直接取 tok 在编译期
烘焙好的 `depth()`（拓扑层级 = 最长路径分层），运行时 O(1) 查表，**调度器零图遍历**：

- **权重 = `depth`、最大堆**（本模板）：最深就绪节点先行 → 优先排空下游、最小化在管缓冲。
  这恰好**复现 back-drain 中 `back` 的「最深先行」次序**（tok 的 back 节点模式本就按
  depth 降序唤起），但把「每 worker 每轮反向扫全图」换成「一次 O(log n) 出堆」。

换权重或换堆向即换调度纪律（骨架不动，见 `schedule.sc` 文末扩展）：

| 权重 / 堆向 | 调度纪律 |
|------------|----------|
| `depth` + 最大堆 | 最深先行：排空下游、最小化缓冲（本模板） |
| `depth` + 最小堆 | 最浅先行：前馈优先、最大化并行铺开 |
| `critical()` | 关键路径优先：经典 HLFET，缩短 makespan |
| `slack()`（取负） | 最小松弛优先（MSF） |

## 运行

```sh
./compiler/build/scc templates/workflow-graph/list-schedule/schedule.sc
```

输出：先打印整张图的烘焙度量表（`depth` 列即调度权重），再以 4 线程按就绪堆列表调度处理
8 帧流水线，最后打印确定性结果——**完成帧数=8、输出校验和=770、各级处理计数 gray=blur=edges=fuse=8**，
与 back-drain 完全一致（证明换调度器不改语义）。

## 内置流水线（线性，单 sink）

```
capture ─► gray ─► blur ─► edges ─► fuse(sink)
   d0       d1      d2      d3       d4        ← depth = 调度权重
```

- `capture` 纯源（`depth=0`）：`set` 即产出，把帧路由进 `gray` 队列。
- `gray/blur/edges/fuse` 处理节点：各带环形缓冲 + 状态机 + kernel + 权重，由池中 worker 按
  就绪堆调度认领。kernel：`gray=in/3`、`blur=in`、`edges=in*2`、`fuse=in+7`。

## 执行模型（就绪队列发现）

1. **dep…map = 前向路由器**：上游 `set` 落值 → 经 `t->ctx()` 取下游节点 → `wn_push` 入队。
2. **入队即入就绪堆**：`wn_push` 在节点 `S_IDLE→S_PEND` 时，按权重把节点压入 `flow.rq`
   （就绪堆，value=节点指针装箱的 `@`）并唤醒一个 worker——「入度归零即入 ready-list」。
3. **worker = pop 队顶执行**：常驻 worker 阻塞等就绪堆非空 → `wf_take` pop 堆顶（权重最高的
   就绪节点）+ 认领一帧 → 锁外跑 `kernel` → `t->set` 写输出（触发下游路由器入队）→ `wn_done`
   收尾（若该节点队列仍有积压，按权重**重入就绪堆**）。

节点状态机 `S_IDLE → S_PEND（在堆中）→ S_RUN（已出堆认领）`，配「仅 IDLE→PEND 才入堆」
保证**每节点同一时刻至多一个堆条目、至多一个 worker 处理**。`flow.inflight` 计在管帧数，
归零即排空完成、唤醒主线程。

## 扩展

加一处理节点 = 加 `tok` + `wnode` + `kernel` + 一条 `dep…map` 边，并在 `main` 中
`N_n.weight = N->depth()` 灌权重——度量与权重由编译器自动重新烘焙。详见 `schedule.sc` 文末。
