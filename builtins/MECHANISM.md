# sc 核心机制/原理

> 本文是 sc 语言内置核心机制的**统一规格文档**，与 [REFERENCE.md](REFERENCE.md)（模块参考总览）和
> [ROADMAP.md](ROADMAP.md)（开发路线图）并列为 builtins 的三大支柱文档。每个机制独立成章，重点是 **"怎么做、怎么实现、背后的技术和流程是什么"**，均从实际源码推导总结（不是用户手册式的功能罗列）。各机制自身的更细致题目见所属子模块目录。

---

## 机制全景

| 机制 | 语法入口 | 运行时载体 | 一句话 |
|------|----------|-----------|--------|
| **自动指针** T@ | `var p: T@`、`T()`、`&expr` | `T_fat {p,tar,own}` + `sc_ref {in,out}` | 双向引用图 + ARC 自动释放 + 释放点悬挂/泄露检测 |
| **类机制** cls/dim | `cls T:`、`dim D:`、`object` | `T_hyper_impl` 分派器 + `SC_DIM_*` 枚举 | 单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除 |
| **设备通讯** com | `com << v`、`com >> s`、`com << rpc()` | `com`(vtable) + `limit`(有界读) + `ioq`(循环缓冲) | 协议驱动的设备 io + << >> 收发糖 + 六种展开模式 |
| **多线程协同** queue | `run rpc()`、`sync`/`async`/`post`、`session` | PORT 单收件箱 + queue/protocol + `g_mutex` | PORT 中枢 + sync 铁律 + 循环互锁本地回退 |
| **分布式图** tok | `tok x: "id"`、`dep…map`、`form`、`back` | `token_bind/intern` + seqlock + 图度量烘焙 | 共享量 + 依赖图 DAG + 前向/反向/迭代三机分离 |
| **张量** ts | `ts_zeros(shape)`、`a->matmul(b)` | `ts_store`(refcount) + `tensor`(shape/strides) | numpy 存储-视图分离 + PyTorch 算子核 + BLAS 可选 |

六大机制互相衔接：
- **T@ 胖指针** 是 tok 共享量的值载体（`@` 类型擦除，自描述胖指针）
- **cls/dim 类机制** 的 `object@`（类型擦除自动指针）基于 T@ 双向引用图，`SC_DIM_DROP` 经分派器析构
- **tok** 的 `'/'` 前缀多线程模式走 **mt** 的无锁原语（op 层 `sc_*` 原子 RMW + seqlock）
- **ts** 张量在 tok 依赖图中作为 `@` 值流动（DNN 模板的前向/反向算子读写张量 `@` 句柄）

---

## 1. 自动指针 `T@`：双向引用图与 ARC

> **机制框架**
> - 一次指针赋值在两端各记一次账（`in` / `out`），释放点做双向验证（→ §1.1）
> - 裸指针 `T&` 和胖指针 `T@` 两条完全独立的路径，胖指针 opt-in（→ §1.2）
> - 赋值时先拆旧边再绑新边，`&` 取址沿胖路径传染（→ §1.3）
> - `in→0` 即 ARC 释放点：先 `drop` 再判 `out`，隔离节点自动 free、out>0 报未清理（→ §1.3）
> - 三套可选构建开关（ref/mem/ptr），默认关闭仅保留 ARC（→ §1.4）
> - 跨 C ABI、按值拷贝、goto 跨域等编译器硬约束（→ §1.6）

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

> **机制框架**
> - 每个 `cls` 对象首部注入一个分派函数指针 `_class`，所有 `dim` 折叠进唯一分派器的 `switch` 分支（→ §2.1）
> - 维度名是全局选择子——同名 `dim` 跨类即同一消息，语义检查强制参数签名一致（→ §2.1、§2.5）
> - `tril` 三态基础类型 + `object` 类型擦除引用 + `instanceOf` O(1) 身份判定（→ §2.2）
> - 五个保留维度（CLS_ID/REF/DROP/OBJ_KEY/OBJ_NAME/RLT_KEY/RLT_NAME）与用户 dim 选择子从 7 起（→ §2.3）
> - `_class` 在四种构造点自动安装（栈/堆/T@/全局），跨单元 class.h 聚合保证编号一致（→ §2.4、§2.5）

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

## 3. 设备通讯 `com`：协议驱动的 io 框架

> **机制框架**
> - 采用 vtable「协议对象」模式，实现设备自定义扩展和实现（→ §3.1）
> - 通过 `<<`、`>>` 流读写操作符实现 I/O 统一访问（→ §3.2）
> - 通过 sc 的分身/切片能力，实现定长/不定长数据的读取（→ §3.3）
> - 通过指定 `ioq` 对象、和实现 `readable`/`writable` 接口来启动和支持设备的异步访问能力（→ §3.4、§3.5）
> - 和语言的 async 异步机制整合，实现异步 I/O 的有序访问（→ §3.2 异步展开部分、§3.5 事件循环）
> - 内置 O(1) 性能的多路复用就绪状态通知能力，配合 async 异步机制（→ §3.5）

### 3.1 类型结构：com 的 vtable 布局

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

### 3.2 编译层：`<<`、`>>` 的识别与展开

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

### 3.3 通过 sc 的分身/切片能力，实现有界数据的读取

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

### 3.4 ioq：读写缓存队列 — 启动和支持异步 I/O

com 的异步能力由 `rq` / `wq` 两个 ioq 字段启用：非 nil 即表示该 com 支持对应方向的异步访问。ioq 本质是一个自动扩容的循环缓冲，把 io 操作从"立即执行"变成"入队等待"——调用方（`<<`、`>>`）推送一段待收发数据进队后即可返回，由事件循环在设备就绪时再取出执行。

ioq 队列中每条目由调用方经 `push` 写入，事件循环经 `pull` 取出执行：

- **io 缓冲项** `[size, buf]`：`size ≠ 0` 时表示一段待 io 的数据——`buf` 为数据地址，`size` 为字节数。`pull` 取出后按队列方向（rq=读/wq=写）调用 com 的 `read` 或 `write` 方法指针，把数据从设备读入 `buf` 或从 `buf` 写入设备
- **完成回调项** `[0, cb, data]`：`size = 0` 时表示一个待调用的回调——`cb` 为函数地址，`data` 为其参数。`pull` 取出后直接调用 `cb(data)`，通常用于通知上层某段异步 io 已完成

### 3.5 readable / writable — 是否支持 O(1) 多路复用的关键

设备是否支持 O(1) 多路复用（epoll/kqueue/IOCP/poll），由 com 的 `readable` / `writable` 两个方法指针决定。事件循环在等待 io 就绪时调用它们探测设备状态：

- 若设备能给出可被多路复用后端监听的句柄（如 socket fd），`readable` 通过出参 `id` 返回该句柄 → 事件循环将其注册进 epoll/kqueue/poll，由内核在设备就绪时 O(1) 唤醒
- 若设备无法给出监听句柄（如纯内存模拟设备），`readable` 将出参 `id` 置 nil，事件循环改为按返回值轮询：`1` = 已就绪可立即 io，`0` = 仍未就绪稍后重试，`<0` = 出错

`async_io()` 是设备 io 就绪的驱动入口：遍历登记了待办 io 的 com，经上述探测确认就绪后，驱动 ioq 队首的收发，完成后兑现对应的 future 接回异步状态机。

### 3.6 架构分层

com 机制的完整链路，从上到下四层清晰分离：

- **声明层**（`op.sc`，默认导入）：`com` / `limit` / `ioq` 的 sc 类型布局 + 方法协议。compiler 只认识 `com` 这个类型名和 `<<`/`>>` 的展开规则，parser 和 semantic 对 com 零额外知识
- **编译层**（codegen）：识别 `<<`/`>>` 链（仅判断最左操作数类型是否为 `"com"`），按目标形态展开为方法调用或异步切点
- **运行时层**（`op_impl.c`，始终链接）：`limit_read` 框架读循环、ioq 管理、异步 io 桥接 API（`com_read_async` 等）、多路复用事件循环
- **设备层**（用户实现，如 `builtins/io/`）：提供 `com` 的方法指针——`read`/`write`/`readable`/`writable`/`close`/`error`

---

## 4. 多线程 `mt`：PORT + sync 铁律

> **机制框架**
> - 每线程一个 PORT 收件箱中枢，全局单锁消解所有锁序环（→ §4.1）
> - sync 铁律：一旦开始执行必死等到底，栈帧始终有效，无需堆影子会话（→ §4.2）
> - 循环互锁时自动本地回退执行（substitute），替代死锁（→ §4.2）
> - 三种消息投递：sync 阻塞取返回值 / async 拿 future / post 投递即忘（→ §4.3）
> - session 延迟应答：sync 的返回可推迟到另一线程兑现，与 future 对称（→ §4.4）
> - 编译器经 op 协议指针派发，零 emit mt 实现符号——op 与 mt 双向解耦（→ §4.5）

### 4.1 概念模型：PORT 单收件箱

线程间是**每线程一个 PORT**——单一收件箱中枢：

- **port**：线程局部 `TLS g_port`，地址即稳定唯一身份。多个 `queue` 可 attach 到同一 port，
  消息归并进**单一收件箱**按优先级排成单链 → 跨多队列全局时序。
- **queue**：协议对象（vtable）。首次被某 port `pull` 时惰性 attach，积压消息整块插入收件箱首部。
- **全局唯一互斥 `g_mutex`**：保护所有 port 收件箱 + queue 暂存 + consumer/waiting 关系图——
  单锁消除一切锁序环。

### 4.2 sync 铁律（核心承诺）

> **一次 sync 一旦开始执行（PULLING），调用方必死等到 DONE/CLOSED，绝不中途放弃。**
> 调用方栈帧在执行期间始终有效，执行方可直接读写调用方栈上的参数与返回槽，
> 无需堆影子会话。

**循环互锁回退**：若 `sync` 的消费者恰是发送者自己（A→B→A 或 A 经队列回自己），
检测到循环依赖后在调用方线程本地直接执行 rpc 体（substitute），替代死锁。

### 4.3 消息投递三态

| 方式 | 语法 | 语义 |
|------|------|------|
| `sync` | `ret = queue.sync(rpc(...))` | 阻塞等返回值；铁律保障不中途放弃 |
| `async` | `f = queue.async(rpc(...))` | 返回 `future&`，`await f` 拿值；可跨线程 `done` |
| `post` | `queue.post(rpc(...))` | 投递即忘 |

### 4.4 延迟应答 `session`

一次 `sync` 的应答可延迟到 rpc 体返回之后、甚至**另一线程**才兑现：

```sc
fnc handler:: i4, s: session&
    s.done(ret)              # 延迟兑现——可在当前 rpc 体内、也可把 s 传给另一线程
```

与单线程 `future`（`async→future→done`）对称。调用方在兑现前一直阻塞（铁律）。

### 4.5 编译器边界

编译器只认识 `op.sc` 暴露的线程协议（vtable 函数指针）与少量 op 内核符号。对
`mutex/pool/queue/future/session` 一律经协议指针派发，绝不直接 emit `mt_*` 实现符号——
op 层为 mt 提供操作系统抽象，mt 实现为 op 协议的具体实现者，双向解耦。

---

## 5. 分布式 token `tok`：依赖图 + 工作流引擎

> **机制框架**
> - 以字符串 id 唯一标识的共享量，create-or-get 语义，设值即广播（→ §5.1）
> - 三种身份：form 候选（带 combine 合成）/ enforce 纯从（直接赋值）/ 未 form（挂起队列）（→ §5.1）
> - 边拓扑 × 门逻辑正交：普通 dep / map DAG 边 / loop 反馈边 / back 反向遍历，各自独立组合 all 与门 / any 或门（→ §5.2）
> - map 图编译期环检测 + 图度量烘焙为字面量常量，运行时 O(1) 查表（→ §5.2）
> - `'/'` 前缀多线程细粒度无锁：每 token seqlock + 每 dep 自旋锁 + follow 锁外运行（→ §5.3）
> - DNN 三机分离：forward（map+pulse）→ backward（back loss）→ train（for 循环 + 优化器）（→ §5.4）
> - 声明降解为隐藏函数 + 注册注入 main/模块 init 序章；op→tok 隐式依赖（→ §5.5）

### 5.1 概念模型：共享量 + 依赖演变

以字5符串 id 唯一标识的**共享量**。声明是「create-or-get」——首次创建、后续幂等返回
（adt 哈希 dict 按 id intern）。语义：「设值即广播、依赖即重算」。

**三种身份：**

| 身份 | combine | 效果 |
|------|---------|------|
| form 候选 | 有 | 输入经 combine「合成」再落值（去抖/取峰/累加） |
| enforce 纯从 | 无 | `set` 直接赋值 |
| 未 form | 任意 | `set` 仅入挂起队列，暂不传播 |

**变更检测**：同值静默丢弃。`pulse` 脉冲绕过抑制。`tok_modified()` 哨兵在 combine 体内强制传播。

### 5.2 依赖图：边拓扑 × 门逻辑（正交）

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

### 5.3 多线程：`'/'` 前缀细粒度无锁

id 以 `'/'` 前缀 → MT 模式：值用每-token **seqlock**（读无锁乐观重试），
依赖门用每-dep **自旋锁**（仅护门计数），`follow` 在释放所有锁后调用——零持锁跑用户码。
combine 须纯（锁内不得 set/get 其他 token）。非 MT token 零原子开销。

### 5.4 DNN 三机分离（模板落地）

| 机制 | 语法 | 职责 |
|------|------|------|
| 前向 forward | `dep…map` DAG + `pulse` | 输入→隐层→输出→loss，每条边 follow = 该层前向算子 |
| 反向 backward | `back loss` | 反拓扑序逐层算梯度（`TOK_BACK` 分支），链式法则 |
| 训练 train | `for` + `pulse` + 优化器 | pulse 驱前向 → backward → 更新权重 |

权重 `w` 是参数 token（不在 map 图），层 follow 读 `w->get()`，反向算出 `gw->set(grad)`。

### 5.5 编译器实现

- `tok` → 隐藏 combine 函数 + `var t: token&`（`isTok`，记 `tokId`/`tokFn`）
- `dep` → 隐藏 follow 函数 + `DepD`（门/项/蹦床）+ 图度量烘焙
- `form`/`back` → `FormS`/`BackS` 语句
- 注册注入 `main` 序章/模块 `init`；op→tok 隐式依赖（编译器自动纳入）
- 运行时见 `builtins/tok/`（`token_*` 函数 + seqlock + 图度量字面量查询）

---

## 6. 张量 `ts`：多维数组与数值计算

> **机制框架**
> - 存储-视图分离：连续缓冲 + refcount，reshape/transpose/slice 等零拷贝视图共享 store（→ §6.1）
> - 工厂创建（zeros/ones/arange/from_buf）+ 形变（reshape/permute/broadcast）+ 二元广播运算 + 逐元素 + 归约 + 损失（→ §6.2）
> - 数值稳定：softmax 减 max、cross_entropy 走 log_softmax、sum/mean 用 double 累积（→ §6.3）
> - matmul 可选 BLAS 加速，编译器对张量零特殊知识——纯库模块（→ §6.4）

### 6.1 概念模型：存储-视图分离

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

### 6.2 API 分类

**创建**：`ts_zeros`、`ts_ones`、`ts_full`、`ts_arange`、`ts_eye`、`ts_from_buf`
**形状**：`reshape`、`transpose`、`permute`、`squeeze`、`unsqueeze`、`ravel`、`slice`、`broadcast_to`
**二元**（广播）：`add`、`sub`、`mul`、`div`、`matmul`（批量，2D 可选 BLAS）
**逐元素**：`relu`、`sigmoid`、`tanh`、`exp`、`log`、`sqrt`、`neg`、`gelu`
**归约**：`sum`、`mean`、`max`、`min`、`argmax`（dim 参数；浮点用 double 累积）
**损失**：`mse_loss`、`cross_entropy_loss`（含反向核，C-ABI 供 nn 调用）
**访问**：`at`/`item`/`itemset`/`data`/`copy_`

### 6.3 数值稳定性

- softmax：减 max（防 exp 溢出）
- cross_entropy：`log_softmax` + NLL 两步（防 log(0)）
- sum/mean：浮点用 double 累积（防大数组截断）
- gelu：tanh 近似

### 6.4 BLAS 与编译器边界

`matmul` 2D：编译选项 `-DSCC_WITH_BLAS` → `cblas_sgemm`/`cblas_dgemm`，否则纯 C 实现。
编译器对张量无任何特殊知识——纯库模块。

---

## 附录 A：各机制编译器落点

| 机制 | lexer | AST | parser | semantic | codegen |
|------|-------|-----|--------|----------|---------|
| T@ | — | `TypeRef::fat`/`autoFree` | 类型解析 `@`/`@&` | `checkFatBoundaries` | `fatScopes` 退域清理 |
| cls/dim | `KwCls`/`KwDim` | `isClass`/`isDim` | `cls`→StructD | `tril`/`object` 类型检查 | `T_hyper_impl` + `class.h` |
| com | — | — | `<<`/`>>` = 普通 Binary | — | `emitComChain`/`emitComAwait`/`emitComRpc*` |
| queue | — | — | `run` 语句 | — | op 协议（vtable）+ 隐式依赖 |
| tok | `KwTok`/`KwDep`/`KwForm` | `isTok`/`depItems`/`FormS` | 合成 combine/follow | `FormS` 类型检查 | 注册注入 + 蹦床 + 烘焙 |
| ts | — | — | — | — | 无（纯库模块） |

## 附录 B：快速导航

- T@ 自动指针 → §1
- cls/dim 类机制 → §2
- com 设备通讯 → §6
- queue 多线程协同 → §3
- tok 依赖图 → §4
- ts 张量 → §5
