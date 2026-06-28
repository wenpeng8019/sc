# 跨线程协同 `mt`：PORT 单收件箱、sync 铁律与延迟应答 `session`

> sc 多线程的协同骨架。本文是**定稿规格 + 机制流程 + 全场景示例**，
> 既面向用户讲清怎么用，也作为编译器/运行时实现的对照基准（避免后续遗忘）。
> 配套源码：`builtins/mt/mt.sc`（事实源，类型与方法声明）、`builtins/mt/mt_impl.c`（默认实现）、
> `builtins/op.sc` + `builtins/op.h` + `builtins/op_impl.c`（thread/run/pool/queue/future/session 内核协议）。

---

## 0. 设计目标与边界

**目标**：在**不引入** actor 运行时、**不引入** GC、**不引入**编译器特殊知识的前提下，让 sc 能：

1. 用一条 `run` 语句创建线程（detach / joinable / 入池三态），目标必须是 `rpc` 调用；
2. 用消息队列 `queue` 做跨线程 rpc——同步 `sync`（阻塞取返回值）、异步 `async`（拿 `promise` 后取）、
   投递 `post`（fire-and-forget）；
3. 让阻塞型 `sync` 在**循环互锁**时自动本地替代执行，而非死锁；
4. 让一次 `sync` 的应答可以**延迟**到 rpc 体返回之后（甚至另一线程）才兑现——`session` 延迟应答，
   与单线程 `future`（`async→future→done`）完全对称。

**核心承诺**——「铁律」（见 §5）：

> **一次 sync 一旦开始执行（PULLING），调用方必死等到 DONE/CLOSED，绝不中途放弃。**
> 由此调用方栈帧在执行期间始终有效，执行方可直接读写调用方栈上的参数与返回槽，
> **无需堆影子会话（shadow session），无需 pending 列表**。

**明确不做**（设计边界，不是 bug，写进文档当承诺）：

- **不做 work-stealing / 优先级抢占池**：默认池是 FIFO + n 个固定工作线程；其它策略另起 `*_pool(n)` 构造。
- **不做取消/超时杀死正在执行的 rpc**：`sync` 超时只在**尚未被 pull**（QUEUED）时干净撤回；
  **已开始执行**（PULLING）则铁律死等。
- **不自动断开延迟会话**：`session` 被领走后若责任线程始终不 `done`，调用方将永久阻塞——
  这是**程序责任**，等价于「`promise` 永不兑现 → `await` 永久挂起」（见 §7、§11）。
- **编译器零 emit mt 符号**：编译器只认识 `op.sc` 暴露的协议（vtable 函数指针）与少量 op 内核符号，
  对 `mutex/pool/queue/future/session` 一律经协议指针派发，绝不直接 emit `mt_*` 实现符号（见 §10）。

---

## 1. 核心模型：PORT 单收件箱

线程间不是「每队列一把锁、每队列一条等待链」，而是**每线程一个 PORT**——单一收件箱中枢。

- **port（每线程）**：线程局部 `TLS g_port`，首次需要时惰性堆分配。**它的地址即该线程的稳定唯一身份。**
  多个 `queue` 可以 attach 到同一 port，消息全部归并进 port 的**单一收件箱**，按到达顺序
  （同优先级 FIFO）排成单链——于是跨多个队列也能得到**全局时序**。
- **queue（消息队列）**：协议对象（vtable）。首次被某 port `pull` 时**惰性 attach** 到该 port
  （`consumer = port`），并把 attach 前积压的 `staging` 暂存消息整块插入 port 收件箱首部（优先处理历史）。
- **全局唯一互斥 `g_mutex`**：保护所有 port 收件箱 + queue 暂存 + `consumer/waiting` 关系图——
  **单锁消除一切锁序环**。各 port 的 `recv`（等来活）/`send`（等应答/替代）条件变量都在 `g_mutex` 上等待。

```
线程 A 的 port                          线程 B 的 port
┌───────────────────────┐              ┌───────────────────────┐
│ box: ready ──▶■─▶■─▶■  │ (按 prio)    │ box: ready ──▶■        │
│      delaying ─▶◇      │ (按 deadline)│      delaying          │
│ recv / send 条件变量    │              │ recv / send           │
│ waiting=本线程阻塞的队列 │              │ substituting=有人替我跑 │
│ attached: q1 ─▶ q2     │              │ attached: q3          │
└───────────────────────┘              └───────────────────────┘
        ▲ pull 取队头在本线程执行              ▲
   q1.post/sync ───────────────────────────────┘ 跨线程投递进对方 port box
```

**宿主三态**（`queue` 的 `host: pool&`）决定消息去哪执行：

| host 值 | 含义 | 消费方式 |
|---|---|---|
| `nil`（NULL） | 未绑定 / 延迟绑定 | 消息先入 `staging`，待首次 `pull` 时 attach 到 puller 的 port |
| `main`（哨兵 `(pool*)-1`） | 当前 / 主线程自行消费 | 用户手动跑 `pull` 循环 |
| `&pool`（真实池） | 线程池并发消费 | `post` 直接经 `pool->run` 转交池，不入 port、不 attach |

---

## 2. 类型系统

`mt.sc` 是唯一事实源：`@def` 给出纯数据布局（C ABI 契约），`fnc name::` 是无函数体的 extern 原型，
`@rpc` 仅声明（调用包装由编译器生成，`*_rpc` 在 C 侧实现）。

| 类型 | 归属 | 角色 | 关键方法 |
|---|---|---|---|
| `mutex` | mt.sc | 互斥锁 | `init/drop/lock/unlock/try_lock` |
| `cond` | mt.sc | 条件变量 | `init/drop/one/all/wait` |
| `barrier` | mt.sc | N 方汇合屏障 | `init(n)/drop/wait` |
| `thread` | op.sc 内核 | joinable 线程句柄 | `join` |
| `pool` | op.sc 内核协议 | 线程池接口（vtable，run 投递目标） | `run/join/drop`；mt 提供两种策略 `default_pool(n)`（常驻 FIFO）/ `drain_pool(n)`（按需自调度） |
| `queue` | op.sc 内核协议 | 消息队列接口（vtable） | `post/sync/async/pull/drop`；mt 提供 `default_queue(host)` |
| `future` | op.sc 内核 | 单线程异步句柄（`async→future→done`） | `await`；`done f, r` 兑现 |
| `promise` | op.sc 内核协议 | 跨线程异步句柄（`queue.async` 返回） | `ready/wait/drop` |
| `session` | op.sc 内核协议 | **rpc 延迟应答会话**（与 future 对称） | `respond`（由实现填充）；`done s, r` 兑现 |

`pool` / `queue` / `promise` / `session` 都是 **op 层接口协议**：类型与方法属语言内核（默认导入），
mt 模块只是**默认策略实现**——犹如 io 的 `file()` 之于 `com`，填充协议指针并返回 `T&`。

---

## 3. 内存布局：联合分配 `[节点][参数]`

跨线程传递 rpc，参数必须随消息走。统一哲学：**单次分配 `[消息/任务节点][rpc 参数]`，参数紧随节点拷入，
投递点无需保活**。

### 3.1 run 线程（op 内核）

```
[ thread 对象 | rpc 参数 | 实现私有区 ]
                ↑ p + sizeof(thread) 即参数；线程实体与参数同生命周期
```

### 3.2 post / async 消息节点（mt_impl.c）

```c
typedef struct pmsg {
    struct pmsg *next;
    que_state   *receiver;   /* 目标队列：detach 清理 + 归属判定 */
    void       (*fn)(void*); /* rpc worker */
    int32_t      prio;       /* 优先级：ready 链按其有序 */
    uint8_t      delayed;    /* 延迟项：按 deadline 入 delaying 链 */
    struct timespec deadline;
    sync_sess   *sess;       /* ≠NULL=同步消息（R2 铁律）；NULL=投递/异步（fire-and-forget） */
} pmsg;                       /* 投递/异步：[pmsg][rpc 参数] 内联；同步：参数在调用方栈，不内联 */
```

- **post / 异步**：参数**内联**紧随 `pmsg`（`(void*)(m+1)`），节点由执行方释放——投递方不保活。
- **同步（sync）**：参数**不内联**，留在**调用方栈**（`sess->params`）——因为铁律保证调用方栈在执行期间存活。

### 3.3 async 的 promise（堆拥有，因不阻塞）

`async` 不阻塞，参数缓冲与返回槽不能放调用方栈 → 由 `promise` 堆拥有：

```
[ promise_box（base/mu/done_cv/done/result/fn） | rpc 参数缓冲 max(psize, 8) ]
```

消费者跑蹦床 `q_async_run`：在堆缓冲上实跑 rpc（写返回槽）→ 类型擦除结果首 8 字节存入 `result` → 置位唤醒
`wait`。调用方 `p->wait()` 取结果后 `p->drop()` 整块回收。

---

## 4. 队列协议：post / sync / async / pull / drop

```sc
var q: queue& = default_queue(main)   # 宿主=当前线程，自跑 pull 循环
q << work(a, b)                       # 投递（post）：rpc 整体打包入队，不等返回
var r: i4 = sync<q> work(a, b)        # 同步：阻塞至执行完成，取返回值
var p: promise& = q->async(...)       # 异步：拿 promise，p->wait() 取结果
for q->pull(0) > 0                     # 排空：取一条执行，队空返 0
    skip
q->drop()                              # 析构：解绑 → 排空残留 → 回收
```

| 方法 | 阻塞 | 返回 | 参数位置 | 应答 |
|---|---|---|---|---|
| `post` | 否 | 成功/失败 | 节点内联 | 无（fire-and-forget） |
| `sync` | 是 | rpc 返回值（0 成功 / 1 超时 / -1 中断） | 调用方栈 | rpc 体 return **或** 延迟 `done`（§7） |
| `async` | 否 | `promise&` | promise 堆缓冲 | 消费者兑现，`p->wait()` 取 |
| `pull` | 可超时 | 1 处理 / 0 超时空 / -1 已关闭排空 | —— | 驱动本 port 收件箱执行 |

**pull 的 P5 时序**：先把 `delaying` 中已到期项按 prio 提升进 `ready`，再取 `ready` 队头（最高优先级）；
`ready` 空时按 `min(pull 截止, delaying 头到期时刻)` 定时等，醒来重算（鲁棒于虚假唤醒）。

---

## 5. sync 铁律（核心，R2）

`sync` 是阻塞型 rpc。一次 sync 的会话状态机：

```
SS_QUEUED ──pull──▶ SS_PULLING ──执行完──▶ SS_DONE
    │                                          ▲
    └──(超时/drop/线程退出，尚未 pull)──▶ SS_CLOSED┘（返回 -1=被中断）
```

**铁律**：

- `timeout <= 0`：无限等至 `DONE/CLOSED`。
- `timeout > 0` 且超时触发：
  - **`SS_QUEUED`（还没被 pull）** → 干净摘除消息、**零执行**、返回 `1`（超时）。
  - **`SS_PULLING`（执行已开始）** → **死等至 `DONE`**（返回 `0`），**绝不放弃**。

为什么必须这样：pull 把消息标记 `PULLING` 与调用方超时摘除消息，**都在 `g_mutex` 下串行**——
所以标记 `PULLING` 之后调用方保证不再触碰消息节点 `m`，执行方可放心在调用方栈参数上跑 rpc、写回返回槽。
这把「执行方读写调用方栈」变成安全操作，**消灭了参考原型里的堆影子会话与 pending 列表**（见 §12）。

会话对象 `sync_sess` 就放在**调用方栈**上，消息只持 `&sess`：

```c
typedef struct sync_sess {
    cnd_state *cond;     /* = 调用方 port 的 send 条件变量 */
    int32_t    state;    /* SS_QUEUED/PULLING/DONE/CLOSED */
    void      *params;   /* 指向调用方栈上的 rpc 参数 */
    void     (*fn)(void*);
    session    pub;      /* R4：暴露给延迟应答的公有会话句柄（见 §7） */
} sync_sess;
```

---

## 6. 循环死锁替代（P5d）

若线程 A 正阻塞 `sync` 等线程 B，而 B 又反过来 `sync` 等 A（或更长的环），朴素实现会死锁。
sync 入口先做**循环死锁替代判定**：

```c
/* 须持 g_mutex：from 端口拟向 tq 队列 sync，是否应改为本地替代执行？ */
static int port_should_substitute(que_state *tq, sc_port *from) {
    if (tq->consumer == from) return 1;           /* 自替代：向自己消费的队列 sync */
    sc_port *c = tq->consumer;
    int guard = 4096;                             /* 防御步数上限 */
    while (c && guard-- > 0) {
        que_state *w = c->waiting;                /* c 线程当前阻塞 sync 的队列 */
        if (!w) return 0;                         /* c 未阻塞 → 无循环 */
        if (w->consumer == from) return 1;        /* 环回到自己 → 替代 */
        c = w->consumer;
    }
    return 0;
}
```

走的是「每节点至多一条出边（`waiting`）」的函数图，在「替代使图保持无环」不变量下必终止；意外成环时
保守返回 0（退化为正常投递，至多死锁而非检测器卡死）。

命中替代时，**调用方在本线程直接执行受害队列的 rpc**（写回自己的返回槽），并配合 `substituting` 标志：

- 自替代（向自己消费的队列 sync）：直接 `fn(params)` 本地执行。
- 环替代：置受害端口 `victim->substituting = 1` → 本地 `fn(params)` → 清 `substituting`；
  若受害方已超时挂起（不再 `waiting`），唤醒其 `send` 让它解栈。
- **铁律延伸**：sync 返回前，若本端口 `substituting`（有人正替我执行）→ **死等其完成再解栈**，
  防止替代者还在读我的栈我就返回了。
- 本地替代不支持延迟应答 → 执行前 `op_session_begin(NULL)` 置空当前会话，防止 rpc 体误领（§7）。

---

## 7. `session` 延迟应答（R4）——与 `future` 对称

单线程异步用 `future`：`async fn(...)` 拿 `future&`，将来 `done f, r` 兑现，`f->await()` 取值。
跨线程 rpc 的「延迟应答」与之**完全对称**，只是把 `future` 换成 `session`：

```sc
inc mt.sc

var g_sess: session& = nil      # 被延迟的会话（rpc 体里领走，待将来兑现）
var g_arg:  i4 = 0

# rpc 体：取会话转延迟应答，return 值被忽略（真正结果由将来的 done 给出）
rpc serve: i4, x: i4
    var s: session& = async    # 裸 async：取当前 sync 会话 → 本次改为延迟应答
    g_sess = s
    g_arg  = x
    return 0                   # 立即返回，但调用方仍阻塞，等将来 done

# 另一线程在 rpc 体「之外」才兑现 —— 这就是延迟
rpc server: qq: queue&
    qq->pull(-1)               # 进入 serve()：会话被领走，调用方仍阻塞
    done g_sess, g_arg * 10    # 延迟应答：现在才写回调用方返回槽并唤醒（跨线程）

fnc main: i4
    var sq: queue& = default_queue(nil)
    var st: thread& = nil
    run server(sq), &st
    var r: i4 = sync<sq> serve(7)     # 阻塞至延迟应答兑现：期望 70
    printf("delayed response: r=%d\n", r)
    st->join()
    sq->drop()
    return 0
```

### 7.1 三个原语的对称表

| 单线程 future | 跨线程 session | 语义 |
|---|---|---|
| `async fn(...)`（带调用） | **裸 `async`**（其后换行，不带调用） | 取句柄 |
| `var f: future& = async fn()` | `var s: session& = async` | 句柄落地 |
| `done f, r` | `done s, r` | 兑现：写回返回槽 + 唤醒等待方 |
| `f->await()` | （调用方本就在 `sync` 阻塞） | 取结果 |

### 7.2 运行时三步

1. **领取**：rpc 体里裸 `async` → `op_session_current()`，置 `g_cur_session_taken=1`，返回**当前 sync 会话**
   `&sess.pub`（`session&`）。pull 在执行 rpc 前已 `op_session_begin(&s->pub)` 把它设为当前会话。
2. **转延迟**：执行方在 `s->fn(s->params)` 返回后检查 `op_session_taken()`——若被领走，则**不自动应答**
   （不置 DONE），调用方继续死等（PULLING）；rpc 体的 `return` 值被忽略。
3. **兑现**：将来（可跨线程）`done g_sess, g_arg*10` → 经会话协议指针 `s->respond(...)` →
   `mt_session_respond` 加锁、`memcpy` 写回调用方返回槽（按静态返回类型精确宽度）、置 `SS_DONE`、
   唤醒调用方 `send`。会话句柄 `&sess.pub` 在调用方栈，铁律保证其在 `done` 之前始终存活。

### 7.3 `done` 的多态

`done` 语句按操作数类型静态分派：

| 形态 | 操作数类型 | 转译 |
|---|---|---|
| `done f, r` | `future&` | `future_done(f, &r, sizeof r)` |
| `done s, r` | `session&` | `({ T _dv = (r); (s)->respond((s), &_dv, sizeof _dv); })`（T=静态返回类型，精确宽度写回返回槽偏移 0） |
| `done s` | `session&`（无结果） | `(s)->respond((s), NULL, 0)` |

---

## 8. 清理与生命周期

正确程序里消费线程退出前已 `pull` 干净（收件箱空）、`drop` 在 `join` 之后调用。以下是兜底：

- **线程退出**（`port_release`，TLS 析构触发）：把本 port 所有 attach 队列解绑（`consumer=NULL`，
  令后续 `q->drop()` 见 `consumer==NULL` 安全回收）；释放收件箱残留消息——其中**同步残留**以
  `SS_CLOSED` 唤醒被阻塞的调用方（返回 `-1`=被中断），不致其永久挂起。
- **queue drop**（`que_drop`）：关闭队列 → 若仍 attach（异常路径）则从 port 解绑并清其在 port 收件箱里
  归属本队列的消息（`inbox_discard_for`，同步残留同样 `CLOSED` 唤醒）→ 排空 `staging` → 回收整块。
- **残留同步消息**（`pmsg_discard`）：若是同步消息（调用方在等）先以 `SS_CLOSED` 唤醒；消息节点本身
  由清理处释放（调用方不持有它）；fire-and-forget 消息直接释放。

**唯一不兜底的口子**：被领走的延迟 `session`，若责任线程从此不 `done`，调用方永久阻塞——
程序责任（§11）。

---

## 9. 全场景示例

### 9.1 detach / joinable / 入池

```sc
run work(a, b)          # detach：独立线程，结束自释放
run work(a, b), &t      # joinable：t: thread&，须 t->join() 回收
run<p> work(a, b)       # 入池：p: pool&（default_pool 构造）
```

### 9.2 主线程消费队列（pull 循环）

```sc
var q: queue& = default_queue(main)
q << job(1)
q << job(2)
for q->pull(0) > 0          # 排空所有已投递任务
    skip
q->drop()
```

### 9.3 跨线程同步 rpc

```sc
var sq: queue& = default_queue(nil)
var st: thread& = nil
run consumer(sq), &st              # 消费线程跑 sq->pull 循环
    var r: i4 = sync<sq> compute(7)    # 阻塞至返回
st->join(); sq->drop()
```

### 9.4 异步 rpc（promise）

```sc
var p: promise& = sq->async(...)   # 立即返回
... 干别的 ...
var r: i8 = p->wait()              # 取结果（消费者兑现前 wait 会阻塞）
p->drop()
```

### 9.5 延迟应答（session）

见 §7 完整示例（`examples/feature45.sc`，输出 `delayed response: r=70`）。

### 9.6 线程池

```sc
var p: pool& = default_pool(4)     # 4 工作线程（0 → CPU 逻辑核数）
run<p> work(a, b)
p.join()                           # 屏障：等已提交任务完成（池仍可用）
p.drop()                           # 析构：等完成 → 停池 → 回收
```

### 9.7 按需自调度池（drain_pool —— pool 的另一种策略）

`pool` 是 sc 对「线程调度组件」的核心抽象，**投递动词只有 `run`**。mt 提供两种策略，均返回
`pool&`、均凭 `run` 投递，只是运行时 `run` 指针指向不同实现：

- `default_pool(n)`：常驻 worker 消费**内部 FIFO 任务队列**——`run<p> f()` 入队，`f` 执行一次。
- `drain_pool(n)`：**按需自调度**——无任务队列，worker 反复跑投递的工作单元 rpc 直到一轮无新
  投递即退；`run<dp> f()` = 通知有新活 + 按需激活一个 worker（上限 n）。适合「任务在外部图/
  队列里、由应用自调度」的场景（如 `templates/workflow-graph/back-drain` 的 `back` drain）。

```sc
rpc work_unit: id: i4              # 工作单元：自身循环排空至「本视角无活」后返回
    while <还有活>
        <处理一单位>              # 重活在锁外
    return 0                       # 无活即返回（池代检无新 run 则令本 worker 退出）

var dp: pool& = drain_pool(4)      # 上限 4 worker；构造时不启 worker
run<dp> work_unit(0)               # 生产者投放后调用：通知有新活，内部按需激活 worker
dp->join()                         # 屏障：等当前 worker 全部退出（running→0）
dp->drop()                         # 析构：置停 → 等 worker 退出 → 回收
```

**一致性（核心）**：`running`（在跑 worker 数）本质是个信号量，连同「有新活」世代 `gen`
均由**池内部锁**守护，经**世代代检**消除丢唤醒——`run` 先 `gen++` 再判 `running<max` 激活；
worker 每轮跑 `work_unit` 前在锁下快照 `seen=gen`、返回后在锁下复检 `gen!=seen`（其间有人
`run`）则再来一轮、否则 `running--` 退出。两段各在池锁下原子成段，故即便工作单元内部另有锁
（如外部图的锁），只需「先令工作源可见、再 `run<dp>`」即不漏活。**应用层无须再手搓「在跑
计数 + 补投」信号量——这正是 `pool` 抽象（线程调度组件）该承担的职责。**

---

## 10. C 转译方案（编译器零 emit mt 符号）

编译器对 mt 类型一律**经协议指针派发**，绝不直接 emit `mt_*` 实现符号：

- `q << work(a,b)` → `q->base.post(&q->base, work_rpc, &args, sizeof args, prio, delay)`；
- `sync<q> work(a,b)` → `q->base.sync(&q->base, work_rpc, &args, sizeof args, ...)`；
- `q->async(...)` → `q->base.async(...)` 返回 `promise*`；
- `p->wait()` / `p->ready()` / `p->drop()` → `p->wait(p)` 等协议指针。

**允许 emit 的少量 op 内核符号**（非 mt 实现）：`future_done`、`op_session_current`。
`session.respond` 是**逐对象方法指针**，由 mt 在构造会话时填充（`mt_session_respond`），编译器只发
`(s)->respond((s), ...)`——派发点零知识。

`op_impl.c` 拼接进 `op.sc` 生成的 C 单元（同 TU）。**后端中立**的会话 TLS 函数
（`op_session_begin/current/taken`）必须放在 `#ifdef SCC_WITH_UV` **之外**（默认 poll 后端不定义 UV），
否则默认构建链接报未定义符号。

```c
/* op_impl.c：后端中立区，置于 future_new 之后、#ifdef SCC_WITH_UV 之前 */
static TLS session *g_cur_session;
static TLS int      g_cur_session_taken;
void     op_session_begin(session *s) { g_cur_session = s; g_cur_session_taken = 0; }
session *op_session_current(void)     { g_cur_session_taken = 1; return g_cur_session; }
int      op_session_taken(void)       { return g_cur_session_taken; }
```

---

## 11. 设计边界速查（向用户的承诺）

| 想做的事 | 行为 |
|---|---|
| 创建线程 | `run rpc(...)`（detach）/ `, &t`（joinable）/ `run<pool> rpc(...)`（入池） |
| 跨线程同步调用 | `sync<q> rpc(...)`，阻塞取返回值 |
| 同步调用超时 | 仅**未被 pull** 时撤回（返回 1）；已开始执行则铁律死等 |
| 循环互锁 | 自动本地替代执行，不死锁 |
| 延迟应答 | 裸 `async` 取 `session&`，将来 `done s, r` 兑现 |
| `done` 一直不发 | 调用方永久阻塞（= promise 永不兑现）——**程序责任** |
| 线程退出/队列 drop 时有人在等 | 以 `CLOSED` 唤醒，返回 -1（被中断），不永久挂起 |
| 想要 work-stealing 池 | 不提供，另起 `*_pool(n)` 策略构造 |
| 取消正在执行的 rpc | 不支持（铁律：开始即承诺完成） |

---

## 12. 设计取舍与已知缺口

mt 的 PORT 模型在「每线程单收件箱 + 全局单锁 + 优先级/延迟投递 + 循环互锁替代」之上，
按 sc 铁律做了若干**刻意简化**：

| 维度 | 通用做法 | mt（R1+R2+R4） | 结论 |
|---|---|---|---|
| 延迟应答取会话 | action 不应答 → 标记待决 | 裸 `async` → `op_session_taken()` | 概念对齐 |
| 延迟应答兑现 | pending 模式回写 | `mt_session_respond` | 概念对齐 |
| sync 会话生命周期 | 堆影子会话（调用方可中途 abort） | **调用方栈会话**（铁律：不中途放弃） | 刻意简化 |
| 延迟会话登记 | port->pending 列表 | 嵌在调用方栈，无列表 | 刻意简化 |
| detach 唤醒延迟调用方 | 遍历 pending 以 CLOSED 唤醒 | ❌ 不登记 → 程序责任 | **唯一行为缺口** |
| 结果写回 | 类型擦除 packet（引用计数） | 强类型 `memcpy` 写返回槽（精确宽度） | 刻意简化（sc ABI） |

**唯一行为缺口**：若把延迟会话挂在 `port->pending`，执行方 port detach 时便能遍历 pending
以 `THREAD_CLOSED` 唤醒被阻塞的调用方；mt 在 pull 后即释放消息节点、仅靠应用持有变量（如 `g_sess`）
追踪会话，所以「`done` 永不发生」时调用方死等。此缺口为**刻意接受的程序责任**（同「promise 永不兑现」），
由铁律换来「无影子会话、无 pending 列表」的简洁。

---

## 13. 实现路线与状态

| 轮次 | 主题 | 状态 |
|---|---|---|
| R1 | PORT 单收件箱模型 | ✅ 已落地 |
| R2 | sync 铁律 + 删除堆影子会话 | ✅ 已落地 |
| R4 | rpc 延迟应答 `session`（裸 `async`→`session&`，`done s,r` 兑现） | ✅ 已落地 |

回归基线：`bash tests/run.sh`（golden 全绿）；并发正确性以 ASan + TSan 复核
（`SCC_CFLAGS="-fsanitize=thread -g -O1" SCC_LDFLAGS="-fsanitize=thread"`）。
