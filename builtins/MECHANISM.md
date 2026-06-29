# sc 核心机制/原理

> 本文是 sc 语言内置核心机制的**统一规格文档**，与 [REFERENCE.md](REFERENCE.md)（模块参考总览）和
> [ROADMAP.md](ROADMAP.md)（开发路线图）并列为 builtins 的三大支柱文档。每个机制独立成章，重点是 **"怎么做、怎么实现、背后的技术和流程是什么"**，均从实际源码推导总结（不是用户手册式的功能罗列）。各机制自身的更细致题目见所属子模块目录。

---

## 机制全景

| 机制 | 语法入口 | 运行时载体 | 一句话 |
|------|----------|-----------|--------|
| **自动指针** T@ | `var p: T@`、`T()`、`&expr` | `T_fat {p,tar,own}` + `sc_ref {in,out}` | 双向引用图 + ARC 自动释放 + 释放点悬挂/泄露检测 |
| **类机制** cls/dim | `cls T:`、`dim D:`、`object` | `T_hyper_impl` 分派器 + `SC_DIM_*` 枚举 | 单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除 |
| **多线程** mt | `run rpc()`、`sync`/`async`/`post`、`session` | PORT 单收件箱 + queue/protocol + `g_mutex` | PORT 中枢 + sync 铁律 + 循环互锁本地回退 |
| **分布式 token** tok | `tok x: "id"`、`dep…map`、`form`、`back` | `token_bind/intern` + seqlock + 图度量烘焙 | 共享量 + 依赖图 DAG + 前向/反向/迭代三机分离 |
| **张量** ts | `ts_zeros(shape)`、`a->matmul(b)` | `ts_store`(refcount) + `tensor`(shape/strides) | numpy 存储-视图分离 + PyTorch 算子核 + BLAS 可选 |
| **设备通讯** com | `com << v`、`com >> s`、`com << rpc()` | `com`(vtable) + `limit`(有界读) + `ioq`(循环缓冲) | 协议驱动的设备 io + << >> 收发糖 + 六种展开模式 |

六大机制互相衔接：
- **T@ 胖指针** 是 tok 共享量的值载体（`@` 类型擦除，自描述胖指针）
- **cls/dim 类机制** 的 `object@`（类型擦除自动指针）基于 T@ 双向引用图，`SC_DIM_DROP` 经分派器析构
- **tok** 的 `'/'` 前缀多线程模式走 **mt** 的无锁原语（op 层 `sc_*` 原子 RMW + seqlock）
- **ts** 张量在 tok 依赖图中作为 `@` 值流动（DNN 模板的前向/反向算子读写张量 `@` 句柄）

---

## 1. 自动指针 `T@`：双向引用图与 ARC

### 1.1 概念模型：一条边，两端记账

一次指针赋值 `A.p = &B` 在生命周期上**同时约束两端**：
- **B 不能比这条边先死** — 否则 `A.p` 悬挂（约束在 B）
- **A 不能丢下这条边就死** — 否则 B 泄露（约束在 A）

sc 在两个端点各记一次账：

| 计数 | 含义 | 维护者 | 违反时 |
|------|------|--------|--------|
| `in`（入边数） | 多少指针指向本对象 | `tar` 字段（目标引用头） | `in>0` 时释放 → **悬挂** |
| `out`（出边数） | 本对象持有多少指针 | `own` 字段（持有者引用头） | `out>0` 时释放 → **未清理** |

**释放不变式**：对象可释放 ⟺ `in == 0 && out == 0` ⟺ 完全孤立节点。

对比传统方案：

| 方案 | 记账方向 | 能做 | 不能做 |
|------|---------|------|--------|
| C++ `shared_ptr` | 只记入边 | 自动释放 B | 抓不到 A 持有边的悬挂 |
| Rust | 借用实体化、编译期验证 | 全保证 | 概念重、数据结构转不了 C |
| **sc `T@`** | **一条边两端各记一次** | 自动释放 + 双向悬挂/泄露检测 | 不级联、不反向失效 |

### 1.2 类型系统：裸指针 vs 胖指针

两条**完全独立**的路径，胖指针 opt-in：

```sc
var raw: T&        # 裸指针，8 字节，纯 C 语义，不追踪
var ptr: T@        # 胖指针，24 字节 = {p, tar, own}，参与引用图
```

- **裸→胖**：禁止隐式（裸指针无 tar/own 信息）
- **胖→裸**：显式允许 `r: & = ptr`（取 `ptr.p`），转裸后退回纯 C，不可检测
- **半自动指针 `T@&`**：物理 `T*` + 退域 RAII 销毁（`drop + free`），unique_ptr 语义
- **裸 `@`**（类型擦除）：`sc_afat`（24B 同构 `sc_fat` + `dtor` 槽），用于通用容器

胖指针的 `own` 身份：

| 胖指针位置 | own |
|-----------|-----|
| 栈/全局根指针 | `SC_OWN_ROOT(-1)`，域退出自动拆 |
| 子成员，经胖 base 访问 | 最后一个胖 hop 所指对象的 `out` 地址 |
| 子成员，经裸 base 访问 | 可读/可 `=nil`；绑新边仅限 `init` 内经 `this` |
| `&` 取址结果赋给接收者 | 按接收者自身位置递归套用 |

### 1.3 引用图维护

**赋值/解绑**：
```
owner.p = ⌖B      # 先拆旧边(旧目标.in--, owner.out--)，再绑新边(B.in++, owner.out++)
owner.p = nil     # B.in--, owner.out--
```

**`&` 取址传染**：对胖目标的子成员取址 → 结果也是胖指针；`tar` 来自访问路径上最后一个胖 hop。

**`in→0` 即释放点**：先调 `drop`（若有），随后 `in==0 && out==0` → 自动 `free`（ARC）；
`out>0` → 报错"未清理"。`drop` 体内 `this->m = nil` 解绑子成员 → 逐层递归触发子对象 `in→0`。

### 1.4 释放点与验证

三种触发：(a) 域退出（先全部拆边再全部断言，两阶段）、(b) 堆对象 `in→0`（ARC）、
(c) 用户显式 `free`。

三个独立构建开关，默认关闭仅保留 ARC：

| 开关 | 覆盖 |
|------|------|
| `--check=ref` / `SCC_REF_CHECK` | 悬挂诊断 + 源码定位 |
| `--check=mem` / `SCC_MEM_CHECK` | 堆头尾 canary + 栈/全局数组尾哨兵 + `-fstack-protector-strong` |
| `--check=ptr` / `SCC_PTR_CHECK` | nil 解引用守卫 + 编译期已知维度数组下标越界 |

### 1.5 设计边界

| 能 | 不能 |
|----|------|
| 孤立堆对象 ARC 自动释放 | 不级联（不区分所属权；由 `drop` 驱动递归回收） |
| 释放点检测悬挂（in>0）并定位源码 | 不反向失效（目标死不自动置 nil 引用者） |
| 释放点检测未清理出边（out>0） | 不保证无泄漏（环须手工断边、忘记释放照漏） |
| 全 `T@` 闭包内安全 | `T@` 转裸后退回纯 C，不可检测 |

### 1.6 编译器硬约束

- 重新赋值必须先拆旧边；所有出口（return/goto/break）插入拆解
- 域退出两阶段（先拆边后断言）
- `T@` 数组：局部/全局（一维/多维）已实现；结构字段/参数/返回/tls 仍报错
- 跨 C ABI（导出/rpc/cImpl）禁止含 `T@`；sc 内部按值拷贝禁止内嵌 `T@`
- `goto` 跨作用域→编译期报错或注入清理

---

## 2. 类机制 `cls` / `dim`：分派器 + 维度

### 2.1 概念模型

非传统 vtable + 继承，而是「**单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除引用**」：

1. 每个 `cls` 对象首部注入**一个**分派函数指针 `_class`（synthetic 成员，复用 def 结构体全部机制）
2. 所有「方法」即维度 `dim`，折叠进该类**唯一**分派器 `T_hyper_impl` 的 `switch(dim_id)`
3. 维度名是**全局选择子**——同名 `dim` 跨类即同一消息 → 天然多态；语义检查强制参数签名一致
4. 维度恒返回三态 `tril`（应答/不应答/否定），真正输出经指针出参回填

### 2.2 基础类型与语法

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

### 2.3 保留维度（选择子 0..6）

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

### 2.4 调用分派与构造点

- **静态接收者** `o.Dim(args)`（`T`/`T&`/`T@`）→ `T_hyper_impl(&o._class, SC_DIM_Dim, args)`
- **动态接收者** `ob.Dim(args)`（`object`）→ `(*ob)(ob, SC_DIM_Dim, args)`

`_class` 在四种构造点自动安装：栈实例（声明处，早于 `init`）、堆构造 `T()`、
自动指针 `T@`、全局实例（模块 init 序言）。

### 2.5 编译器实现

- `cls` → `StructD` + `isClass=true` + synthetic `_class` 成员
- `dim` → `FuncD` + `isDim=true`；不单独生成函数，折叠进分派器
- 工程模式跨单元聚合 → 共享 `class.h`（编号一致）
- `instanceOf` 降解为 `(*o) == T_hyper_impl`
- 语义检查：同名 dim 参数签名须一致；`base(o)` 对 cls 跳过 `_class`

---

## 3. 多线程 `mt`：PORT + sync 铁律

### 3.1 概念模型：PORT 单收件箱

线程间是**每线程一个 PORT**——单一收件箱中枢：

- **port**：线程局部 `TLS g_port`，地址即稳定唯一身份。多个 `queue` 可 attach 到同一 port，
  消息归并进**单一收件箱**按优先级排成单链 → 跨多队列全局时序。
- **queue**：协议对象（vtable）。首次被某 port `pull` 时惰性 attach，积压消息整块插入收件箱首部。
- **全局唯一互斥 `g_mutex`**：保护所有 port 收件箱 + queue 暂存 + consumer/waiting 关系图——
  单锁消除一切锁序环。

### 3.2 sync 铁律（核心承诺）

> **一次 sync 一旦开始执行（PULLING），调用方必死等到 DONE/CLOSED，绝不中途放弃。**
> 调用方栈帧在执行期间始终有效，执行方可直接读写调用方栈上的参数与返回槽，
> 无需堆影子会话。

**循环互锁回退**：若 `sync` 的消费者恰是发送者自己（A→B→A 或 A 经队列回自己），
检测到循环依赖后在调用方线程本地直接执行 rpc 体（substitute），替代死锁。

### 3.3 消息投递三态

| 方式 | 语法 | 语义 |
|------|------|------|
| `sync` | `ret = queue.sync(rpc(...))` | 阻塞等返回值；铁律保障不中途放弃 |
| `async` | `f = queue.async(rpc(...))` | 返回 `future&`，`await f` 拿值；可跨线程 `done` |
| `post` | `queue.post(rpc(...))` | 投递即忘 |

### 3.4 延迟应答 `session`

一次 `sync` 的应答可延迟到 rpc 体返回之后、甚至**另一线程**才兑现：

```sc
fnc handler:: i4, s: session&
    s.done(ret)              # 延迟兑现——可在当前 rpc 体内、也可把 s 传给另一线程
```

与单线程 `future`（`async→future→done`）对称。调用方在兑现前一直阻塞（铁律）。

### 3.5 编译器边界

编译器只认识 `op.sc` 暴露的线程协议（vtable 函数指针）与少量 op 内核符号。对
`mutex/pool/queue/future/session` 一律经协议指针派发，绝不直接 emit `mt_*` 实现符号——
op 层为 mt 提供操作系统抽象，mt 实现为 op 协议的具体实现者，双向解耦。

---

## 4. 分布式 token `tok`：依赖图 + 工作流引擎

### 4.1 概念模型：共享量 + 依赖演变

以字符串 id 唯一标识的**共享量**。声明是「create-or-get」——首次创建、后续幂等返回
（adt 哈希 dict 按 id intern）。语义：「设值即广播、依赖即重算」。

**三种身份：**

| 身份 | combine | 效果 |
|------|---------|------|
| form 候选 | 有 | 输入经 combine「合成」再落值（去抖/取峰/累加） |
| enforce 纯从 | 无 | `set` 直接赋值 |
| 未 form | 任意 | `set` 仅入挂起队列，暂不传播 |

**变更检测**：同值静默丢弃。`pulse` 脉冲绕过抑制。`tok_modified()` 哨兵在 combine 体内强制传播。

### 4.2 依赖图：边拓扑 × 门逻辑（正交）

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

### 4.3 多线程：`'/'` 前缀细粒度无锁

id 以 `'/'` 前缀 → MT 模式：值用每-token **seqlock**（读无锁乐观重试），
依赖门用每-dep **自旋锁**（仅护门计数），`follow` 在释放所有锁后调用——零持锁跑用户码。
combine 须纯（锁内不得 set/get 其他 token）。非 MT token 零原子开销。

### 4.4 DNN 三机分离（模板落地）

| 机制 | 语法 | 职责 |
|------|------|------|
| 前向 forward | `dep…map` DAG + `pulse` | 输入→隐层→输出→loss，每条边 follow = 该层前向算子 |
| 反向 backward | `back loss` | 反拓扑序逐层算梯度（`TOK_BACK` 分支），链式法则 |
| 训练 train | `for` + `pulse` + 优化器 | pulse 驱前向 → backward → 更新权重 |

权重 `w` 是参数 token（不在 map 图），层 follow 读 `w->get()`，反向算出 `gw->set(grad)`。

### 4.5 编译器实现

- `tok` → 隐藏 combine 函数 + `var t: token&`（`isTok`，记 `tokId`/`tokFn`）
- `dep` → 隐藏 follow 函数 + `DepD`（门/项/蹦床）+ 图度量烘焙
- `form`/`back` → `FormS`/`BackS` 语句
- 注册注入 `main` 序章/模块 `init`；op→tok 隐式依赖（编译器自动纳入）
- 运行时见 `builtins/tok/`（`token_*` 函数 + seqlock + 图度量字面量查询）

---

## 5. 张量 `ts`：多维数组与数值计算

### 5.1 概念模型：存储-视图分离

```sc
@def tensor&: {          # 堆专属
    store:  &            # ts_store*：带 refcount 的连续缓冲
    shape:  i4&          # ndim 个维长
    strides: i4&         # ndim 个步长（可 0/负）
    offset: i8           # 起始元素偏移
    ndim:   i4           # 维数
    dtype:  i4           # DT_F32/DT_F64/DT_I32/DT_I64/DT_U8
    numel:  i8           # 元素总数
}
```

接口习惯对齐 numpy/PyTorch，以成员函数表达（`a->add(b)`，不引入运算符重载改动语法）。

**视图零拷贝**：`reshape/transpose/permute/squeeze/flip/slice/select/narrow` 等可行时返回
零拷贝视图（共享 `store`，refcount 保护），否则静默物化。**strides 广播**：被广播维 stride
置 0，二元/比较/where 不物化中间结果。

### 5.2 API 分类

**创建**：`ts_zeros`、`ts_ones`、`ts_full`、`ts_arange`、`ts_eye`、`ts_from_buf`
**形状**：`reshape`、`transpose`、`permute`、`squeeze`、`unsqueeze`、`ravel`、`slice`、`broadcast_to`
**二元**（广播）：`add`、`sub`、`mul`、`div`、`matmul`（批量，2D 可选 BLAS）
**逐元素**：`relu`、`sigmoid`、`tanh`、`exp`、`log`、`sqrt`、`neg`、`gelu`
**归约**：`sum`、`mean`、`max`、`min`、`argmax`（dim 参数；浮点用 double 累积）
**损失**：`mse_loss`、`cross_entropy_loss`（含反向核，C-ABI 供 nn 调用）
**访问**：`at`/`item`/`itemset`/`data`/`copy_`

### 5.3 数值稳定性

- softmax：减 max（防 exp 溢出）
- cross_entropy：`log_softmax` + NLL 两步（防 log(0)）
- sum/mean：浮点用 double 累积（防大数组截断）
- gelu：tanh 近似

### 5.4 BLAS 与编译器边界

`matmul` 2D：编译选项 `-DSCC_WITH_BLAS` → `cblas_sgemm`/`cblas_dgemm`，否则纯 C 实现。
编译器对张量无任何特殊知识——纯库模块。

---

## 6. 设备通讯 `com`：协议驱动的 io 框架

> **机制框架**
> - 采用 vtable「协议对象」模式，实现设备自定义扩展和实现（→ §6.1）
> - 通过 `<<`、`>>` 流读写操作符实现 I/O 统一访问（→ §6.2）
> - 通过 sc 的分身/切片能力，实现定长/不定长数据的读取（→ §6.3）
> - 通过指定 `ioq` 对象、和实现 `readable`/`writable` 接口来启动和支持设备的异步访问能力（→ §6.4、§6.5）
> - 和语言的 async 异步机制整合，实现异步 I/O 的有序访问（→ §6.2 异步展开部分、§6.5 事件循环）
> - 内置 O(1) 性能的多路复用就绪状态通知能力，配合 async 异步机制（→ §6.5）

### 6.1 类型结构：com 的 vtable 布局

`com` 在 C 侧是一个以函数指针为主的结构体（`op.h:466-478`），各 `fnc name:` 字段（单冒号、无函数体）即 MethodPtr 槽：

```c
// op.h — C ABI：sc 侧 def com: <limit> { ... fnc read: ... } 编译生成
struct com {
    limit *(*alloc)(com *_this, uint32_t size, void *ending);  // 分身构造
    void  (*free)(com *_this, limit *s);                        // 分身析构
    int32_t (*read)(com *_this, void *data, uint32_t *size);    // 设备读（*size in/out）
    int32_t (*write)(com *_this, void *buf, uint32_t *size);    // 设备写
    int32_t (*error)(com *_this);
    int32_t (*readable)(com *_this, void **id);   // 读就绪探测：*id=可监听句柄或nil
    int32_t (*writable)(com *_this, void **id);   // 写就绪探测
    int32_t (*close)(com *_this);
    void   *dev;                  // 设备专属不透明句柄
    ioq    *rq;                   // 读缓冲队列（nil=不支持异步读）
    ioq    *wq;                   // 写缓冲队列
    limit  *_self;                // 容器注入：分身回指本体（<limit> 机制）
};
```

`<limit>` 容器机制使 com 同时是**分身/切片的主体**：`var s: com[size, ending]`（`T[...]` 句柄语法）→ `s = com` 展开为 `s._ = com.alloc(&com, size, ending); s._->_self = &com`。`s._` 即注入的 `limit` 节点，承载有界读视图。

### 6.2 编译层：`<<` / `>>` 的识别与展开

`<<` 和 `>>` 在 parser 中是普通二元运算符（`Expr::Binary`，左结合），**parser 对 com 零知识**。识别发生在 codegen 的表达式 emit 阶段。

**识别（comChain）**：从最外层二元表达式自顶向下剥离 `<<` / `>>` 链，倒序收集 `ComOp {target, send}`（右操作数 + 方向），剥到最左端时检查其类型是否为 `com`。若是 → 将其作为基址、逐个展开 ops。若不是 → 退化为普通移位运算。

```c
// codegen_c.cpp:3632 — ComOp 内部结构（codegen 局部，不出现在 AST）
struct ComOp { const Expr* target; bool send; };
// send=true → << 发（write），send=false → >> 收（read）
```

**同步展开（emitComChain，`codegen_c.cpp:3657`）** 按右操作数三种形态分派：

| 右操作数形态 | 识别方式 | 产出的 C 代码 |
|-------------|---------|-------------|
| rpc 调用/名 | `comRpcTarget()`：发端=Call（带实参），收端=Ident（裸 rpc 名） | 逐字段装填/读出参数结构体 → 调用 rpc 处理 |
| com[...] 句柄 | `projEntityOf(name) == "com"`（分身类型为 com） | `limit_read(&com, s._)` — 框架确定读循环 |
| 普通变量 v | 其余 | `com.method(&com, &v, &_scsz)` — 直接收发 sizeof(v) |

普通变量的展开（模式 A）：
```c
{ uint32_t _scsz;
  _scsz = sizeof(v);                    // 期望字节数
  com.write(&com, (void *)&v, &_scsz);  // io 执行；_scsz 回写实际收发字节数
}
```

rpc 序列化收发（模式 C）：发端先装填参数结构体 `struct rpc _rp = {0}`，再逐字段 `com.write(&com, &_rp.field, &sz)`；收端先 `com.read(&com, &_rp.field, &sz)` 再 `rpc_rpc(&_rp)` 触发处理。字段顺序即收发协议。

**异步展开（emitComAwait，`codegen_c.cpp:5738`）**：rpc 含 `await` 时已整体编译为状态机（`_p->_state`），com 收发点成为状态机的一个切点。核心三段式：

```c
// codegen_c.cpp:5769 — 异步收发点的通用模板
_p->_fut = com_read_async(&com, &v, sizeof(v));            // 1. 发起异步 io
if (future_await(_p->_fut, _p, (void(*)(void*))rpc_rpc))  // 2. 已就绪→goto 恢复点
    goto _s<N>;
_p->_state = <N>; return;                                  // 3. 未就绪→保存stage+让出
_s<N>: ;                                                   // 恢复点标签
```

与普通 `future`（值通过 `future_get` 返回）的区别：com io 的 `data` 参数指向调用方变量（`&v`），
io 就绪后**直接写入该地址**。future 兑现只作完成信号（兑现值=收发字节数），数据本身已原地回写。
状态机从 `_s<N>` 恢复后，`v` 已包含读出数据。

rpc 异步序列化（模式 F）：参数帧堆分配 `_p->_crpc<N> = calloc(...)` 跨 await 切点存活，
逐字段 `com_write_async await` 写出后 `free`。

### 6.3 运行时：limit_read — 框架确定读循环

`limit_read`（`op_impl.c:377`）是 com 机制中唯一的「框架确定」逻辑——一个不变的读循环，策略全部外移给用户的 `data()` / `ending()` 方法指针：

```c
// op_impl.c:377 — limit_read 的实际实现
int32_t limit_read(com *c, limit *s) {
    char *base = (char *)(s->data ? s->data(s) : NULL); // ← 用户提供缓冲基址
    for (;;) {
        uint32_t chunk;
        if (s->ending)  chunk = s->size;                // 动态：每次最多 size
        else { if (s->len >= s->size) return IO_EOF;    // 定长：读满即止
               chunk = s->size - s->len; }
        int32_t r = c->read(c, base + s->len, &chunk);  // ← 设备读（用户方法指针）
        if (r < 0) return r;                             // 不可恢复 → 中断
        if (r == IO_AGAIN) return IO_AGAIN;              // 异步挂起信号
        s->len += chunk;
        if (s->ending) {
            int32_t m = s->ending(s);                    // ← 用户判定截止
            if (m >= 0) { s->len = m; return IO_EOF; }
        } else if (s->len >= s->size) return IO_EOF;
        if (r == IO_EOF || chunk == 0) return IO_EOF;
    }
}
```

- `ending == nil` → 定长模式：读满 `size` 字节即 `IO_EOF`
- `ending != nil` → 动态模式：`size` 只是每次最大 chunk，每读一段回调 `ending(s)`；返回值 ≥0 = 命中，<0 = 继续
- 框架不做缓冲区管理、不做协议解析——HTTP `\r\n\r\n` 截止由用户在 ending 里扫描

### 6.4 运行时：ioq — 循环缓冲队列

`ioq`（`op.h:426` / `op_impl.c:319`）是 com 的可自动膨胀循环缓冲，以 `void*` 数组存储，`_head`/`_tail` 游标。每个 item 的编码（`ioq_pull:348`）：

- **io 缓冲项** `[size, buf]`：`size ≠ 0` → 按 rq/wq 方向调 `com.read/write(c, buf, &n)`，返回 `buf`
- **完成回调项** `[0, cb, data]`：`size = 0` → 调 `cb(data)`，返回 NULL

### 6.5 运行时：异步 io 的双后端架构

`op_impl.c` 内置两套异步后端，由编译选项 `SCC_WITH_UV` 切换，对外接口完全一致。

**自有后端**（默认，`op_impl.c:1279~1338`）：全局 `io_req` 单链表 + epoll/kqueue/IOCP/poll 多路复用：

```
com_read_async / com_write_async / com_limit_read_async
  → future_new() + calloc io_req + 挂入 g_io_reqs + wake()

事件循环 run_loop():
  → process_new_reqs: 遍历 g_io_reqs
      调 readable/writable(id):
        *id≠nil → mux_register(fd) 注册进 epoll/kqueue/poll
        *id=nil → 看返回值: 就绪→armed=1
  → mux_wait(timeout)            // 多路复用等待
  → execute_ready: 遍历 armed 的 io_req
      lim模式: limit_read() → IO_AGAIN? 重置等待 : 兑现
      普通:   c->read/write() → future_done(f, n)
  → fire_timers + drain_ready
```

**libuv 后端**（`-DSCC_WITH_UV`，`op_impl.c:746~815`）：`uv_poll_t` + `uv_timer_t` + `uv_run`。

### 6.6 全链路示例

以 `com >> x`（同步收一个 i4）为例：

1. **parser**：`com >> x` → `Binary(>>, Ident("com"), Ident("x"))` — parser 无 com 知识
2. **codegen·comChain**（`codegen_c.cpp:3637`）：剥离 `>>`，检测最左 `com` 是 com 类型 → `ops = [{&x, false}]`
3. **codegen·emitComChain**（`codegen_c.cpp:3689`）：x 是普通变量 → 展开为：
   ```c
   { uint32_t _scsz; _scsz = sizeof(x);
     com.read(&com, (void *)&x, &_scsz); }
   ```
4. **运行时**：`com.read` 是用户设备实现的方法指针（如 socket `recv`/文件 `read`），从 fd 读 sizeof(x) 字节进 x

以 `com << rpc(a, b)`（同步发 rpc 参数）为例：

1. **codegen·comChain**：`ops = [{Call(rpc,a,b), true}]`
2. **codegen·emitComRpcSend**（`codegen_c.cpp:3796`）：栈上声明 `struct rpc _rp = {0}`，装填实参，逐字段 `com.write(&com, &_rp.a, &sz); com.write(&com, &_rp.b, &sz)`
3. **运行时**：逐字段写进设备，对端 `com >> rpc` 对称读回后调 `rpc_rpc(&_rp)` 触发处理

### 6.7 架构分层

```
┌─ op.sc 声明层（默认导入）────────────────────────────┐
│  def com: <limit> { fnc read: ... fnc write: ... }    │  类型布局 + 方法协议
│  def limit: { fnc data: ... fnc ending: ... }          │  有界读视图
│  def ioq: { fnc push: ... fnc pull: ... }               │  循环缓冲
├─ codegen_c.cpp 编译层 ────────────────────────────────┤
│  comChain()  → 识别 << / >> 链（仅判断最左类型=="com"）│  语法糖识别
│  emitComChain/emitComAwait → 六种模式展开              │  代码生成
│  emitComRpcSend/Recv → rpc 参数逐字段序列化            │
├─ op_impl.c 运行时层（始终链接）──────────────────────┤
│  limit_read()         → 框架确定读循环                 │  设备无关驱动
│  ioq_push/pull()      → 循环缓冲 io 执行               │
│  com_read/write_async → 登记 io_req + future           │  异步桥接
│  process_new_reqs + execute_ready + mux_wait           │  事件循环
└─ 用户设备实现（应用层）───────────────────────────┘
   提供 com 的各方法指针，例如 builtins/io/ — file com
```

编译器对 com 的知识仅限于：`<<`/`>>` 链最左操作数的类型名是否为 `"com"`（`comChain` L3646）。parser 不解析 com 特有语法、semantic 不做 com 特有检查——设备逻辑与编译器彻底解耦。

---

## 附录 A：各机制编译器落点

| 机制 | lexer | AST | parser | semantic | codegen |
|------|-------|-----|--------|----------|---------|
| T@ | — | `TypeRef::fat`/`autoFree` | 类型解析 `@`/`@&` | `checkFatBoundaries` | `fatScopes` 退域清理 |
| cls/dim | `KwCls`/`KwDim` | `isClass`/`isDim` | `cls`→StructD | `tril`/`object` 类型检查 | `T_hyper_impl` + `class.h` |
| mt | — | — | `run` 语句 | — | op 协议（vtable）+ 隐式依赖 |
| tok | `KwTok`/`KwDep`/`KwForm` | `isTok`/`depItems`/`FormS` | 合成 combine/follow | `FormS` 类型检查 | 注册注入 + 蹦床 + 烘焙 |
| ts | — | — | — | — | 无（纯库模块） |
| com | — | — | `<<`/`>>` = 普通 Binary | — | `emitComChain`/`emitComAwait`/`emitComRpc*` |

## 附录 B：快速导航

- T@ 自动指针 → §1
- cls/dim 类机制 → §2
- mt 多线程 → §3
- tok 依赖图 → §4
- ts 张量 → §5
- com 设备通讯 → §6
