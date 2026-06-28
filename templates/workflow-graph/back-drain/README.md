# back-drain —— 工作流计算图（拉取式 drain：back 反向扫描排空）

把一条数据处理流水线表达为一张**静态有向无环图（DAG）**：每个处理阶段是一个 `tok` 节点，
阶段间的数据流是一条 `dep…map` 有向边。但本模板**不是「设值即同步级联」**，而是一套真实的
**流水线运行时**——节点带**环形缓冲队列**、由**线程池**并发处理、用**状态机**标记待处理帧，
worker 经 **drain（反向遍历）** 认领「最近未处理」的节点。

## 与 tok 的关系：配合，而非合入

| 职责 | 由谁负责 |
|------|----------|
| 拓扑连边 / 编译期烘焙度量 / 反向遍历 `back`（支持 break）/ 节点上下文 `ctx` + 节点钩子 `exec` | **tok 内核** |
| 环形缓冲队列 / 线程池调度 / 节点状态机 / 排空与停机 | **本文件协作层**（基于 `mt` 模块的 mutex/cond/thread） |

tok 不感知队列与线程；协作层不改 tok 内核。为此用到的 tok 机制：**`back` 支持 break**（drain
反向遍历时，钩子返回非 0 即中止本轮扫描，认领并处理一节点后停扫、重扫——天然「下游优先排空」）、
**节点私有上下文 `ctx`**（`form t,v,&n` 把侧车绑到 token，`t->ctx()` 取回）、**节点处理钩子
`exec`**（`form t,v,&n,exec` 绑定；`back` 在节点模式下按反拓扑序对各注册节点唤起，节点自己出队跑 kernel）。

## 运行

```sh
./compiler/build/scc templates/workflow-graph/back-drain/workflow.sc
```

输出：先打印整张图的烘焙度量表（depth/critical/slack/batch/reach/checkpoint），
再以 4 线程 drain 处理 8 帧流水线，最后打印确定性结果（完成帧数、输出校验和、各级处理计数）。

## 内置流水线（线性，单 sink）

```
capture ─► gray ─► blur ─► edges ─► fuse(sink)
```

- `capture` 是纯源（`depth=0`）：`set` 即产出，把帧路由进 `gray` 队列。
- `gray/blur/edges/fuse` 是处理节点：各带一条环形队列 + 状态机，由池中 worker 认领处理。
- kernel：`gray=in/3`、`blur=in`、`edges=in*2`、`fuse=in+7`。

## 执行模型（拉取式 drain：dep 管路由，node 管函数）

**职责分离**——边只路由，节点只处理：

1. **dep…map = 前向路由器**（单分支）：上游有新值时，经目标 token 的 `t->ctx()` 取下游节点，
   **仅把数据入队**（`wn_push`），不计算、不立即传播——解耦快慢级，多帧同时在管（流水线缓冲）。
   边体不硬编码节点变量、不含算子（下游是谁由 ctx 决定）。
2. **node = 函数实现**（`exec` 钩子）：每个处理节点把 kernel 挂在侧车 `wnode` 上，经
   `form t, (0:@), &n, node_drain` 绑定统一的节点钩子。worker 调 `back fuse`——tok 见图中存在
   `exec` 节点即进**节点 drain 模式**，按反拓扑序（最深先行）对各节点唤起 `node_drain`：
   认领成功则出队一帧、跑 `n->kernel`、`t->set` 写输出（触发下游路由器入队），随后
   **`return 1` 请求 break** 停扫重扫；未认领则返 0，继续往更浅节点扫描。

节点状态机：`S_IDLE`（无待处理）→ `S_PEND`（队列有帧、可被认领）→ `S_RUN`（已认领处理中）
→ 处理完队列仍有帧则回 `S_PEND`，否则回 `S_IDLE`。`flow.navail` 计可认领节点数，
`flow.inflight` 计在管帧数（归零即排空完成，唤醒主线程）。

## worker 策略（`WORKER_MODE` 切换：默认按需）

调度逻辑（drain：`back fuse` 反拓扑认领一节点）始终在模板里，**池只负责分配线程**。
两套等价策略，输出完全一致，都是 `pool&`、都凭 `run` 投递，由 `WORKER_MODE` 选择：

| 策略 | 池（均 `pool&`） | `run` 投递的 rpc | worker |
| --- | --- | --- | --- |
| `MODE_ONDEMAND`（默认） | `drain_pool(NW)` | `run<g_dp> wf_drain()`（每次有新活时投） | 按需 0..NW，反复跑 `wf_drain`（有活 `back fuse`、无活即退） |
| `MODE_RESIDENT` | `default_pool(NW)` | `run<p> worker_resident(i)`（启动期投 NW 个） | NW 个常驻，无活阻塞等 `flow.work`，`stop` 后退出 |

关键认识：**`pool` 是 sc 对「线程调度组件」的核心抽象，投递动词只有 `run`**。按需策略不是
另起一个协议，而是 `pool` 的另一种策略 `drain_pool`——`run<dp> wf_drain()` 即「通知有新活 +
按需激活一个 worker」，worker 反复跑 `wf_drain`（其自身 `while` 循环排空，无活即返回）。

**按需策略本质是个信号量**（在跑 worker 数 + 「有新活」世代）——这套一致性已**收敛进内置
`drain_pool`**：`running` 计数与工作世代 `gen` 由池内部锁守护，`run`（先 `gen++` 再判
`running<max` 激活）与 worker「无活退出」（每轮跑前快照 `gen`、返回后锁下复检 `gen` 是否变）
经**世代代检**消除丢唤醒。故模板只需维持一条不变式：**`navail` 每 +1 都伴随一次 `run<g_dp>`**
（`wn_push` 已满足；`wn_done` 重入队那处由当前正在循环的 worker 自然接手）——再无手搓
「在跑计数 + 补投」逻辑。详见 [builtins/mt.md](../../builtins/mt.md) §9.7。

## 框架自省（编译期烘焙，运行时 O(1) 查表）

图是静态声明的，编译器在编译期把每个节点的结构度量算成常量烘焙进二进制，运行时**零图遍历**：

| 方法 | 含义 | 框架用途 |
|------|------|----------|
| `depth()` | 拓扑层级（源=0） | drain 反向遍历的天然次序（最深先行） |
| `critical()` / `slack()` | 是否关键路径 / 松弛量 | 定位吞吐瓶颈、优先优化 |
| `batch()` | 并行波次 | 同波次无依赖 → 可派线程池 |
| `checkpoint()` / `reach()` | 支配咽喉 / 影响范围 | 缓存边界 / 脏标记波及面 |

## 扩展

详见 `workflow.sc` 文末「扩展指南」，要点：

1. **加一处理节点**：`tok` + `var N_n: wnode` + 一个 `kernel` 纯函数 + `wn_init(&N_n, N, N_k, sink)`
   + `form N, (0:@), &N_n, node_drain` + 一条**单行前向路由**的 `dep…map`，度量由编译器自动重新烘焙。
2. **承载真实数据**（tensor/image）：环形缓冲存堆缓冲句柄而非 `i8`，kernel 签名随之改。
3. **并行分支 + 汇合（join）**：fork 处加多条边；join 节点按**帧序 id**配对多路输入，
   drain 从各 sink 分别 `back`。
4. **背压**：队满改阻塞或反压上游。
5. **细粒度锁**：单把 `flow.mu` 可拆为每节点一锁，提升独立子图并行度。

依赖 `mt` 模块（`inc mt.sc`：mutex/cond/barrier/thread/pool），见 `builtins/mt/mt.sc`
与 `syntax.md` §21（tok 体系）。
