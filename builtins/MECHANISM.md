# sc 核心机制/原理

> 本文是 sc 语言内置核心机制的**统一规格文档**，与 [REFERENCE.md](REFERENCE.md)（模块参考总览）和
> [ROADMAP.md](ROADMAP.md)（开发路线图）并列为 builtins 的三大支柱文档。每个机制独立成章，重点是 **"怎么做、怎么实现、背后的技术和流程是什么"**，均从实际源码推导总结（不是用户手册式的功能罗列）。各机制自身的更细致题目见所属子模块目录。

---

## 机制全景

| 机制 | 语法入口 | 运行时载体 | 一句话 |
|------|----------|-----------|--------|
| **内存安全检查** | `--check=mem`、`--check=ptr` | 栈/堆 canary 哨兵 + nil 守卫 + 下标越界 | 编译期注入运行时哨兵/校验，命中即 abort+源码定位 |
| **自动指针** T@/T*/T@1 | `var p: T@/*/@1`、`T()`、`&expr` | `T_fat {p,tar,own}` + `sc_ref {in,out}` | 双向引用图 + ARC 自动释放 + 释放点悬挂/泄露检测 |
| **链接** `~` | `def T: ~ {}` | `chain {head}` + 注入 `_prev/_next` | 结构体侵入式双向链表，O(1) 增删，chain 不持有元素 |
| **分身和切片** `<S>` | `def T: <S> {}`、`var s: T[...]` | `T__project` 匿名句柄 + `_self` 回指 | 本体 T 的窗口视图，alloc/free 构造析构，零拷贝 |
| **成员与容器** `<C,I>` | `def T: <C, I> {}` | 注入节点 `I` + 自定义容器 `C` | 给结构体挂载可检索容器，`[]` 下标糖透传 find |
| **线程协同** queue | `run rpc()`、`sync`/`async`/`post`、`deferred` | PORT 单收件箱 + queue/protocol + `g_mutex` | PORT 中枢 + sync 铁律 + 循环互锁本地回退 |
| **异步协程** | `async rpc()`、`await`、`done` | `future` + 状态机帧 + 事件循环 | rpc 编译为 stackless 状态机 + future 就绪链 + epoll/kqueue 驱动 |
| **设备通讯** com | `com << v`、`com >> s`、`com << rpc()` | `com`(vtable) + `limit`(有界读) + `ioq`(循环缓冲) | 协议驱动的设备 io + << >> 收发糖 + 六种展开模式 |
| **类机制** cls/dim | `cls T:`、`dim D:`、`object` | `T_hyper_impl` 分派器 + `SC_DIM_*` 枚举 | 单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除 |
| **依赖图** tok | `tok x: "id"`、`dep…map`、`form`、`back` | `token_bind/intern` + seqlock + 图度量烘焙 | 共享量 + 依赖图 DAG + 前向/反向/迭代三机分离 |
| **内存分配** | `T()`、`sc_alloc`/`sc_chunk`、`-DSC_POOL`、`-DMEM_DEBUG` | `sc_alloc` 间接层 + chunk 无锁池 + 两套越界金丝雀 | 可切换分配层 + 每线程 size-class 池 + `--check=mem`/`MEM_DEBUG` 双金丝雀 |
各机制互相衔接：
- **T@ 胖指针** 是 tok 共享量的值载体（`@` 类型擦除，自描述胖指针）
- **cls/dim 类机制** 的 `object@`（类型擦除自动指针）基于 T@ 双向引用图，`SC_DIM_DROP` 经分派器析构
- **tok** 的 `'/'` 前缀多线程模式走 **mt** 的无锁原语（op 层 `sc_*` 原子 RMW + seqlock）

---

## 1. 内存安全检查 `--check=mem` / `--check=ptr`

> 以下两个检查开关独立于 T@ 自动指针机制，属于语言级的运行时内存安全守卫。

### 1.1 `--check=mem`：栈/堆地址越界探测

在编译时对函数内栈数组和 `--check=mem` 标注的堆分配注入尾哨兵（canary）。栈数组超额分配额外空间存放魔数哨兵值，域退出时校验——若哨兵被破坏（数组越界写），立即报告并定位源码。
同时注入 `-fstack-protector-strong` 保护返回地址。
堆对象在 `sc_ref` 头尾各加一个 canary，`free` 时校验。

### 1.2 `--check=ptr`：nil 解引用与数组下标越界

编译时在指针解引用（`p->field`、`*p`）前，也就是访问指针时，注入 nil 校验——命中即 abort 并报告源码位置。
对编译期已知维度的栈数组，在下标访问时注入越界校验（`i >= 0 && i < len`），命中即 abort 并报告下标与数组长度。

---

## 2. 自动指针：`T@` 胖指针 / `T*` 瘦指针 / `T@1` 单例指针

> **机制框架**
> - 一次指针赋值在两端各记一次账（`in` / `out`），释放点做双向验证（→ §1.1）
> - 裸指针 `T&` 和胖指针 `T@` 两条完全独立的路径，胖指针 opt-in；标量/裸指针经显式 `(e:@)`/`(e:*)` 按值装箱入空记账句柄（→ §1.2）
> - 赋值时先拆旧边再绑新边，`&` 取址沿胖路径传染（→ §1.3）
> - `in→0` 即 ARC 释放点：先 `drop` 再判 `out`，隔离节点自动 free、out>0 报未清理（→ §1.3）
> - 三套可选构建开关（ref/mem/ptr），默认关闭仅保留 ARC（→ §1.4）
> - 跨 C ABI、按值拷贝、goto 跨域等编译器硬约束（→ §1.6）

### 2.0 裸指针

```sc
var raw: T&        # 裸指针，8 字节，纯 C 语义，不追踪
```

首先，对齐一个概念：**裸指针**。也就是 `T&` 类型。它对应的就是 C 语言中 T*，即原生的指针类型。
自动指针就是相对它而言的。而所谓的自动就是对裸指针的封装、以及对它的生命周期、引用双边关系的管理和维护

### 2.1 胖指针 @：双向引用追踪 + ARC

```sc
var ptr: T@        # 胖指针，24 字节 = {p, tar, own}，参与引用图
```

胖指针是 sc 推荐的默认指针类型。它把一次指针赋值 `A.p = &B` 看作一条引用边，这条边**同时约束两端**：
- **B 不能比这条边先死** — 否则 `A.p` 悬挂（约束在 B）
- **A 不能丢下这条边就死** — 否则 B 泄露（约束在 A）

sc 在两个端点各记一次账：

| 计数 | 含义 | 维护者 | 违反时 |
|------|------|--------|--------|
| `in`（入边数） | 多少指针指向本对象，或被多少人引用 | `tar` 字段（目标引用头） | `in>0` 时释放 → **悬挂** |
| `out`（出边数） | 本对象持有多少指针 | `own` 字段（持有者引用头） | `out>0` 时释放 → **泄露** |

**释放条件**：一个对象可以安全释放，当且仅当 `in == 0 && out == 0`，即它不指向任何人、也没有任何人指向它。

对比传统方案：

| 方案 | 追踪方向 | 能做的 | 做不到的 |
|------|---------|--------|---------|
| C++ `shared_ptr` | 只记入边（引用计数） | 无人引用时自动释放目标 | 持有者释放时，（子成员）可能还引用了别人，导致别人无法释放；又或者是循环引用；这个泄露发现不了 |
| Rust | 编译期借用检查 | 全部保证 | 概念门槛高，数据结构很难转成 C |
| **sc `T@`** | **一条边两端各记一次** | 自动释放 + 悬挂和泄露都能在释放点检测 | 不递归释放、不反向置空 |

#### 2.1.1 胖指针的 `own` 身份：

| 胖指针位置 | own |
|-----------|-----|
| 经裸指针（base）访问的子成员 | `SC_OWN_RAW(-2)`, 可读/可 `=nil`；绑新边仅限 `init` 内经 `this` |
| 栈/全局根指针 | `SC_OWN_ROOT(-1)`，域退出自动拆 |
| 经胖指针（base）访问的子成员 | 最后一个胖 hop 所指对象的 `out` 地址 |
| `&` 取址结果赋给接收者 | 按接收者自身位置递归套用 |

#### 2.1.2 引用图维护

**赋值/解绑**：
```
owner.p = ⌖B      # 先拆旧边(旧目标.in--, owner.out--)，再绑新边(B.in++, owner.out++)
owner.p = nil     # B.in--, owner.out--
```

**`&` 取址传染**：对胖目标的子成员取址 → 结果也是胖指针；`tar` 来自访问路径上最后一个胖 hop。

**`in→0` 即释放点**：先调 `drop`（若有），随后 `in==0 && out==0` → 自动 `free`（ARC）；
`out>0` → 报错"未清理"。`drop` 体内 `this->m = nil` 解绑子成员 → 逐层递归触发子对象 `in→0`。

#### 2.1.3 释放点与验证

三种触发：(a) 域退出（先全部拆边再全部断言，两阶段）、(b) 堆对象 `in→0`（ARC）、
(c) 用户显式 `free`。域退出和 `in→0` 的断言由 `--check=ref`（`SCC_REF_CHECK`）控制——
开启后在释放点注入源码定位的悬挂诊断（`in>0` 时报告引用者位置），默认关闭仅保留 ARC 自动释放。

**设计边界**

| 能做到 | 做不到 |
|--------|--------|
| 孤立堆对象 ARC 自动释放 | 不递归（不区分所有权层级；靠 `drop` 内的解绑递归回收） |
| 释放点检测悬挂（in>0）并定位源码 | 不反向置空引用者（目标释放后，不会自动把指向它的指针置 nil） |
| 释放点检测未清理的出边（out>0） | 不保证无泄漏（循环引用需要手动断边；忘记释放照样泄漏） |
| 全 `T@` 闭包内安全 | 一旦转裸（`r: & = ptr`）就退回纯 C，编译器管不了 |

### 2.2 瘦指针 *

```sc
var ptr: T*        # 瘦指针，24 字节 = {p, tar, dtor}，只追踪入边，不计出边
```

相比胖指针，瘦指针仅保留 `p`/`tar` 两个槽，放弃维护 `own` 的引用关系。即它**只记入边**（`tar.in`），不记出边——也就是 C++ 的 shared_ptr

之所以引入瘦指针，并非要兼容某个使用场景。而是为了某个特殊场景进行优化。也就是说，使用中，默认都是使用胖指针的。但对于 item 为指针的容器而言，如果 item 是胖指针，那么它们的 own 就肯定都是容器自身，此时每个 item 都保留一个 own 字段，并维护容器的出（被）引用计数就显得太奢侈了。
所以，瘦指针的设计使用场景，目前仅仅是针对将指针类型作为 item 的容器

### 2.3 单例指针 @1

```sc
var ptr: T@1       # 单例指针，物理就是裸 T*（8 字节），退域 RAII，unique_ptr 语义
```

单例指针**不是句柄结构**——它物理上就是一个普通 `T*`，无任何 16/24/32B 句柄、无引用图记账。唯一的自动化是**退域 RAII**：作用域退出或 `return` 前，若非 nil 则 `drop`（若有）+ `sc_free`，等同 C++ `unique_ptr`。重新赋值覆盖旧值时也先销毁旧对象。

- 必须**具名类型**（`@1` 需具名，类型擦除句柄无意义）
- **单层**：不可再叠 `&`（`T@1` 已是终态）
- 最轻：零句柄开销，适合"唯一持有、退域即释放"的对象

### 2.4 类型系统

- **裸→胖/瘦**：禁止隐式转换（裸指针无 tar/own 信息）；但允许显式强转 `(e: @)`/`(e: *)` ，并按 **标量** 装箱（见下，非托管，不入引用图）
- **胖/瘦→裸**：支持隐式自动转换 `r: & = ptr`（取 `ptr.p`）；源 `ptr` 不变、记账保留，但产出的裸指针 `r` 脱离引用图——经 `r` 的赋值/解引用不再追踪，不可检测
- **单例指针 `T@1`**：物理 `T*` + 退域 RAII 销毁（`drop + free`），unique_ptr 语义
- **裸 `@`**（类型擦除）：`sc_afat`（24B 同构 `sc_fat` + `dtor` 槽），用于通用容器
- **标量/裸指针→胖/瘦（显式擦除装箱）**：`(e: @)`/`(e: *)` 把任意非胖源装入空记账句柄——整数经 `(intptr_t)` 擦入 `.p`、裸指针/数组直取 `.p`，`tar=NULL`（不计入边）、`dtor=NULL`（不析构）、`own=SC_OWN_RAW`（不计出边，仅 `@`/`sc_afat` 有）。纯值搬运，非托管：不参与引用图、解绑 no-op；调用点经 `(e: T@)`/`(e: T*)`/`(e: i8)` 还原。tok 共享量值即用此装箱（整数触发标量、裸指针句柄无堆分配地塞入值槽）

### 2.5 编译器硬约束

- 重新赋值必须先拆旧边；所有出口（return/goto/break）插入拆解
- 域退出两阶段（先拆边后断言）
- `T@` 数组：局部/全局（一维/多维）已实现；结构字段/参数/返回/tls 仍报错
- 跨 C ABI（导出/rpc/cImpl）禁止含 `T@`；sc 内部按值拷贝禁止内嵌 `T@`
- `goto` 跨作用域→编译期报错或注入清理

---

## 3. 链接 `~`：侵入式双向链表

> **机制框架**
> - `def T: ~ {}`：编译器在结构体首部注入 `_prev`/`_next` 两个指针，使其成为链表节点（→ §3.1）
> - `chain` 是链表管理器：append/push/pop/remove/sort 等操作，O(1) 增删（→ §3.1）
> - chain 不持有元素——remove/pop/cut 只摘链，不释放元素本身

### 3.1 类型注入与 chain 管理

`~` 是 `def` 结构体的修饰符（与 `{}` 并列）。语法 `def T: ~ {}` 让编译器在结构体**首部**注入两个 `void*` 指针——`_prev` 指向前驱、`_next` 指向后继。此后任何 `T` 实例都可以作为链表节点。

链表由 `chain` 类型管理（`op.sc` 声明，默认导入，C 运行时在 `op_impl.c` 始终链接提供）。`chain` 只持有 `head` 指针，提供完整的双向链表操作——`append`/`push`/`pop`/`before`/`after`/`remove`/`first`/`last`/`revert`/`cut`/`sort`。所有操作只修改 `_prev`/`_next` 指针，**不分配、不释放元素内存**。

`prev(o)` / `next(o)` 是编译器内置的导航伪函数：`next(o)` 取注入的 `_next`，`prev(o)` 为边界安全前驱（链头返回 nil）。导航返回 `void&`，用显式 `: T&` 下转回元素指针。

---

## 4. 分身和切片 `<S>`：本体窗口视图

> **机制框架**
> - `def T: <S> {}`：S 为分身/切片类型，注入 `_self: T&` 回指本体（→ §4.1）
> - `var s: T[参数...]`：分身句柄——T 的 alloc 参数构成切片上下文，`s = t` 绑定本体（→ §4.1）
> - `s = nil`：经 `_self` 回指本体调用 `T_free`，安全解绑（→ §4.1）

### 4.1 分身句柄的展开

分身/切片机制让一个本体类型 T 可以有**窗口视图**——例如一个 `buffer` 可以有 `buffer[1024, nil]` 这样的定长读视图（com 的 `limit` 句柄正是这样实现的）。

类型要求：S 需声明为 `@def S: {}`（或带 `~`），T 需提供 `alloc: S&, ...`（分身构造器）和 `free: s: S&`（析构器）。编译器在 S 首部注入 `_self: T&` 回指指针（若 S 带 `~` 则在 `_prev`/`_next` 之后）。

**句柄展开**（`var s: T[a, b]` + `s = t`）：
- `T[a,b]` → 匿名结构体 `T__project { a, b, _: S& }`——参数存入句柄上下文
- `s = t` → `s._ = T_alloc(&t, s.a, s.b)` + `s._->_self = &t`——调 alloc 构造切片，编译器回写本体指针
- `s = nil` → 经 `s._->_self` 取回本体 T，调用 `T_free(&t, s._)` 释放切片

---

## 5. 成员与容器 `<C,I>`：可检索元素管理

> **机制框架**
> - `def T: <C, I> {}`：I 注入 T 首位作为容器节点，C 为管理 I 的自定义容器类型（→ §5.1）
> - T& 与 I& 零偏移互转；导航经容器方法（first/next/find/insert/remove）（→ §5.1）
> - `c[key]` 下标糖透传 `c.find`，命中得 I&，`(it: T&)` 下转回元素（→ §5.1）

### 5.1 容器注入与下标糖

容器/成员机制让结构体 T 挂载一个**自定义容器 C** 来管理和检索 T 的实例。I 是注入到 T 首位的容器节点类型，C 是容器实现。

类型要求：C 须提供 `insert: ret, item: I&` / `remove: ret, item: I&` / `find: ret, out: I&&, ...` / `first: I&` / `next: I&, item: I&`。可选 `last`/`prev` 反向导航。

编译器将 I 注入 T 首位（若 T 带 `~` 或 `<S>` 则在那些注入之后），因此 `T&` 与 `I&` 零偏移互转。导航经 C 的方法——`t.first()` / `t.next(it)` 返回 `I&`。

`c[key]` 是容器下标糖，仅对容器类型 C 生效：展开为 `c.find(&out, key)`，命中得 I&（`(it: T&)` 下转），未命中得 nil。

> **机制框架**
> - 每线程一个 PORT 收件箱中枢，全局单锁消解所有锁序环（→ §4.1）
> - sync 铁律：一旦开始执行必死等到底，栈帧始终有效，无需堆影子会话（→ §4.2）
> - 循环互锁时自动本地回退执行（substitute），替代死锁（→ §4.2）
> - 三种消息投递：sync 阻塞取返回值 / async 拿 future / post 投递即忘（→ §4.3）
> - deferred 延迟应答：sync 的返回可推迟到另一线程兑现，与 future 对称（→ §4.4）
> - 编译器经 op 协议指针派发，零 emit mt 实现符号——op 与 mt 双向解耦（→ §6.5）

## 6. 线程协同 `mt`：PORT + sync 铁律

### 6.1 概念模型：PORT 单收件箱

线程间是**每线程一个 PORT**——单一收件箱中枢：

- **port**：线程局部 `TLS g_port`，地址即稳定唯一身份。多个 `queue` 可 attach 到同一 port，
  消息归并进**单一收件箱**按优先级排成单链 → 跨多队列全局时序。
- **queue**：协议对象（vtable）。首次被某 port `pull` 时惰性 attach，积压消息整块插入收件箱首部。
- **全局唯一互斥 `g_mutex`**：保护所有 port 收件箱 + queue 暂存 + consumer/waiting 关系图——
  单锁消除一切锁序环。

### 6.2 sync 铁律（核心承诺）

> **一次 sync 一旦开始执行（PULLING），调用方必死等到 DONE/CLOSED，绝不中途放弃。**
> 调用方栈帧在执行期间始终有效，执行方可直接读写调用方栈上的参数与返回槽，
> 无需堆影子会话。

**循环互锁回退**：若 `sync` 的消费者恰是发送者自己（A→B→A 或 A 经队列回自己），
检测到循环依赖后在调用方线程本地直接执行 rpc 体（substitute），替代死锁。

### 6.3 消息投递三态

| 方式 | 语法 | 语义 |
|------|------|------|
| `sync` | `ret = queue.sync(rpc(...))` | 阻塞等返回值；铁律保障不中途放弃 |
| `async` | `f = queue.async(rpc(...))` | 返回 `future&`，`await f` 拿值；可跨线程 `done` |
| `post` | `queue.post(rpc(...))` | 投递即忘 |

### 6.4 延迟应答 `deferred`

一次 `sync` 的应答可延迟到 rpc 体返回之后、甚至**另一线程**才兑现：

```sc
fnc handler:: i4, s: deferred&
    s.done(ret)              # 延迟兑现——可在当前 rpc 体内、也可把 s 传给另一线程
```

与单线程 `future`（`async→future→done`）对称。调用方在兑现前一直阻塞（铁律）。

### 6.5 编译器边界

编译器只认识 `op.sc` 暴露的线程协议（vtable 函数指针）与少量 op 内核符号。对
`mutex/pool/queue/future/deferred` 一律经协议指针派发，绝不直接 emit `mt_*` 实现符号——
op 层为 mt 提供操作系统抽象，mt 实现为 op 协议的具体实现者，双向解耦。

---

## 7. 异步协程：`rpc` 状态机 + `future` + 事件循环

> **机制框架**
> - rpc 是协作式异步调度的基本单位：含 `await` 的 rpc 自动编译为 stackless 状态机（→ §4.1）
> - 状态机帧 = 结构体：`_ret`（结果 future）+ `_state`（段号）+ 参数 + 跨 await 存活局部（→ §4.1）
> - `future` 是类型擦除的异步结果句柄：就绪标记 + 结果指针 + 等待者帧 + 恢复入口（→ §4.2）
> - 事件循环 `async_init` → `async_loop` → `async_final`：多路复用（epoll/kqueue/poll）+ 定时器驱动（→ §4.3）
> - `done` 兑现：写回结果、标记就绪、唤醒等待者恢复状态机（→ §4.3）

### 7.1 rpc：协作式调度单位 → 状态机

rpc（remote procedure call）是 sc 异步模型的基本调度单位。它既可作为同步的跨线程消息载体（`run` / `sync`），也可作为异步协程——当 rpc 体内出现 `await` 表达式或 com 的 `<<` / `>>` 收发时，编译器标记该 rpc 为 `hasAwait`，将其编译为 stackless 状态机。

**帧结构**：rpc 的**参数**和跨 await 存活的**局部变量**全部提升到一个结构体帧中：

```c
// 编译器为 rpc serve(x, y) { await ...; var t = ...; await ...; } 生成：
struct serve {
    future *_ret;       // 本次调用的结果 future（调用方通过它取返回值）
    int     _state;     // 当前状态机段号（0 = 初始）
    future *_fut;       // 当前正 await 的 future（挂起点）
    // --- 原始参数 ---
    int32_t x;
    int32_t y;
    // --- 提升的局部变量 ---
    int32_t t;
};
```

**状态机入口**用 `switch(_state)` + `goto _sN` 标签派发段号，await 点把当前段号写入 `_state` 后 `return` 让出，事件循环在 future 就绪后通过帧中保存的恢复入口 resume 状态机。

**启动器 `X__async(参数...)`**：分配帧、装填参数、创建 `_ret` future、首次驱动状态机、立即返回 `_ret`。调用方拿到 `_ret` 后通过 `await` 或 `future_get` 等待结果。

**await 点三段式**（编译器为每个 `await E` 生成）：

```
E 求值 → 得 future f
future_await(f, _p, resume) → 若已就绪 → 继续执行（取结果）
                              → 若未就绪 → _p->_state = N; return （让出）
_s<N>:  // 恢复点：f 已就绪，取结果继续
```

约束（v1）：`await` 只能在 rpc 体顶层直线出现（不可在 if/while/for/case 内），形如 `await E` / `var x = await E` / `x = await E`。

### 7.2 future：异步结果句柄

`future` 是 sc 语言内核的类型擦除异步结果句柄（声明在 `op.sc`，默认导入），承载一次异步操作的完成信号与返回值：

```sc
@def future: {
    ready:  i4       # 0=未就绪, 1=已就绪
    result: &        # 类型擦除结果（调用点用 : T& 还原）
    frame:  &        # 等待者状态机帧（await 点登记）
    resume: &        # 等待者恢复入口函数指针
    next:   &        # 就绪队列链接（事件循环内部）
    id:     i4       # future<ID> 事件 id（>=0=可派发，-1=无标签）
    ctx:    &        # 发起时挂载的用户上下文
}
```

生命周期：
- `async rpc(...)` → 编译器改写为 `X__async(...)` 启动器 → 返回 `future&`
- `await future` → 若已就绪取结果；若未就绪挂起当前 rpc 状态机
- `done future[, result]` → 写回结果、标记就绪、唤醒等待者 — 这是让任意异步原语接入 await 的统一兑现动词

### 7.3 事件循环：多路复用 + 定时器

事件循环是 sc 语言自有异步内核（`op_impl.c`，始终链接），不依赖 libuv（可选 `SCC_WITH_UV` 换后端）：

- `async_init()` — 建立当前线程的事件循环（惰性 TLS）
- `async_loop(proc)` — 驱动循环至全部 future 就绪：多路复用等待（epoll/kqueue/IOCP/poll）+ 定时器截止处理 + future 就绪链唤醒
- `async_final()` — 销毁事件循环

事件循环内部维护全局 future 就绪链表：`done` 把 future 挂入链尾 + 唤醒循环（自管道 `wake()`），循环取出整条链后逐一回调等待者的 `resume` 函数，驱动状态机继续。

**异步叶子原语**（如 `delay`、自定义 `fnc`）接入方式：创建一个 future、登记进事件循环（如定时器挂入超时链表），在完成时调 `done` 兑现。

### 7.4 编译器落地

- parser：rpc 体扫描 `await` / com `<<` `>>` → `hasAwait` 标记
- codegen：`hasAwait` → 帧结构注入 `_ret/_state/_fut` + 局部提升 + `switch(_state)` 状态机 + 启动器 `__async` + 同步包装抑制
- 工程管线：op 单元始终入图（`op_impl.c` 始终链接），编译器不 emit 事件循环符号——事件循环是 op 的运行时，rpc 只是它的消费者

---

## 8. 设备通讯 `com`：协议驱动的 io 框架

> **机制框架**
> - 采用 vtable「协议对象」模式，实现设备自定义扩展和实现（→ §4.1）
> - 通过 `<<`、`>>` 流读写操作符实现 I/O 统一访问（→ §4.2）
> - 通过 sc 的分身/切片能力，实现定长/不定长数据的读取（→ §4.3）
> - 通过指定 `ioq` 对象、和实现 `readable`/`writable` 接口来启动和支持设备的异步访问能力（→ §4.4、§3.5）
> - 和语言的 async 异步机制整合，实现异步 I/O 的有序访问（→ §4.2 异步展开部分、§3.5 事件循环）
> - 内置 O(1) 性能的多路复用就绪状态通知能力，配合 async 异步机制（→ §4.5）

### 8.1 类型结构：com 的 vtable 布局

`com` 结构体通过一组成员函数的接口，即 `fnc name:` 字段（单冒号、无函数体）的 MethodPtr 槽，实现具体设备 I/O 访问的抽象：

```sc
@def com: <limit> {
    dev: &                              # 设备句柄（设备相关）
    rq: ioq&                            # 读队列（nil=不支持异步读）
    wq: ioq&                            # 写队列（nil=不支持异步写）

    # alloc 参数即 com[...] 句柄上下文：var s: com[size, ending]。
    # size/ending 透传构造 limit；缓冲分配等策略由用户 alloc 实现自定。
    fnc alloc: limit&, size: u4, ending: &   # 分身构造：隐藏接收者 com&，返回 limit&
    fnc free: s: limit&                      # 分身析构

    fnc read: ret, data: &, size: u4&   # 设备读（每对象 io 实现，隐藏接收者 com&）
    fnc write: ret, buf: &, size: u4&   # 设备写（每对象 io 实现）
    fnc error: ret                      # 错误回调（每对象）
    fnc readable: ret, id: &&           # 读就绪查询（多路复用探测，见上契约）
    fnc writable: ret, id: &&           # 写就绪查询（语义对称）
    fnc close: ret                      # 关闭设备：释放底层资源（nil=无需关闭，OS 回收）
}
```

### 8.2 编译层：`<<`、`>>` 的识别与展开

`<<` 和 `>>` 在 parser 中是普通二元运算符（`Expr::Binary`，左结合），**parser 对 com 零知识**。识别发生在 codegen 的表达式 emit 阶段，该阶段会根据操作的目标对象来执行不同操作，具体为：

- 如果目标对象是 rpc 调用
  逐字段装填/读出参数结构体 → 调用 rpc 处理

- 如果目标对象是一个 com 分身边界句柄
  进入框架确定读流程，反复调用 com 的 read 接口，读入缓冲，按定长/动态终止条件判定截止

- 其他情况
  直接调用 com 的 read/write 方法指针，一次收发 sizeof(目标) 个字节

以上是在同步上下文（fnc 中）的执行形态

如果是在异步上下文（rpc 中），每个（`<<`、`>>`）收发点变成一个（`await`）状态机切点：
- com 为此提供三个桥接 API：`com_read_async`、`com_write_async`、`com_limit_read_async`
  把一次设备 io 封装成可被 await 的 future，使 rpc 状态机能以统一的 await 协议挂起/恢复。

- 设备 io 的就绪探测与兑现由 `async_io()` 驱动
  遍历登记了的 com io 待办，探测就绪（readable/writable）后执行实际收发并兑现对应 future，接回状态机继续。

### 8.3 通过 sc 的分身/切片能力，实现有界数据的读取

`com` 是基于 sc 的分身容器类型（`def com: <limit>`），即 com 可借 `limit` 分身边界语法生成一个有界读视图（`com[...]` 句柄）。这里的 `limit` 对象和 `com` 一样，也是一个基于协议的结构——它的行为由用户提供的 `limit` 实现实例来定义：
| 成员 | 类型 | 含义 |
|------|------|------|
| `size` | `u4` | 定长模式=数据总大小；动态模式=每次最大 chunk |
| `len`  | `u4` | 框架回写：已累计读取的字节数 |
| `data()` | MethodPtr | 返回读缓冲基址（`alloc` 里分配；写入起点 `data() + len`） |
| `ending()` | MethodPtr | 动态截止判定（可 nil）。每读一段回调，≥0 = 命中（`len` 设为其值），<0 = 继续 |

- 用户通过 `com` 的 `alloc()` 接口来返回自定义实现的 `limit` 实例
- 通过 `limit` 实例的 `data()` 接口来返回读缓冲基址
- 通过 `ending()` 接口实现不定长截止边界的定义，该接口为 nil 表示定长读取
  - 接口返回 ≥0 命中截止（如 HTTP `\r\n\r\n` 可在 ending 内扫描缓冲）
  - 接口返回 <0 继续读

框架拿到这些接口信息后，经 `limit_read` 执行读循环，反复调用 com 的 read 接口读入 `data() + len`，按 `ending` 判停或定长读满即止。框架不内置任何协议解析——所有缓存/边界策略全在用户的实现里。

### 8.4 ioq：读写缓存队列 — 启动和支持异步 I/O

com 的异步能力由 `rq` / `wq` 两个 ioq 字段启用：非 nil 即表示该 com 支持对应方向的异步访问。ioq 本质是一个自动扩容的循环缓冲，把 io 操作从"立即执行"变成"入队等待"——调用方（`<<`、`>>`）推送一段待收发数据进队后即可返回，由事件循环在设备就绪时再取出执行。

ioq 队列中每条目由调用方经 `push` 写入，事件循环经 `pull` 取出执行：

- **io 缓冲项** `[size, buf]`：`size ≠ 0` 时表示一段待 io 的数据——`buf` 为数据地址，`size` 为字节数。`pull` 取出后按队列方向（rq=读/wq=写）调用 com 的 `read` 或 `write` 方法指针，把数据从设备读入 `buf` 或从 `buf` 写入设备
- **完成回调项** `[0, cb, data]`：`size = 0` 时表示一个待调用的回调——`cb` 为函数地址，`data` 为其参数。`pull` 取出后直接调用 `cb(data)`，通常用于通知上层某段异步 io 已完成

### 8.5 readable / writable — 是否支持 O(1) 多路复用的关键

设备是否支持 O(1) 多路复用（epoll/kqueue/IOCP/poll），由 com 的 `readable` / `writable` 两个方法指针决定。事件循环在等待 io 就绪时调用它们探测设备状态：

- 若设备能给出可被多路复用后端监听的句柄（如 socket fd），`readable` 通过出参 `id` 返回该句柄 → 事件循环将其注册进 epoll/kqueue/poll，由内核在设备就绪时 O(1) 唤醒
- 若设备无法给出监听句柄（如纯内存模拟设备），`readable` 将出参 `id` 置 nil，事件循环改为按返回值轮询：`1` = 已就绪可立即 io，`0` = 仍未就绪稍后重试，`<0` = 出错

`async_io()` 是设备 io 就绪的驱动入口：遍历登记了待办 io 的 com，经上述探测确认就绪后，驱动 ioq 队首的收发，完成后兑现对应的 future 接回异步状态机。

### 8.6 架构分层

com 机制的完整链路，从上到下四层清晰分离：

- **声明层**（`op.sc`，默认导入）：`com` / `limit` / `ioq` 的 sc 类型布局 + 方法协议。compiler 只认识 `com` 这个类型名和 `<<`/`>>` 的展开规则，parser 和 semantic 对 com 零额外知识
- **编译层**（codegen）：识别 `<<`/`>>` 链（仅判断最左操作数类型是否为 `"com"`），按目标形态展开为方法调用或异步切点
- **运行时层**（`op_impl.c`，始终链接）：`limit_read` 框架读循环、ioq 管理、异步 io 桥接 API（`com_read_async` 等）、多路复用事件循环
- **设备层**（用户实现，如 `builtins/io/`）：提供 `com` 的方法指针——`read`/`write`/`readable`/`writable`/`close`/`error`

---

## 9. 类机制 `cls` / `dim`：分派器 + 维度

> **机制框架**
> - 每个 `cls` 对象首部注入一个分派函数指针 `_class`，
> - 所有 `dim` 折叠进唯一分派器的 `switch` 分支（→ §3.1）
> - 维度名是全局选择子——同名 `dim` 跨类即同一消息，语义检查强制参数签名一致（→ §3.1、§2.5）
> - `tril` 三态基础类型 + `object` 类型擦除引用 + `instanceOf` O(1) 身份判定（→ §3.2）
> - 五个保留维度（CLS_ID/REF/DROP/OBJ_KEY/OBJ_NAME/RLT_KEY/RLT_NAME）与用户 dim 选择子从 7 起（→ §3.3）
> - `_class` 在四种构造点自动安装（栈/堆/T@/全局），跨单元 class.h 聚合保证编号一致（→ §3.4、§2.5）

### 9.1 概念模型

非传统 vtable + 继承，而是「**单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除引用**」：

1. 每个 `cls` 对象首部注入**一个**分派函数指针 `_class`（synthetic 成员，复用 def 结构体全部机制）
2. 所有「方法」即维度 `dim`，折叠进该类**唯一**分派器 `T_hyper_impl` 的 `switch(dim_id)`
3. 维度名是**全局选择子**——同名 `dim` 跨类即同一消息 → 天然多态；语义检查强制参数签名一致
4. 维度恒返回三态 `tril`（应答/不应答/否定），真正输出经指针出参回填

### 9.2 基础类型与语法

**tril**：新增基础类型（与 `bool` 同级），C 落 `int8_t`。三个内置字面量：
`negative`(-1)、`unknown`(0)、`positive`(+1)。

```sc
cls Cat: {                          # 完全复用 def 结构体全部机制
    age: i4
    dim SPEAK: tril, buf: char&, cap: i4   # 返回恒 tril；真正输出经指针出参回填
        snprintf(buf, cap, "喵")
        return positive
}
```

**object**：指向任意类对象 `_class` 槽的指针（`typedef sc_hyper* object`），既是身份又是分派入口。
特殊强转 `(o: object)` ≡ `&o._class`。

**instanceOf**：`instanceOf(o, T) → bool`，O(1)，`*o == T_hyper_impl`。

### 9.3 保留维度（选择子 0..6）

| id | 维度 | 默认行为 |
|----|------|---------|
| 0 | `CLS_ID` | 写 `SC_CLS_<T>`（不可覆盖） |
| 1 | `REF` | 写目标 `sc_ref` 头地址，供 `object@` 反推引用头（不可覆盖） |
| 2 | `DROP` | 调本类析构 `T_drop`；无 `drop` 则 unknown（不可覆盖） |
| 3 | `OBJ_KEY` | 有 `obj_key` 字段→取字段值，否则取对象基址（可覆盖） |
| 4 | `OBJ_NAME` | 有 `obj_name` 字段→`%s`，否则 `snprintf "<T>@%p"`（可覆盖） |
| 5 | `RLT_KEY` | 与另一同类对象比对 key，按地址比大小 → 三态（可覆盖） |
| 6 | `RLT_NAME` | 与另一同类对象比对 name，`strcmp` → 三态（可覆盖） |

用户 dim 选择子从 **7** 起。工程模式下取所有单元的类名/维度名并集，生成共享 `class.h`
（`tril`/`sc_hyper`/`object` 类型 + `SC_CLS_`/`SC_DIM_` 枚举），各 `.c` `#include` 之；
单文件走内联模式。

### 9.4 调用分派与构造点

- **静态接收者** `o.Dim(args)`（`T`/`T&`/`T@`）→ `T_hyper_impl(&o._class, SC_DIM_Dim, args)`
- **动态接收者** `ob.Dim(args)`（`object`）→ `(*ob)(ob, SC_DIM_Dim, args)`

`_class` 在四种构造点自动安装：栈实例（声明处，早于 `init`）、堆构造 `T()`、
自动指针 `T@`、全局实例（模块 init 序言）。

### 9.5 编译器实现

- `cls` → `StructD` + `isClass=true` + synthetic `_class` 成员
- `dim` → `FuncD` + `isDim=true`；不单独生成函数，折叠进分派器
- 工程模式跨单元聚合 → 共享 `class.h`（编号一致）
- `instanceOf` 降解为 `(*o) == T_hyper_impl`
- 语义检查：同名 dim 参数签名须一致；`base(o)` 对 cls 跳过 `_class`

---

## 10. 依赖图 token `tok`：依赖图 + 工作流引擎

> **机制框架**
> - 以字符串 id 唯一标识的共享量，create-or-get 语义，设值即广播（→ §5.1）
> - 三种身份：form 候选（带 combine 合成）/ enforce 纯从（直接赋值）/ 未 form（挂起队列）（→ §5.1）
> - 边拓扑 × 门逻辑正交：普通 dep / map DAG 边 / loop 反馈边 / back 反向遍历，各自独立组合 all 与门 / any 或门（→ §5.2）
> - map 图编译期环检测 + 图度量烘焙为字面量常量，运行时 O(1) 查表（→ §5.2）
> - `'/'` 前缀多线程细粒度无锁：每 token seqlock + 每 dep 自旋锁 + follow 锁外运行（→ §5.3）
> - DNN 三机分离：forward（map+pulse）→ backward（back loss）→ train（for 循环 + 优化器）（→ §5.4）
> - 声明降解为隐藏函数 + 注册注入 main/模块 init 序章；op→tok 隐式依赖（→ §5.5）

### 10.1 概念模型：共享量 + 依赖演变

以字5符串 id 唯一标识的**共享量**。声明是「create-or-get」——首次创建、后续幂等返回
（adt 哈希 dict 按 id intern）。语义：「设值即广播、依赖即重算」。

**三种身份：**

| 身份 | combine | 效果 |
|------|---------|------|
| form 候选 | 有 | 输入经 combine「合成」再落值（去抖/取峰/累加） |
| enforce 纯从 | 无 | `set` 直接赋值 |
| 未 form | 任意 | `set` 仅入挂起队列，暂不传播 |

**变更检测**：同值静默丢弃。`pulse` 脉冲绕过抑制。`tok_modified()` 哨兵在 combine 体内强制传播。

### 10.2 依赖图：边拓扑 × 门逻辑（正交）

```
边拓扑（怎么传播）              门逻辑（何时触发）
┌──────────────┐          ┌──────────────┐
│ (无) 普通 dep  │          │ all 与门      │
│ map  DAG 边   │    ×     │ any 或门      │   —— 任意组合皆合法
│ loop 反馈边   │          │               │
│ back 反向遍历 │          │               │
└──────────────┘          └──────────────┘
```

**`dep…map`**：源与目标间显式有向边，构成 DAG。编译期环检测（三色 DFS），图度量烘焙为
字面量常量（depth/critical/slack/fanin/fanout/reach/batch/checkpoint/dom_size），运行时 O(1) 查表。

**`dep…loop`**：反馈边，豁免环检测。Tarjan SCC 缩减点，`loop_run(max)` 迭代驱动。

**`back`**：自输出 token 沿反向邻接按反拓扑序遍历；边反向（`TOK_BACK` follow）与节点 drain
（`exec` 钩子）自动分派。

**`exec` 钩子 + `ctx` 侧车**：`form t, v, &node, exec` 绑定节点上下文与统一处理钩子，推送/拉取两模式共用。

### 10.3 多线程：`'/'` 前缀细粒度无锁

id 以 `'/'` 前缀 → MT 模式：值用每-token **seqlock**（读无锁乐观重试），
依赖门用每-dep **自旋锁**（仅护门计数），`follow` 在释放所有锁后调用——零持锁跑用户码。
combine 须纯（锁内不得 set/get 其他 token）。非 MT token 零原子开销。

### 10.4 DNN 三机分离（模板落地）

| 机制 | 语法 | 职责 |
|------|------|------|
| 前向 forward | `dep…map` DAG + `pulse` | 输入→隐层→输出→loss，每条边 follow = 该层前向算子 |
| 反向 backward | `back loss` | 反拓扑序逐层算梯度（`TOK_BACK` 分支），链式法则 |
| 训练 train | `for` + `pulse` + 优化器 | pulse 驱前向 → backward → 更新权重 |

权重 `w` 是参数 token（不在 map 图），层 follow 读 `w->get()`，反向算出 `gw->set(grad)`。

### 10.5 编译器实现

- `tok` → 隐藏 combine 函数 + `var t: token&`（`isTok`，记 `tokId`/`tokFn`）
- `dep` → 隐藏 follow 函数 + `DepD`（门/项/蹦床）+ 图度量烘焙
- `form`/`back` → `FormS`/`BackS` 语句
- 注册注入 `main` 序章/模块 `init`；op→tok 隐式依赖（编译器自动纳入）
- 运行时见 `builtins/tok/`（`token_*` 函数 + seqlock + 图度量字面量查询）

------

## 11. 内存分配：`sc_alloc` 间接层 + chunk 池 + 两套金丝雀

> 本机制回答“一个 `T()` 堆对象的字节从哪来、经过几层、越界怎么被抓”。它与 §1 内存安全检查共享“金丝雀”概念，但分配路径本身独立成体系。

### 11.1 三层分配接口

sc 的堆分配分三层，从上到下语义递进：

| 接口（`op.h`/`mem.h`） | 默认行为 | 说明 |
|------|----------|------|
| `sc_alloc` / `sc_realloc` / `sc_free` | 宏直通 libc `malloc`/`realloc`/`free`（零开销） | **可切换间接层**。定义 `-DSC_POOL` 后转发到 chunk 三件套 |
| `sc_chunk` / `sc_chunk0` / `sc_chunk_array` | `malloc` / `calloc` / `calloc(count,size)`（带溢出检查） | **恒走 mem 池**，不受 `SC_POOL` 影响 |
| `sc_refit` / `sc_recycle` | `realloc` / `free` 语义（池内 refit/回收） | chunk 的重整与回收，与上面配套 |

- **默认（无 `-DSC_POOL`）**：`sc_alloc` 一族是纯宏，直接展开为 libc 调用，编译产物零额外开销。
- **`-DSC_POOL`**：`sc_alloc` 一族转为函数（`op_impl.c` 内 `#ifdef SC_POOL`），转发到 `sc_chunk`/`sc_refit`/`sc_recycle`，让所有 `T()` 堆对象统一走池。
- `sc_chunk` 一族**永远走 mem 池**，与 `SC_POOL` 无关——它是显式池接口。`sc_chunk0` 分配即清零；`sc_chunk_array(count,size)` 带乘法溢出检查，溢出返 `NULL`。
- **mem 恒被链接**：`op` 隐式依赖 `mem`，而 `op` 又默认导入所有 `.sc`，所以 chunk 池的运行时始终在场，`-DSC_POOL` 只是把默认分配路径切过去。

### 11.2 chunk 池实现要点（`builtins/mem/mem_impl.c`）

无锁、每线程的 size-class 分配器：

- **热路径无锁**：每线程持有各 size-class 的空闲链表，分配即从对应链表弹出、释放即压回，本线程内全程无原子操作。
- **size-class 分档**：小对象按档位（16 B 起、页 64 KiB 切分）对齐取整；超大/超对齐对象走哨兵 owner 直连 libc。
- **对象头** `mem_block{owner,info}`：已分配时 `owner` 指向物主堆，空闲时复用为链接指针，大对象用哨兵标记。
- **跨线程回收（MPSC）**：线程 A 分配、线程 B 释放时，B 不能直接动 A 的链表——它把块经**物主堆的 MPSC 原子单链表**（CAS 压入 + 单消费者 `exchange` 取整链）交还，A 下次分配时批量回收。全程无锁、无跨线程数据竞争。

### 11.3 codegen 分配落点

胖 `T@` 堆对象**默认走 chunk 池**；`T<raw>()` 语法退化为裸分配（仅胖指针支持）。释放由运行时 `sc_fat_on_zero_d` 按 `sc_ref.flags` 三路分派：

| 场景 | 分配 | flags | 释放 |
|------|------|------|------|
| `T()` 胖 `T@`（默认） | `sc_chunk(SC_REF_HDR + sizeof T)` | 0 | `sc_recycle`（池回收） |
| `T<raw>()` 胖 `T@` | `sc_alloc(SC_REF_HDR + sizeof T)` | `SC_REF_RAW` | `sc_free`（间接层） |
| `T<raw>()` + `--check=mem` | 裸 `malloc(SC_CANARY + SC_REF_HDR + sizeof T + SC_CANARY)` | `SC_REF_RAW\|SC_REF_CANARY` | `sc_canary_free`（校验头尾哨兵） |
| `T<atom>()` 胖 `T@` | 同默认（chunk） | `SC_REF_ATOM` | `sc_recycle` |
| `T()` 非胖堆对象 / `T@1` 单例 / 字符串 | `sc_alloc` | — | `sc_free` |
| async / com `rpc` 帧 `_p` / 参数帧槽 / 数组后备 | `sc_chunk0` | — | `sc_recycle` |

> `T__new_ref(int32_t _flags)` 是 `static inline`，调用点传常量 `_flags`（`0`/`SC_REF_RAW`/`SC_REF_ATOM` 组合），内部 `if (_flags & SC_REF_RAW)` 分支在内联展开时常量折叠，零运行时开销。`--check=mem` 的 `T<raw>` **故意用裸 `malloc`**：要把块扩成 `[头哨兵|sc_ref 头|实体|尾哨兵]` 手动排布地址派生魔数，这套块算术无法套 chunk。非胖 `NAME&` / `T@1` / 字符串 / adt 仍走 `sc_alloc`/`sc_free`（不在本次改造范围）。

### 11.4 两套越界金丝雀（`--check=mem` 联动开启）

sc 有两套堆越界哨兵，分别守护 chunk 块与裸块；`--check=mem` 作为统一开关，**同时开启两者**：

| | 保护对象 | 载体与校验 |
|---|---|---|
| **chunk `MEM_DEBUG`** | chunk 池分配的块（默认 `T()` 胖 `T@` / `T<atom>` / rpc 帧） | 块尾预留 `MEM_TAIL` 写 `MEM_MAGIC_TAIL`；对象头 magic（ALLOC/FREE）捕获双重释放/野指针；`recycle`/`refit`/`mem_usable` 时校验 |
| **编译器 `SC_CANARY`** | 裸分配块（`T<raw>()`） | codegen 注入 `SC_CANARY` 头尾哨兵（地址派生魔数），`sc_ref.flags` 置 `SC_REF_CANARY`；释放走 `sc_canary_free` 校验 |

**关键：`--check=mem` 是开关，开启时自动联动两者。**

- `main.cpp` 在 `getMemCheck()` 且非裸机时，同时追加 `-fstack-protector-strong`（栈哨兵）+ `-DMEM_DEBUG`（令 chunk 块得尾金丝雀），并让 codegen 走 `SC_CANARY` 分支（守护裼块）。
- **职责分工**：chunk 块由 chunk 自带的 `MEM_DEBUG` 尾金丝雀负责（不依赖编译器 `SC_CANARY`）；裸块（`T<raw>`）由 codegen `SC_CANARY` 负责。
- **关闭时零开销**：无 `--check=mem` 时 `MEM_TAIL=0`，chunk 块末尾不加探测魔数，所有守护退化为空操作。
- `-DMEM_DEBUG` 会变更 chunk 块布局（增 `magic` + `MEM_TAIL`）；因生成 C 与内联的 `mem_impl.c` 同处一个翻译单元、用同一 `cflags` 编译，布局全程一致。

### 11.5 编译器实现

- `sc_alloc` 间接层：`builtins/op.h`（宏/函数声明 + `SC_REF_RAW` 位）+ `op_impl.c`（`#ifdef SC_POOL` 函数定义）
- chunk 池：`builtins/mem/mem.h` + `mem_impl.c`（size-class + MPSC + `MEM_DEBUG` 金丝雀）
- `T<raw>()` 语法：`parser.cpp`（`< raw >` 四-token 识别，同 `T<atom>`）→ `ast.h` Call 节点 `ctorRaw` → `codegen_c.cpp` `ctorRefFlags` 合并为 `_flags`
- 胖 `T@` 分配/释放：`codegen_c.cpp` `T__new_ref`（chunk 默认 / `SC_REF_RAW` 走 `sc_alloc` / `--check=mem` 裸块 `SC_CANARY`）+ `op_impl.c` `sc_fat_on_zero_d` 三路分派
- `--check=mem` 落点：`main.cpp`（`getMemCheck()` → 追加 `-fstack-protector-strong -DMEM_DEBUG`）

------

## 附录 A：各机制编译器落点

| 机制 | lexer | AST | parser | semantic | codegen |
|------|-------|-----|--------|----------|---------|
| T@ | — | `TypeRef::fat`/`autoFree`/`thin` | 类型解析 `@`/`@1`/`*` | `checkFatBoundaries` | `fatScopes` 退域清理 |
| cls/dim | `KwCls`/`KwDim` | `isClass`/`isDim` | `cls`→StructD | `tril`/`object` 类型检查 | `T_hyper_impl` + `class.h` |
| com | — | — | `<<`/`>>` = 普通 Binary | — | `emitComChain`/`emitComAwait`/`emitComRpc*` |
| queue | — | — | `run` 语句 | — | op 协议（vtable）+ 隐式依赖 |
| tok | `KwTok`/`KwDep`/`KwForm` | `isTok`/`depItems`/`FormS` | 合成 combine/follow | `FormS` 类型检查 | 注册注入 + 蹦床 + 烘焙 |
| 内存分配 | — | Call `ctorRaw` | `T<raw>` 四-token | `g_memCheck` 判定 | `T__new_ref`（chunk 默认 / `SC_REF_RAW` 裸分配 / canary）+ `sc_fat_on_zero_d` 三路释放 + chunk 池 |

## 附录 B：快速导航

- 内存安全检查 → §1
- 自动指针 T@/T*/T@1 → §2
- 链接 `~` → §3
- 分身和切片 `<S>` → §4
- 成员与容器 `<C,I>` → §5
- 线程协同 mt → §6
- 异步协程 rpc/future → §7
- 设备通讯 com → §8
- 类机制 cls/dim → §9
- 依赖图 tok → §10
- 内存分配（sc_alloc/chunk 池/金丝雀）→ §11

> 张量 `ts`、`adt` 等是**标准库**而非语言机制，见 [REFERENCE.md](REFERENCE.md)。
