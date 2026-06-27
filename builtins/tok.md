
# 分布式 token `tok`：观念量、依赖演变与 form 形成

> 以字符串 id 唯一标识的**共享量**（distributed token / 观念 notion）。值类型擦除为 `@`
> （裸自动指针，自描述胖指针，携 `tar`/`own`/`dtor`），在「设值即广播、依赖即重算」的声明式
> 语义下跨函数、跨模块（限同进程）自动传播。

- 语法层：关键字 `tok` / `dep` / `form` + 上下文门词 `all` / `any`，类型为 `token&`（op 层声明）。
- 运行时层：`builtins/tok/tok_impl.c` 内的 `token_bind` / `token_get` / `token_set` / `token_pulse` / `token_form` /
  `token_depend`，经 op→tok 隐式依赖随工程**始终链接**（无需 `inc`）。
- 编译层：声明降解为隐藏的 combine / follow 函数 + 注册调用，注入到 `main` / 模块 `init` 序章。

---

## 0. 设计目标与边界

1. **观念即共享量**：同一个字符串 id 在任何位置 `tok x: "id"` 引用到的是**同一个** token。
   声明是「create-or-get」——首次创建、后续幂等返回（adt 哈希 dict 按字符串 id intern）。
2. **设值即传播（唯变更才传播）**：`x.set(v)` 不只是写值，还会触发挂在该 token 上的全部依赖
   关系（`dep`）重算。但**仅当新值 ≠ 原值**时才落值并传播——新值与原值相等则静默丢弃，`follow`
   不再跑（对齐 c_prototype `C_input` 的 `C_equal` 短路）。form 候选的「新值」指 combine 合成
   结果（如取峰：`set` 较小值→合成不变→抑制）；enforce 的「新值」即输入值。`tok_modified()`
   返回的 **modified 哨兵**与任何值都「不相等」——combine 体内 `return tok_modified()` 即可
   在合成结果不变时**强制刷新**传播（对齐 `C_modified`）。调用点一侧则用 `t.pulse(v, tag)`
   脉冲设值绕过抑制（拉取流水线/迭代：每次 `set` 皆事件，同值也不可丢）——见 §1.4。
3. **两种身份（按有无 combine）**：
   - **form 候选**——带 combine 回调，输入要先经回调「合成」再落值（去抖、取峰、累加…）。
   - **enforce（纯从）**——无 combine，`set` 直接赋值。
   两者都须经 `form` 语句**激活**方就绪：共享量需「主的启动点」，未 `form` 的 token 不就绪，
   其 `set` 仅入挂起队列、暂不落值/传播，待该 token 被 `form` 后回放。bind 仅取共享壳（未就绪）。
4. **声明式依赖**：`dep all/any:` 声明 token 间的依赖关系，任一上游变更即唤起 `follow` 回调，
   由**与门（all）/ 或门（any）**决定触发时机；`follow` 返回值动态切换下次门逻辑。
5. **边界（v2）**：
   - **模块域静态对象**，不支持 `@` 导出（注册延迟到模块 `init` / `main` 序章）。
   - 值类型**擦除为 `@`**（`sc_afat`），读出后由调用点 `(e: T@)` 还原；裸指针/整数经 `(e: @)`
     装箱为非托管裸 `@`。`@` 携 `dtor`，故下游还原类型安全、值自描述。
   - id 为**进程内**字符串键（adt 哈希 dict，O(1) intern）；`'/'` 前缀 token 进入多线程
     模式，**细粒度无锁**同步：值用每-token **序列锁（seqlock）**（读无锁乐观重试、写者经
     `seq` 自旋独占），依赖门用每-dep **自旋锁**（仅护门计数），`follow` 回调一律在释放
     所有锁后调用——任一线程任一时刻零持锁跑用户码，根除跨 token 锁序死锁，独立 token 子图
     真并行。MT 依赖会将其全部成员一并升格 MT。非 MT token 零原子开销。
     > **约束**：`combine` 须**纯**（仅 `base`/`input`→`value`，不得 `set`/`get` 其他 token，
     > 它在写者独占下运行）；跨 token 副作用一律写在 `follow`（锁外运行）。`dep` 注册在模块
     > `init` 单线程完成（建图先于任何并发 fire）。
   - token 持值为结构拷贝（保留 `p`/`tar`/`own`/`dtor`），不额外 retain/release。

---

## 1. 语法面

### 1.1 `tok` —— 声明 token 句柄

```sc
# enforce 纯从：set 直接赋值
tok alert: "sensor.alert"

# form 形成主：紧随缩进 combine 体（取较大者 = 峰值保持 / 去抖）
tok level: "sensor.level"
    var b: i8 = (this->base: i8)    # 当前值（@ → i8）
    var i: i8 = (this->input: i8)   # 本次输入
    var m: i8 = b
    if i > b
        m = i
    return (m: @)                  # 合成结果装箱回 @
```

- 形态：`tok <句柄名>: "<字符串 id>"`，**紧随的缩进块**（无引导冒号）即 **combine 体**。
- combine 体唯一上下文形参 `this: __sctok_in&`，成员皆 `@`：
  | 成员 | 含义 |
  |------|------|
  | `this->base`   | 当前值 |
  | `this->input`  | 本次输入 |
  | `this->sender` | 输入者（恒空 `@`，预留） |
  | `this->tag`    | `set` 随附标签（`i4`，体内分流用） |
- 返回 `@`（新值）。无 combine 体 = enforce 纯从。
- **变更检测**：合成结果与原值相等时 `set` 静默丢弃、不传播。如需在「值不变」时仍强制刷新，
  combine 体内 `return tok_modified()`（modified 哨兵与任何值都不相等，强制传播一次）。

### 1.2 `form` —— 形成 / 灌初值

```sc
form level, (0: @)          # 初始化 form token，灌初值 0（@）并升格为 form 主
```

- 形态：`form <token 句柄>, <初值>`（语言内置语句关键字，与 `run` / `done` 同级；初值为 `@`）。
- 语义：给 token 灌初值、**升格为就绪主**（共享量的启动点），随后回放 form 前挂起的 `set`。
  对**任何**自有 token 均适用（不限 combine 形成主）：未 `form` 的 token 不就绪，其 `set` 只入挂起
  队列、不传播；`form` 后方落值并触发依赖。enforce（无 combine）`form` 即直赋型主。

### 1.3 `dep` + `all` / `any` —— 依赖关系

```sc
# 或门：温度 temp 或 烟雾 smoke 任一变更即评估，超阈值点亮 alert（行内多依赖，逗号分隔）
dep any: t:"sensor.temp", s:"sensor.smoke"
    var hot:   bool = (t->get(): i8) > 100   # t = this->toks[0]（局部名糖）
    var smoky: bool = (s->get(): i8) > 0     # s = this->toks[1]
    if hot or smoky
        alert->set((1: @), 0)
    return false                 # 返回门逻辑：false=下次仍或门，true=改与门

# 与门：温度 temp 与 湿度 humid 都更新过才一并评估（块形态，每行一项）
dep all:
    t:"sensor.temp"
    h:"sensor.humid"
    -                            # '-' 分隔依赖项列表与 follow 体
    var idx: bool = (t->get(): i8) > (h->get(): i8)
    comfort->set((idx: @), 0)
    return true                  # 维持与门：下轮仍需两者都变更
```

- 门词：`all`（与门）/ `any`（或门），为**上下文标识**（非保留字）。
- 依赖项：`<局部名>:"<id>"`，可**行内**（逗号分隔）或**块形态**（每行一项，`-` 单独成行分隔体）。
  单依赖时 `all` / `any` 等价（只有一个上游，触发时机相同）；多依赖才显出门逻辑差异：
  **或门**任一上游变更即触发，**与门**须全部上游各变更过一次才触发（之后重新计数）。
- follow 体唯一上下文形参 `this: __scdep_in&`：`this->toks`（依赖项数组 `token&&`）/
  `this->count`（个数）/ `this->active`（触发动作码）。各局部名 `a:"id"` 由编译器注入糖
  `var a: token& = this->toks[i]`，可直接以局部名引用第 `i` 个依赖项。返回 `bool` = 下次门逻辑
  （`true`=与门 / `false`=或门）。

### 1.3.1 `map` —— 依赖图（源 → 目标）

`dep` 的 `map` 关键字在「源」与「目标」之间架起一条**显式有向边**，令一组 token 构成
**依赖关系图**（工作流 / 流水线 / 神经网络等）：

```sc
# 块形态：源 在前，'map' 单独成行作分隔，目标 在后，'-' 再分隔 follow 体
dep any:
    r:"wf.raw"               # 源（触发/上游）
    map
    c:"wf.clean"             # 目标（输出/下游）
    -
    var v: i8 = (r->get(): i8)
    if v < 0
        v = 0
    c->set((v: @), 0)        # follow 写入目标
    return false

# 行内形态：源... map 目标...
dep any: c:"wf.clean" map o:"wf.report"
    o->set(((c->get(): i8) * 2: @), 0)
    return false
```

- `map` 为**上下文标识**（非保留字，同 `all` / `any`）。
- **源**（`map` 之前）：触发/上游——门按其状态开合，其变更/就绪触发本 `dep`；编译器把它们反挂到
  各源 token 的 `deps[]`。
- **目标**（`map` 之后）：输出/下游——由 follow 体写入，**自身不触发本 dep**（杜绝自环回灌）。
- 源与目标**都**注入局部名糖：`this->toks` 排布为 `源(nsrc) ++ 目标(ntgt)`，follow 体内两者均可
  直接以局部名引用（`token&`）。
- **编译期环检测（语义分析）**：全单元所有 `map` 边汇成有向图，编译器三色 DFS 检测有向环——
  出现环（含自环 `a→a`）即报错，提示环路径，要求依赖图为 **DAG**（有向无环图）。普通 `dep`
  （无 `map`）不贡献图边。

### 1.3.2 烘焙常量（baked constants）：依赖图度量

`map` 把一组 token 连成显式 DAG 后，编译器在**编译期**对这张图跑一组有向图算法（环检测 /
拓扑 / 最长路 / 关键路径 / 传递闭包 / 支配树 / Tarjan SCC），把每个 token 的图属性算成
**字面量常量**烘焙进生成的 C（注册时 `token_set_*` 写入句柄），运行时只是 **O(1) 查表**——
零图遍历。这套「编译期算贵的、运行时只查表」的做法类比游戏的 **lightmap / navmesh 烘焙**：
图是静态的（`dep…map` 声明期即定），故所有结构度量都能预计算。

> **架构铁律**：图算法本体住在 `builtins/tok/graph`（`graph.h` 契约 + `graph_impl.c` 实现，
> 编译进 `scc`），编译器只做「AST 依赖边 → 整数图」的适配与「结果 → C 字面量」的烘焙；运行时
> （`tok_impl.c`）只持烘焙好的常量并以 getter 返回。`graph.h` 是 scc（生产者）与 tok 运行时
> （消费者）共享的同一份契约。

token 句柄上由此暴露一组 **O(1) 查询方法**（均为烘焙常量，`@def token` 协议见 `op.sc`）：

| 方法 | 含义 | 图算法 |
|------|------|--------|
| `t.depth()`        | 依赖图深度（源=0，多前驱取最长路 = 第几级 / 第几层） | 最长路径分层 |
| `t.critical()`     | 是否在**关键路径**（最长链）上——加长它即拖慢整条流水线（瓶颈） | 正向最早 + 反向最长 |
| `t.slack()`        | **松弛余量**：可深多少跳而不拖慢全局（0=关键点；旁支有余量） | 同上 |
| `t.fanin()`        | **扇入度**：被多少上游 map 依赖（高=聚合汇 / 瓶颈消费者） | 入边计数 |
| `t.fanout()`       | **扇出度**：驱动多少下游 map 目标（高=枢纽 / 广播源） | 出边计数 |
| `t.reach()`        | **可达下游数**：变更本 token 后须重算的下游总数（脏标记影响范围 / 失效爆炸半径） | 传递闭包（位集） |
| `t.batch()`        | **拓扑波次编号**（= `depth`）：同波 token 两两无路径（反链），可并行触发（接 MT） | 最长路径分层 |
| `t.batch_width()`  | 本波次**并行宽度**：与本 token 同深度、可并行的 token 数 | 同层计数 |
| `t.checkpoint()`   | 是否为**支配咽喉 / 缓存边界**——流经其下游全部数据的咽喉，在此缓存可覆盖整个支配子树 | 支配树（CHK idom） |
| `t.dom_size()`     | **支配子树规模**：本检查点缓存可覆盖的下游 token 数 | 同上 |

```sc
# 媒体流水线：主链 fetch→decode→render→output（4 级，全关键），音频旁支 fetch→audio→output
dep all: s:"pipe.fetch"  map t:"pipe.decode"
    return false
# …（略）…
dep all: s:"pipe.audio"  map t:"pipe.output"
    return false

fnc main: i4
    printf("%d %d %d\n", fetch->depth(),  fetch->critical(),  fetch->slack())   # 0 1 0（关键）
    printf("%d %d %d\n", audio->depth(),  audio->critical(),  audio->slack())   # 1 0 1（旁支有余量）
    return 0
```

> 这些方法**不引入新语法**——只是 `@def token` 上的普通查询方法（同 `get`/`set`），`emit-sc`
> 往返时仍只回写普通 `dep…map` + 方法调用。完整示例见 `examples/feature51.sc`（关键路径）/
> `feature52.sc`（扇入扇出）/ `feature53.sc`（可达性）/ `feature54.sc`（拓扑分批）/
> `feature55.sc`（支配树）。

### 1.3.3 `loop` —— 受控反馈环（dep loop）

`map` 强制 DAG（环即报错），但有些问题**天生有环**：迭代求解（牛顿法、不动点）、控制反馈、
互递归收敛。`loop` 是与 `map` **正交**的边拓扑——占据 `map` 的位置（块式 `loop` 单独成行 /
行内 `… loop t:"id"`），声明一条**反馈边**，**豁免环检测**，编译期对全部 `loop` 边跑 Tarjan
求**强连通分量（SCC）**，把每个 token 的「反馈簇编号 + 簇大小」烘焙为常量：

```sc
# 牛顿法整数平方根：a⇄b 双向反馈簇（a→b, b→a），迭代收敛 a=b=√N
dep all: s:"nt.a" loop t:"nt.b"
    var n: i8 = 100
    b->set((n / (a->get(): i8): @), 0)     # b = N / a
    return false
dep all: s:"nt.b" loop t:"nt.a"
    var x: i8 = (a->get(): i8)
    a->set(((x + (b->get(): i8)) / 2: @), 0)   # a = (a + b) / 2
    return false
```

| 维度 | `map`（DAG 边） | `loop`（反馈边） |
|------|----------------|------------------|
| 环 | 编译期报错（强制 DAG） | 豁免；编译期 Tarjan 缩点 |
| 触发 | 源变更**自动级联** | 源**不自动级联**（杜绝无限环），仅经显式 `t.loop_run(max)` 按簇驱动 |
| 烘焙 | depth / critical / fanin… | `scc()` 簇编号 + `scc_size()` 簇大小 |

- **门逻辑正交**：`all`/`any`（与门/或门）与边拓扑（none / `map` / `loop`）相互独立——
  `loop` 不改门语义，`dep all: … loop …` 仍是与门，只是边是反馈边。
- 运行时方法：`t.scc()`（反馈簇编号，非反馈=0）/ `t.scc_size()`（簇大小，>1 或含自环=反馈）/
  `t.loop_run(max)`（驱动 `t` 所在簇迭代至多 `max` 轮，返回实际轮数；非反馈簇不迭代）。
- 完整示例见 `examples/feature50.sc`（2 节点反馈簇牛顿法，从 a=100 收敛到 a=b=10=√100）。

### 1.3.4 `back` —— 反向遍历（反向传播骨架）

`back` 是硬关键字（语句位）：自一个输出 token 出发，沿反向邻接（`producers[]`，由 `map` 目标
反挂）**按反拓扑序**（深者先行）逐个以 `acting=TOK_BACK` 唤起上游 `dep` 的 `follow`——follow
体内判 `this->active == TOK_BACK` 即走**反向计算**（读目标、写源；如梯度反传）。

```sc
back loss, (1: @)      # 自 loss 反向遍历，先灌梯度种子 1（可省 seed）
```

- 形态：`back <token 句柄>[, <种子>]`。种子非空则先灌入起点（不触发前向级联），随后反向 BFS。
- 编译期已保证 `map` 图为 DAG（环检测），反向遍历必终止。降解为 `token_back(t, seed, 0)`。
- **两种模式（自动分派，互斥）**：`token_back` 先扫可达节点——
  - **边反向**（无 `exec` 节点）：按反拓扑序对各上游 `dep` 以 `acting=TOK_BACK` 唤起 `follow`，
    follow 体判 `this->active == TOK_BACK` 走反向计算（读目标、写源；如梯度反传，见 `feature49`）。
  - **节点 drain**（图中存在 `exec` 节点）：按节点 `depth` 降序对各注册节点唤起
    `exec(t, t->ctx)`，节点自己出队跑 kernel——`dep` 只管前向路由，处理归节点（见 §1.3.5）。
- **提前中止（break）**：钩子（follow / exec）返回**非 0** 即停止本轮反向遍历（返回 0 = 走完全程，
  向后兼容）。供「drain」拉取式协作层用：worker 自 sink `back`，最深可认领节点认领并处理一帧后返回
  非 0 中止扫描，worker 再发起下一轮——天然「最近未处理者先行 / 下游优先排空」。

#### drain 驱动 ≠ form input 驱动（仍须 form 启动点，但无须 combine）

`back` / `TOK_BACK` 是一条**独立于前向推送的拉取驱动**通道，**与 combine 合成正交**（但依然需要 form 就绪）：

- **前向推送驱动**有两种：`set` 变更（`TOK_ALL_CHANGED` / 变更项下标）与 form 就绪
  （`TOK_ALL_READY` / `TOK_ANY_READY`）。两者都要求**目标已 form**——共享量唯被 `form`
  激活（主的启动点）后方就绪，`set` 方落值并传播；未 form 则 `set` 仅入挂起队列、不传播。
- **drain 拉取驱动**是第三种：计算由 worker 主动 `back` 触发，而非由上游 `set` / `form` 推动；
  follow 的前向分支只把上游数据**入队**（截断同步级联）、反向分支（`TOK_BACK`）才认领并处理。
- 正交点在 **combine**，不在 **form**：纯 drain 流水线**无需写 combine 体**（每帧独立流过、不跨帧
  合并），但**仍须对每个传播节点 `form`**——否则节点 token 不就绪，路由器 `set` 落不进、流水线停摆。
  combine 仅当某节点需**跨帧合并**（去抖 / 峰值保持 / 累加）时才引入。
  完整脚手架见 `templates/workflow-graph/workflow.sc`（缓冲队列 + 线程池 + drain，各节点 form 无 combine）。


### 1.3.5 节点侧车 `ctx` + 节点钩子 `exec`（经 `form` 绑定）

`dep` 是**边**（关系路由），`tok` 是**点**（函数实现）。`form` 是节点配置的**唯一入口**——一处灌初值、
升格 form 主、绑侧车 `ctx`、挂处理钩子 `exec`，使 `dep` 体退化为纯路由（不硬编码节点变量、不含算子）：

| 形态 | 降解 | 语义 |
|------|------|------|
| `form t, v, &n` | `token_form(t, v, 0, &n, 0)` | 绑定节点私有上下文（**侧车**）到 token；后续 `t.ctx()` 取回 |
| `form t, v, &n, exec` | `token_form(t, v, 0, &n, exec)` | 同时挂**节点处理钩子** `exec`（拉取/推送两模式共用，见下） |
| `t.ctx()` | `token_ctx(t)` | 取本 token 的侧车（`&`；未绑定=空 `&`）——节点处理态的通用载体 |

- 钩子类型：`@fnc token_exec_fn: i4, t: token&, ctx: &`。**单一 `exec` 统一两模式**，模式由**谁驱动**决定（非钩子属性）：
  - **拉取**（`back` 驱动）：`back` 节点模式下按节点 `depth` 降序唤起 `exec(t, t.ctx())`，节点出队跑 kernel、`t.set` 产出；返非 0=认领并处理一节点、中止本轮扫描。
  - **推送**（`set` 驱动）：`set` 值变更落定后于**锁外**、向下游传播前唤起 `exec(t, t.ctx())`（副作用/观察：产出、统计、日志、外部推送）。
- **职责分工**：`combine` 须纯（锁内只算值，不得 set/get 其它 token）；`dep` 只管前向路由；处理/副作用归 `exec`（锁外，MT 安全）。一个节点只会被其一种模式驱动（取决于模板用 `back` 还是 `set`）。
- **两类模板对照**：
  - 拉取式（`templates/workflow-graph/`）：`dep` 经 `t.ctx()` 把帧**入队**到下游节点；worker `back` →
    `exec` 出队跑 kernel。异步缓冲、多线程。
  - 推送式（`templates/push-reactive/`）：`dep` 把上游值**前推** `t.set(s.get())` 触发下游 `combine`
    同步重算；`exec` 做节点观察/产出。同步级联、单线程。变更检测在此是**反应式记忆化**
    （同值截断级联），与拉取式「同值丢帧需 `pulse` 逃生」相反（见 §1.4 `pulse`）。


### 1.4 取值 / 设值

token 句柄是 `token&`（指针），用**箭头** `->` 调方法：

```sc
level->set((150: @), 0)         # 设值（@ 值 + i4 标签）→ 仅值变更才落值并触发依赖级联（记忆化/去抖）
var lv: i8 = (level->get(): i8)  # 取值（返回 @，调用点 (e: T@) / (e: i8) 还原）
level->pulse((150: @), 0)        # 脉冲设值：绕过相等抑制，即便同值也强制传播
```

- `set` **记忆化**：唯「新值≠原值」才落值传播（combine 取合成结果、enforce 取输入值；对齐 c_prototype
  的 `C_input`）——推送/反应式里这是**去抖/级联自截断**（特性）。
- `pulse` **脉冲**：绕过相等抑制，同值也落值并强制传播。用于**拉取流水线 / 迭代驱动**（每次 `set` 皆事件、
  相同值也不可丢）：如 `templates/workflow-graph/`（源帧入队 ping）、`templates/dnn-framework/`（训练循环
  每 epoch 喂同一输入 `x` 须重跑前向）。降解为 `token_pulse(t, v, tag)`。
- 另有 `tok_modified()`（combine 体内 `return tok_modified()`）强制本次合成传播——区别：`pulse` 在**调用点**
  对任意 token 强制（含 enforce/map），`tok_modified` 在 **combine 体内**强制（仅 form 候选）。

---

## 2. 降解（desugaring）

编译器把声明式语法降解为普通函数 + 注册调用：

| 源语法 | 降解产物 |
|--------|----------|
| `tok t: "id"` + 紧随缩进体 | 隐藏 `__sctok_<id>_combine`（`tokHidden` 函数，单形参 `this: __sctok_in&`，返回 `@`）+ `var t: token&`（`isTok`，记 `tokId` / `tokFn`） |
| `tok t: "id"`（无体） | 仅 `var t: token&`（`isTok`，`tokFn` 空 = enforce） |
| `dep all/any: …` + 体 | 隐藏 `__scdep_<N>_follow`（`tokHidden`，单形参 `this: __scdep_in&`，返回 `bool`；体首注入各依赖项局部名糖 `var a: token& = this->toks[i]`）+ `DepD`（记 `depAll` / `depItems` / `tokFn`） |
| `dep …: 源 map 目标` + 体 | 同上，另记 `depTargets`；源与目标均注入局部名糖（`toks` = 源++目标）。注册降解为 `token_depend_map(_deps={源++目标}, nsrc, ntgt, all, tramp, NULL)`（仅源反挂触发，目标随 follow 传入；编译期对 map 边查有向环），并烘焙 depth/critical/fanin/reach/batch/dom 等图度量为 `token_set_*` 常量 |
| `dep …: 源 loop 目标` + 体 | 同上，但记 `depLoop`；豁免环检测，注册降解为 `token_depend_loop(...)`（源不反挂）；编译期对 loop 边跑 Tarjan，烘焙 `token_set_scc(t, 簇编号, 簇大小)` |
| `form t, v[, ctx[, exec]]` | `FormS` 语句 → `token_form(t, v, 0, ctx, exec)`（`v` 为 `@`；可选第三参 `ctx`=`&n` 侧车、第四参 `exec`=节点处理钩子，缺省传 `NULL`/`0`） |
| `back t[, seed]` | `BackS` 语句 → `token_back(t, seed, 0)`（`seed` 省略时传空 `@`） |
| `t->set(v, tag)` / `t->pulse(v, tag)` / `t->get()` | 方法分派 → `token_set(t, v, tag)`（记忆化）/ `token_pulse(t, v, tag)`（脉冲强制传播）/ `token_get(t)` |
| `t->depth()` / `t->scc()` / `t->loop_run(n)` … | 方法分派 → `token_depth(t)` / `token_scc(t)` / `token_loop_run(t, n)` …（O(1) 查烘焙常量） |

注册代码注入到 `main` 序章（或模块 `init` / 测试 runner）：

```c
/* tok 绑定 */
temp  = token_bind("sensor.temp",  NULL);
smoke = token_bind("sensor.smoke", NULL);
alert = token_bind("sensor.alert", NULL);
/* dep 注册（依赖项数组 + 门逻辑 all=0(或门) + follow 蹦床） */
{ token *_deps0[] = { temp, smoke }; token_depend(_deps0, 2, 0, __scdep_0_tramp, NULL); }
```

follow / combine 的 C ABI 签名以上下文结构传递；combine 直接收 `__sctok_in*`，follow 由编译器生成的
**蹦床**把运行时通用签名打包成 `__scdep_in&`：

```c
static int __scdep_0_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;
    return (int)__scdep_0_follow(&_self);
}
```

---

## 3. 运行时（builtins/tok/tok_impl.c）

| 函数 | 作用 |
|------|------|
| `token *token_bind(const char *id, token_combine cb)` | create-or-get：按 id intern（adt 哈希 dict）；`cb` 非空则挂为 form 主，幂等补挂 |
| `sc_afat token_get(token *t)` | 取当前值（`@`） |
| `void token_set(token *t, sc_afat v, int32_t tag)` | 有 combine 则 `value = cb(this{base,input:v,sender,tag})`，否则直接赋值；触发变更依赖 |
| `void token_form(token *t, sc_afat v, int32_t tag, void *ctx)` | 灌初值、升格 form 主、回放挂起、触发就绪依赖；`ctx` 非空则绑定为节点私有上下文（侧车） |
| `void token_depend(token **ts, int n, int all, token_follow cb, void *ctx)` | 注册依赖：拷贝依赖项数组，反挂到各 token 的 `deps[]` |
| `void token_depend_map(token **ts, int nsrc, int ntgt, int all, token_follow cb, void *ctx)` | 注册 map 依赖（`ts` = 源 `nsrc` ++ 目标 `ntgt`）：门/武装/反挂仅覆盖源，目标只随 `follow` 传入；二者共用 `tok_depend_impl(ts, nsrc, nsrc+ntgt, …)` |
| `void token_depend_loop(token **ts, int nsrc, int ntgt, int all, token_follow cb, void *ctx)` | 注册 loop 反馈依赖：源**不反挂**（杜绝自动级联无限环），登记全局 loop 表，由 `token_loop_run` 按 SCC 簇驱动；目标仍建反向邻接 |
| `void token_set_depth/crit/degree/reach/batch/dom/scc(...)` | 烘焙写入：编译期算好的图度量（深度 / 关键路径+松弛 / 扇入扇出 / 可达数 / 波次宽度 / 支配检查点+子树 / SCC 簇）以常量写入句柄（注册时调用） |
| `int token_depth/critical/slack/fanin/fanout/reach/batch/batch_width/checkpoint/dom_size/scc/scc_size(token *t)` | O(1) 查询：读烘焙好的图度量常量（无图遍历），对应 §1.3.2 的 `t.*()` 方法 |
| `int token_loop_run(token *t, int max)` | 驱动 `t` 所在 SCC 反馈簇迭代至多 `max` 轮（`acting=TOK_LOOP`），返回实际轮数；非反馈簇（簇大小≤1）不迭代 |
| `void token_back(token *t, sc_afat seed, int32_t tag)` | `back t[, seed]`：自 `t` 沿反向邻接按反拓扑序遍历；若图中存在 `exec` 节点则进**节点 drain 模式**（按 depth 降序对各注册节点唤起 `exec(t, t->ctx)`，返非 0 即 break），否则走边反向 `follow`（`acting=TOK_BACK`）；`seed` 非空先灌入 `t` |
| `void token_form(token *t, sc_afat v, int32_t tag, void *ctx, token_exec exec)` | `form t, v[, ctx[, exec]]`：灌初值并升格；可同时绑定节点侧车 `ctx` 与统一节点钩子 `exec` |
| `void token_set(token *t, sc_afat v, int32_t tag)` / `void token_pulse(token *t, sc_afat v, int32_t tag)` | `t.set(v, tag)`：同值抑制的记忆化设值；`t.pulse(v, tag)`：绕过相等抑制、同值也强制传播 |
| `void *token_ctx(token *t)` | `t.ctx()`：取节点私有上下文（`form t,v,&n[,exec]` 绑定的侧车；未绑定=`NULL`） |

上下文结构（C ABI，见 `tok.h`）：`__sctok_in { sc_afat sender, base, input; int32_t tag; }`、
`__scdep_in { token **toks; int32_t count; int32_t active; void *ctx; }`（`ctx`=注册时透传的关系私有边状态）。
节点钩子类型：`token_drain`/`token_apply` 均为 `int (*)(token *t, void *ctx)`——前者 `back` 拉取唤起（返非 0=break），
后者 `set` 变更后锁外唤起（节点级副作用/观察）。

### 3.1 门逻辑与触发

`token_set` / `token_form` → `tok_fire`：遍历该 token 反挂的每条依赖关系（`tok_dep`），按门逻辑处理：

- **与门（all）**：每个依赖项本轮首次事件即「武装(arm)」并令 `remain--`；`remain` 归零（全部
  依赖项各到位一次）才以 `acting=TOK_ALL_CHANGED`（set）/ `TOK_ALL_READY`（form）唤起 `follow`，
  触发后重置 `armed` / `remain` 进入下一轮。
- **或门（any）**：任一事件即唤起 `follow`（`acting=` 变更项下标，或 form 时 `TOK_ANY_READY`）。
- `follow` 返回值更新该关系的门逻辑（`true`→与门 / `false`→或门），动态切换；切到与门时重置计数。

`token_depend` 注册时即结算一次：已就绪（form / enforce）的依赖项预先武装，满足门逻辑则立即以
`TOK_ALL_READY` / `TOK_ANY_READY` 触发 follow（对齐"注册即就绪"语义）。

动作码：`TOK_ALL_READY (-2)`（与门 form 就绪）、`TOK_ALL_CHANGED (-3)`（与门 set 变更）、
`TOK_ANY_READY (-1)`（或门就绪 / 退化）、`idx >= 0`（或门变更项下标）、`TOK_BACK (-4)`
（`back` 反向遍历：follow 体走反向计算）、`TOK_LOOP (-5)`（`loop_run` 反馈簇迭代）。依赖表
`deps` 与挂起队列 `pending` 均为动态增长数组（无定长上限）。

> 后续待扩：关系索引访问、引用计数 / 生命周期回收、句柄与依赖记录的回收（当前为进程生命周期
> 静态对象）。v2 已含 adt 哈希 dict token 查找、form 就绪（`TOK_ALL_READY` / `TOK_ANY_READY`）
> 触发、`'/'` 前缀多线程细粒度无锁（每-token seqlock 读 + 每-dep 自旋门 + follow 锁外）。

---

## 4. 完整示例

见 `examples/feature47.sc`：`level`（form / 峰值保持）+ `alert`（enforce）+ `dep any`
（超阈值点亮 alert）。运行输出：

```
after 50:  level=50 alert=0
after 150: level=150 alert=1     # 超阈值，依赖触发
after 30:  level=150             # 30 < 峰值 150，combine 取较大者 → 仍 150
```

---

## 5. 实现落点

| 文件 | 内容 |
|------|------|
| `compiler/src/lexer.{h,cpp}` | 关键字 `tok` / `dep` / `form`（`KwTok` / `KwDep` / `KwForm`） |
| `compiler/src/ast.h` | `Stmt::FormS`、`Decl::DepD`、`Decl` 的 `isTok` / `tokId` / `tokFn` / `tokHidden` / `depAll` / `depItems` |
| `compiler/src/parser.cpp` | `parseTok` / `parseDep` / `FormS`；id 去引号、combine / follow 合成 |
| `compiler/src/semantic.cpp` | `FormS` 类型检查（expr 为 token&、初值任意） |
| `compiler/src/codegen_c.cpp` | 收集 `tokBinds` / `depRegs`，注入注册、生成蹦床与前向声明 |
| `compiler/src/codegen_sc.cpp` | `tok` / `dep` / `form` 反生成；跳过 `tokHidden` 函数 |
| `compiler/src/ast_json.cpp` | `FormS` / `DepD` / `tok` 节点 |
| `builtins/op.sc` | `@def token`（句柄类型 + `get` / `set` 方法协议，默认导入，不生成代码） |
| `builtins/op.h` | `#include "tok/tok.h"`（令 `token` / `token_*` 随 op.h 默认带入每个 C 单元） |
| `builtins/tok/tok.sc` | tok 运行时载体模块（经 op→tok 隐式依赖携带 `tok_impl.c`） |
| `builtins/tok/tok.h` | `token` C 类型与 `token_*` 原型（C ABI 契约） |
| `builtins/tok/tok_impl.c` | `token_*` 运行时（经拼接机制始终链接） |
| `compiler/src/main.cpp` | op→tok 隐式依赖（使 tok 单元恒入图、`tok_impl.c` 恒链接） |

---

## 6. 状态与后续

**已跑通（v2）**：声明 / form / 依赖触发、combine（form 主）/ enforce（纯从）、and / or 门、
follow 动态切门、form 就绪触发（`TOK_ALL_READY` / `TOK_ANY_READY`）、form 前挂起回放、
`'/'` 前缀多线程**细粒度无锁**（每-token seqlock 读 + 每-dep 自旋门 + follow 锁外，
TSan 干净）、adt 哈希 dict intern、`@` 值贯通（`this` 上下文结构 + 标签）、
`dep…map` 显式依赖图（编译期环检测）、**依赖图度量烘焙常量**（depth / critical+slack /
fanin+fanout / reach / batch+width / dominator checkpoint+size，全 O(1) 查表）、
`dep…loop` 受控反馈环（Tarjan SCC 缩点 + `loop_run` 迭代）、`back` 反向遍历骨架、
emit-c / emit-sc 双向往返、回归快照。

**未实现（后续）**：关系索引访问、跨进程 id 空间、引用计数 / 生命周期回收、句柄与依赖记录回收、
加权关键路径（`tok cost N`）、波次并行调度接 MT 线程池。