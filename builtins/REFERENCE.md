# builtins 内置模块参考

`builtins/` 是 scc 的内置模块搜索路径：`inc x.sc` 会依次尝试
`builtins/x.sc` 与子项目形态 `builtins/x/x.sc`。
也可用环境变量 `SCC_BUILTINS` 指定额外搜索目录。

> 三大支柱文档：[REFERENCE.md](REFERENCE.md)（模块参考总览·本文）、
> [MECHANISM.md](MECHANISM.md)（核心机制规格·T@/cls/dim/mt/tok/ts 五章）、
> [ROADMAP.md](ROADMAP.md)（开发路线图）。

当前内置模块：

| 模块 | 引入方式 | 说明 |
|------|----------|------|
| adt | `inc adt.sc` | 抽象数据类型：string（动态字符串）、list（段式裸 @ 容器）、dict（开放寻址哈希映射）、ring（SPSC 无锁循环队列）、bst（AVL/红黑融合有序映射）、heap（数组背二叉堆/优先队列）、trie（前缀树）、lru（LRU 缓存） |
| mt | `inc mt.sc` | 多线程语言支持标准：run 语句、thread、mutex、cond（含 wait 方法）、pool（线程池）、queue（消息队列） |
| sys | `inc sys.sc` | 运行环境 / 系统路径 / 应用网络：ARGS_* 命令行解析、work_dir、home_dir、download_dir、exe_file、tmp_file、sock_socketpair / sock_connect / sock_close（跨平台 TCP 套接字，C 侧实现） |
| ts | `inc ts.sc` | 数值张量：numpy 级存储-视图分离（ts_store refcount + strides + offset）+ PyTorch nn 算子核——tensor（动态 rank，dtype f4/f8/i4/i8/bool）、工厂 zeros/ones/full/empty/arange/linspace/logspace/eye/diag/from_data/*_like、零拷贝形变 reshape/transpose/permute/squeeze/unsqueeze/ravel/broadcast_to/flip、索引视图 slice/select/narrow（take/masked_select/where 物化）、逐元素（广播）add/sub/mul/div/pow/mod/max/min/比较→bool/逻辑(+原地 `_`)、一元 abs/sqrt/exp/log/floor/round/sign/clip/三角族(sin/cos/tan/asin/acos/atan/sinh/cosh)…、规约 sum/mean/max/min/argmax/argmin/prod/std/var/cumsum/any/all/median/percentile（axis+keepdim）、线代 matmul(2D/批量,可选 BLAS)/bmm/addmm/dot/outer/trace/diagonal/triu/tril/norm/det/inv/solve/cholesky/qr/eigh/svd、拼接 concat/stack/split/tile/repeat、网格 meshgrid、nn relu/sigmoid/tanh/softmax/log_softmax/leaky_relu/elu/silu/gelu/cross_entropy/mse_loss/nll_loss/bce_with_logits/layer_norm/dropout/sdpa/conv1d/conv2d/max_pool1d/avg_pool1d/max_pool2d/avg_pool2d、高级索引 nonzero/gather/scatter_、形变 pad/roll、随机 rand_seed/rand_uniform/rand_normal/rand_randint/permutation/shuffle_、构造 tri、序列化 save/ts_load(.npy，兼容读取旧 .scts)、以及与前向成对的**反向数值核**（matmul/逐元素/relu/sigmoid/tanh/mse/cross_entropy backward + sum_to 广播归约，仅 C-ABI 供 nn 组合，不在 sc 表面暴露）。进度与路线见 `ts/ROADMAP.md`，规格示例见 `ts.md` |
| nn | `inc nn.sc` | 神经网络自动微分引擎（nn = ts 数值（含反向核） + tok 调度 之上的组合层，PyTorch 风格）——`val`（define-by-run 张量节点：matmul/add/sub/mul/relu/sigmoid/tanh/mse_loss/cross_entropy/backward/item/value/grad/zero_grad）、进程内录带 tape（`nn_input`/`nn_param`/`nn_tape_clear`）、`linear` 模块（Kaiming 初始，forward/w/b；权重存 `[in,out]`）、`optim` 优化器（`nn_sgd`/`nn_adam`，含 momentum/weight_decay，track/track_linear/zero_grad/step）。训练闭环：forward → `loss->backward()` → `opt->step()` → `opt->zero_grad()` → `nn_tape_clear()`。规格见 `nn/nn.sc`，示例见 `templates/dnn-framework` |
| neuron | `inc neuron.sc` | 独立神经元基元（与 `nn` 成对——`nn`=张量/层粒度工业引擎，`neuron`=神经元/标量粒度研究设计件；自动微分**引擎**本身是 tok 的 `back`/`TOK_BACK` 反拓扑序，本库提供与拓扑无关的算子层）。纯 sc 实现（`builtins/neuron/neuron.sc` 全程可读，无 _impl.c）：核心是**神经元对象 `neuron`**（act/pre/grad/bias/权重 `w[]` + SGD 动量 `vw/vbias` + Adam 二阶矩 `sw/sbias`；方法 `forward`/`backward`/`sgd`/`adam`/`seed_mse`，均返回 `this` 可链式）、激活枚举 `act_kind`（`AK_IDENT/AK_RELU/AK_LEAKY/AK_SIGMOID/AK_TANH`）、纯标量数学 `act_fwd`/`act_bwd`/`mse_loss`（自由函数）、多分类 `xent_seed`（softmax 交叉熵）、dep follow 体一行调度 `edge_step(dep, down)`。拓扑（tok=神经元 / dep=突触束 / back=反传）显式留在用户 .sc。示例见 `templates/dnn-design` |
| mem | `inc mem.sc` | 内存池：chunk/chunk0/chunk_array/chunk_aligned/refit/recycle（替代 malloc/calloc/realloc/free）、mem_usable、mem_trim（空闲页归还 OS）、mem_stat（统计快照含峰值）、mem_teardown、arena（竞技场批量分配）、shm（跨进程命名共享内存，支持只读/独占标志）；每线程 TLS 堆无锁、跨线程释放安全、线程退出堆自动回收复用 |
| proto | `inc proto.sc` | 协议解析/转换构件：单一 `proto` 对象承载「类型化字节记录」序列 + 消费纪律（`PROTO_FILO` 栈 / `PROTO_FIFO` 队列）——`feed`（通用三元组 tag/data/size 入栈入队）、类型化 `push_b/i1/i2/i4/i8/u1/u2/u4/u8/f4/f8`（可带 printf format）/`push_str`（可去尾 trim）/`push_blob`/`push_ptr`（内联 xform 回调）、`drain`/`peek`/`back`（按 mode 顶/头消费）、`each`（自定义 xform 逐条重组，供 encode/decode 双向转换）、`build`/`build_to`（内置格式化重组，条间 delim）、`depth`/`is_empty`/`clear`/`drop`。存储创新（参考 stk）：定长 chunk 双向链，数据从块首前向排、4 字节索引项从块尾反向排，块满追加新块（指针稳定不搬移），整块入空闲缓存复用；索引 = kind(高8位)\|offset(低24位)，单块寻址 16MB，记录大小由相邻偏移推出。底层内存走 mem 模块 |
| op | 默认导入（无需 inc） | 语言底层（语法层面）机制：operand（设备操作数 `.` 透传为 `platform.h` 的 `sc_<op>` 宏）、chain（侵入式双向链表，C 运行时由 `op.h`/`op_impl.c` 自动提供）、stringify（类型 JSON 格式化关键字，选项类型 `stringify_t` 见 `op.h`）；platform.h 的 sc 侧入口 |
| gpu | `inc gpu.sc` | GPU 运行环境（env 层，≈sokol_app 去窗口）：多后端（Metal/GL，运行时选择不降级）、device、surface 交换链（WINDOW 窗口 / MEMORY 无表面 memimg 环，dequeue/enqueue 供编码器）、memimg（可导出/导入内存图像：dma-buf/IOSurface）、每帧渲染目标交付。**不依赖窗口库**——gpu.h 文件头定义平台原生句柄标准，窗口库（如 templates/utils/wsi）适配交付。预编译 lib<mod>.a 按 triple 变体匹配；平台框架链接编译器自动注入。caps/ = 标准设备能力档案库（.ss `tar "名.caps"`）。机制见 `gpu/MECHANISM.md` |
| gfx | `inc gfx.sc` | 渲染层（≈sokol_gfx，依赖 gpu）：五类资源（buffer/image/sampler/shader/pipeline）、pass/draw/commit、执行 scc 的 .ss 产物（反射清单驱动绑定）；Metal + GL4.1/GLES3 双后端；memimg 可绑定为离屏渲染目标 |
| spc | `inc spc.sc` | 多维空间并行计算（依赖 gpu，与 gfx 平级）：三级入口——kernel（.ss comp 自写算子，Metal compute）/ graph（MPSGraph 算子图）/ model（CoreML 整图推理，唯一 ANE 路径，实测 86% 算子上 ANE）；与 ts 共享 sc_tensor 字段（零链接依赖） |

另有 `platform.h`（非模块）：面向用户的 C 跨平台基础头，默认 -I 可直接 inc，见末节。

## 子项目通用机制

子项目形态 `builtins/x/` 由三件套构成，分别面向编译流程的不同阶段：

| 文件 | 角色 | 面向谁 |
|------|------|--------|
| x.sc | **sc 侧接口声明**：`@def` 数据布局 + `@fnc T::m` 方法声明（无函数体），决定 sc 代码能怎么用 | scc 编译器 |
| x.h | **C ABI 契约**（数据布局/原型，与 x.sc 同步维护），自定义/默认实现据此编写 | C 编译器 |
| x_impl.c | **默认实现**：源实现经拼接机制并入 `x.sc` 生成的单元 `.c`（同一 TU）；预编译 `.a` 则单独链接 | C 编译器 / 链接器 |

### 本质：x.sc 真正参与编译，不是参考文档

`inc x.sc` 并非纯文档引用——scc 会**真实读取、词法+语法解析这个 `.sc` 文件**，
把其中 `@` 导出的声明提取出来标记为 `external`，挂进当前单元的 AST 参与类型检查与
代码生成。它给编译器提供的是 **sc 语言侧的"符号契约"**。

> 谁说了算：sc 代码怎么用这个类型，由 x.sc 说了算（编译器只读它）；生成的 C 与
> 底层实现怎么对齐，由 x.h 说了算（C 编译器只读它）。同一份数据布局在 x.sc 的
> `@def` 与 x.h 的 `struct` 中**各写一遍、需手工保持一致**——两者是一对必须同步的
> 声明，而不是某一方自动生成另一方。

### 流程：从 `inc x.sc` 到可执行文件

1. **路径解析**：`inc x.sc` 经 `resolveModulePath` 解析为 `builtins/x.sc` 或子项目
   `builtins/x/x.sc`，标记该 `inc` 声明 `external = true`，加入依赖列表。
2. **解析并合并声明**：scc 读取 `x.sc`、`parse(lex(...))`，把其中 `@` 导出的
   `@def`/`@fnc T::m` 声明标记 `external` 后合并进主单元 AST（`resolveUnitDeps`），
   再递归对依赖自身重复此过程（`loadUnitGraph`，含循环依赖检测）。
3. **语义检查**：`semanticCheck` 据合并后的 external 声明校验——类型可见、方法存在、
   返回类型推断、参数匹配。
4. **代码生成**：codegen **跳过所有 external 声明的代码体**（方法无函数体，"由其
   模块头提供"），仅生成对它们的引用——方法调用 `s.len()` 重整为 `string_len(&s)`，
   并依据声明驱动语法糖（`string()` 堆构造、声明即构造）。生成的 `.c` 顶部
   `#include "builtins/x/x.h"`，由 x.h 提供真正的 `struct` 定义与函数原型。
5. **并入实现**：源实现 `x_impl.c` 经**拼接机制**并入 `x.sc` 生成的单元 `.c`、
   编成同一翻译单元（与 sc 侧共享模块私有 `static`，`::` 符号就地定义）；预编译
   `x.a`（内嵌发行版释放）或 `--adt <x.o|x.a>` 指定的二进制实现无法拼接，则作为目标
   文件单独参与链接；皆无则跳过（不影响纯 sc 子项目）。拼接机制通用细节见
   [syntax.md](../syntax.md) §9「模块实现的并入：拼接机制」。

### 用途：三件套各自决定什么

| 文件 | 决定 | 举例 |
|------|------|------|
| x.sc | sc 代码**怎么用**这个类型（合法类型、方法签名、语法糖、name mangling） | `var s: string`、`s.len()` 返回 `u8` |
| x.h | 生成的 C 与实现**怎么对齐**（数据布局、函数原型） | `struct string {...}`、`string_len(...)` |
| x_impl.c | extern 符号怎么兑现（默认实现，可替换）——源实现拼接进单元 TU，二进制实现走链接 | `string_len` 的实际函数体 |

关键点：x.sc 的方法**没有函数体**，scc 因此不为其生成任何代码体，实现交给配套 C——
源实现 `x_impl.c` 拼接进同一 TU、二进制实现（`.a`/`--adt`）走链接——这正是"可替换
实现"（`--adt` 等）得以成立的基础。

## adt —— 抽象数据类型子项目

目录结构（`builtins/adt/`）：

| 文件 | 角色 |
|------|------|
| adt.sc | sc 侧接口声明：`@def` 数据布局 + `@fnc T::m` 方法声明（无函数体） |
| adt.h | C ABI 契约（数据布局/原型，与 adt.sc 同步维护），自定义实现据此编写 |
| adt_impl.c | 默认实现，经拼接机制并入 adt 单元 `.c`（同一 TU） |

### 工作机制

1. sc 源码 `inc adt.sc` 后即可使用 `string`/`list`/`dict`/`ring` 类型及其方法。
2. 方法声明（`fnc T::m` 无函数体）转 C 时生成 extern 原型 `T_m(T *_this, ...)`，
   实现由配套 C 兑现。
3. 单元图包含 `builtins/adt/adt.sc` 时，scc 把默认实现 `adt_impl.c` 经拼接机制并入
   adt 单元 `.c`（同一 TU）；可替换为自定义实现：

```sh
scc app.sc --adt my_adt.c      # .c 源实现拼接进 adt 单元 TU；.o/.a 直接参与链接
SCC_ADT=my_adt.o scc app.sc    # 环境变量等价；.sc 配置键 adt 亦可
```

自定义实现须完整实现 `adt.h` 中的全部函数（可基于第三方库如 sds、uthash 等封装）。

### 构造与析构

- `init`：栈对象声明即构造——函数内 `var s: string`（无初值、非指针、非数组）自动调用
  `string_init(&s)`；全局变量需手动 init。
- `T()` 堆构造糖：`var p&: string = string()` → malloc + 清零/默认值 + init，
  返回 `T&`（malloc 失败返回 nil）。
- `drop`：手动析构 `s.drop()`（命名保留，未来支持作用域自动插入）；
  堆对象 drop 后需再 `free(p)`（drop 只释放内部资源，不释放对象本身）。

### string —— 动态字符串

内部 NUL 结尾，`cstr()` 永不返回 nil，可直接交给 C 接口。
返回 `bool` 的方法：1 成功 / 0 失败（内存不足或参数越界）。
签名为空表示无返回值无参数。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造为空串 |
| drop | | 释放缓冲区 |
| len | `u8` | 字符数（不含 NUL） |
| cstr | `char&` | C 字符串视图（始终非 nil） |
| clear | | 置空（保留容量） |
| reserve | `bool, n: u8` | 预留容量 |
| assign | `bool, s&: const char` | 赋值为 C 字符串 |
| append | `bool, s&: const char` | 追加 C 字符串 |
| append_n | `bool, s&: const char, n: u8` | 追加前 n 字节 |
| append_char | `bool, c: char` | 追加单字符 |
| insert | `bool, index: u8, s&: const char` | 指定位置插入 |
| erase | `bool, index: u8, n: u8` | 删除 n 字节 |
| at | `char, index: u8` | 取字符（越界返回 0） |
| find | `i8, sub&: const char, start: u8` | 查找子串（未找到 -1） |
| rfind | `i8, sub&: const char` | 反向查找（未找到 -1） |
| equals | `bool, s&: const char` | 与 C 字符串比较相等 |
| starts_with | `bool, s&: const char` | 前缀判断 |
| ends_with | `bool, s&: const char` | 后缀判断 |
| slice | `bool, start: i8, stop: i8, out&: string` | 切片 `[start, stop)`，负索引从尾部计 |
| strip | | 去除首尾空白 |
| lower / upper | | 大小写转换（ASCII） |
| clone | `bool, out&: string` | 深拷贝到 out |

### list —— 动态指针数组

元素为裸指针（`&`，即 void 指针），**不拥有元素**：drop/clear/remove_at 不释放元素本身。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造为空列表 |
| drop | | 释放槽位数组 |
| len | `u8` | 元素个数 |
| clear | | 清空（保留容量） |
| reserve | `bool, n: u8` | 预留槽位 |
| push | `bool, value&:` | 尾部追加 |
| pop | `&` | 弹出尾元素（空返回 nil） |
| get | `&, index: u8` | 取元素（越界返回 nil） |
| set | `bool, index: u8, value&:` | 改写元素 |
| insert | `bool, index: u8, value&:` | 指定位置插入 |
| remove_at | `&, index: u8` | 删除并返回该元素 |
| index_of | `i8, value&:` | 按指针值查找（未找到 -1） |
| reverse | | 原地反转 |
| clone | `bool, out&: list` | 浅拷贝到 out |
| sort | `cmp&: list_cmp` | 稳定排序，`list_cmp: i4, a&:, b&:` |

### dict —— 开放寻址哈希映射

key→`@`（裸自动指针）映射，**拥有 value**（每条一份 retain，put 替换/remove/clear/drop 自动 release）。
自写 SwissTable 风格开放寻址表：控制字节 + 桶数据两块内存，无 per-item 分配；线性探测 + 墓碑，
负载因子 7/8 时整体 rehash 重建。因 `init` 带 `key_size` 参数，**不参与「声明即构造」**，须显式 `d.init(...)`。

`key_size` 三态决定 key 语义（接口 key 均为 `const &` 裸指针，按 key_size 解读）：

| key_size | 含义 | 存储 | 比较 |
|----------|------|------|------|
| `> 0` | 定长数值/POD（如 i4=4、指针=8） | 内联拷贝 key_size 字节 | memcmp（浮点键不安全，限整数/指针类） |
| `== 0` | 引用字符串 | 仅存 `const char*` 借用指针，**dict 不拥有**，字符串本体须由 value 自持 | strcmp |
| `== -1` | 拷贝字符串 | put 时复制一份，remove/clear/drop 回收 | strcmp |

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `key_size: i4` | 构造（指定 key 模式） |
| drop | | 释放全部 retain + 回收桶/控制块 |
| len | `u8` | 元素个数 |
| has | `bool, key: const &` | 是否含 key |
| get | `@, key: const &` | 借用 value 句柄（未命中返回空句柄，不改计数） |
| put | `bool, key: const &, value: @` | 插入/替换（retain 新、替换 release 旧） |
| remove | `bool, key: const &` | 删除并 release value（未命中返回 false） |
| clear | | 清空并 release 全部 value（保留桶容量） |
| each | `fn: dict_each_fn, ctx: &` | 无序遍历占用桶，回调返 false 即停 |
| first / last | `i8` | 首/末个占用桶游标（空集 -1） |
| next / prev | `i8, cur: i8` | cur 之后/之前的占用桶（无则 -1） |
| key_at | `const &, cur: i8` | 游标处 key（无效返回 nil） |
| value_at | `@, cur: i8` | 游标处 value 借用（无效返回空句柄） |

回调类型 `dict_each_fn: bool, key: const &, value: @, ctx: &`（返回 false 提前终止）。
游标与 `each` 走同一桶序；`get/has/each` 期间游标稳定，`put/remove` 可能 rehash 使其失效（遍历期间勿增删）。

#### 桶结构与碰撞策略（自写，非 uthash 路线）

dict 的底层结构完全自写，走**开放寻址（open addressing）**路线，与 uthash 的**链地址法（separate
chaining）**根本不同——更接近 Google SwissTable / Rust `hashbrown`。当初拒绝 uthash 正是为避开它的两个
硬伤：**per-item malloc**（每元素一次堆分配，cache 不友好）与**编译期类型绑定**（侵入式 handle 宏，与裸
`@` 类型擦除不兼容）。

- **连续桶数组 + 线性探测**：所有 item 内联住一整块 `slots`，碰撞时顺序往后探（`i = (i+1) & mask`，
  nbuckets 为 2 的幂）。整张表仅 `ctrl + slots` 两块内存，**零 per-item 分配**，探测/遍历皆顺序访问。
- **SwissTable 控制字节**：独立 1 字节/桶的 `ctrl` 数组存 hash 低 7 位（h2）。探测先比这 1 字节快速过滤，
  绝大多数不匹配桶连 key 都不碰（空=`0xFF`、墓碑=`0xFE`、占用=`0x00..0x7F`）。
- **墓碑删除**：删除标 `0xFE` 而非真空，保住探测链；rehash 时统一清墓碑。
- **负载因子 7/8 整体 rehash**：`(used+1)*8 >= nbuckets*7` 触发，重建整表（开放寻址扩容时 item 位置必变，
  本就须全搬，故不做增量迁移）。

| 维度 | uthash | 本 dict |
|------|--------|---------|
| 桶结构 | 桶=链表头，元素散在堆 | 桶=连续数组槽，元素内联 |
| 碰撞解决 | 链地址（挂链表） | 开放寻址 + 线性探测 |
| 元素分配 | 每 item 一次 malloc | 零 per-item，整表两块内存 |
| 快速过滤 | 可选 bloom filter | SwissTable 控制字节（h2） |
| 删除 | 摘链表节点 | 墓碑标记 |
| 扩容 | 桶翻倍重挂链（item 不动） | 负载 7/8 整体 rehash 重建 |
| 内存局部性 | 差（指针跳转） | 好（顺序访问） |
| 类型模型 | 编译期宏 + 侵入式 handle | 运行时类型擦除（key_size 三态） |

> 仅「哈希算法」这一层对标了 uthash（下文 7 选 1）；桶与碰撞这套底层结构是独立的开放寻址设计。
>
> **设计渊源**：本 dict 属 Google **SwissTable**（Abseil `flat_hash_map`）/ Rust **`hashbrown`**（标准库
> `HashMap` 后端）一派——开放寻址 + 独立控制字节（h2 元数据）+ 连续内联存储。本实现是其精简版：保留控制
> 字节快速过滤与连续布局两大核心收益，但用**标量逐桶线性探测**替代 SwissTable 的 SIMD 16 桶组（实现更小、
> 无平台 intrinsic 依赖），代价是大表高负载时探测略慢于 SIMD 版。

#### 哈希算法选择（编译期，对标 uthash）

dict 内置 7 种哈希算法，默认 **FNV-1a**。算法全进程统一（哈希值不跨进程持久化），用编译宏切换、
**零运行时开销**（编译期定死、直接内联，无函数指针间接调用）：

```sh
SCC_CFLAGS="-DDICT_HASH=dict_hash_mur" scc app.sc   # 切到 MurmurHash3
# 或 .sc 配置：cflags = -DDICT_HASH=dict_hash_jen
```

| 宏值 | 算法 | 速度 | 分布质量 | 优势 / 适用场景 |
|------|------|------|----------|-----------------|
| `dict_hash_fnv`（默认） | FNV-1a 64 | 快 | 中上 | 实现极小、无表无依赖；短键（小整数、短字符串）分布稳——**通用首选** |
| `dict_hash_ber` | Bernstein djb2 | 最快 | 中 | 一行核心、最省指令；键短且分布友好、追求极致简洁时用 |
| `dict_hash_sax` | Shift-Add-XOR | 最快 | 中 | 同 djb2 量级，移位混合，对部分文本键比 djb2 略匀 |
| `dict_hash_oat` | Jenkins one-at-a-time | 快 | 上 | 雪崩明显优于 djb2/SAX，仍单字节流式；中等长度字符串键的稳妥选择 |
| `dict_hash_jen` | Jenkins lookup2 | 中 | 高 | 12 字节分块强混合；长字符串键、要求低碰撞时 |
| `dict_hash_sfh` | SuperFastHash | 中快 | 高 | 16 位分块，吞吐高；中长键、批量查找的吞吐敏感场景 |
| `dict_hash_mur` | MurmurHash3 x86_32 | 中快 | 最高 | 雪崩/抗碰撞最好；键分布刁钻、对碰撞最敏感时——**质量优先首选** |

选型经验：**短键**（≤8 字节 int/短串）→ FNV-1a/djb2（混合开销占比大，简单即快）；
**中长字符串键**→ OAT/SFH；**对碰撞最敏感或键分布刁钻**→ MurmurHash3。
注意：本表非密码学哈希，**不抗 HashDoS**；如需抗恶意构造碰撞，应另接 SipHash（替换 `dict_hash` 单函数即可，接口/ABI 不变）。

### ring —— SPSC 无锁循环队列（kfifo 风格）

单生产者单消费者（SPSC）**无锁有界队列**，仿 Linux 内核 `kfifo`。元素为定长值块（`elem_sz` 字节、
逐字节复制），容量向上取 2 的幂。`head`（消费者独写）/ `tail`（生产者独写）为自由递增计数器，
槽位下标 = `idx & mask`，元素数 = `tail - head`（无符号回绕）：**空** = `tail==head`，**满** = `tail-head > mask`。
因 `init` 带参数，**不参与「声明即构造」**，须显式 `r.init(sizeof(T), capacity)`。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `bool, elem_sz: u4, capacity: u4` | 构造（capacity 向上取 2 幂；分配失败返回 false） |
| drop | | 释放缓冲区 |
| cap | `u8` | 容量（2 的幂；未初始化 0） |
| len | `u8` | 当前元素数快照（tail − head） |
| is_empty | `bool` | 是否空 |
| is_full | `bool` | 是否满 |
| clear | | 复位 head/tail（**仅无并发时安全**） |
| push | `bool, value: &` | 生产者入队一个元素（拷贝 elem_sz 字节）；满返回 false |
| pop | `bool, out: &` | 消费者出队到 out（拷贝 elem_sz 字节）；空返回 false |
| peek | `&` | 消费者借用队首元素指针；空返回 nil |

#### 无锁内存序

生产者 `push` 以 **release** 发布 `tail`、消费者 `pop`/`len`/`is_empty` 以 **acquire** 观测 `tail`，
保证数据写入先于索引可见；反向 `head` 同理释放槽位供生产者复用。原子读写经 `platform.h` 的 `sc_*` 宏
（C11 `stdatomic` / 平台特化），无任何锁与系统调用。

**使用约束**：
- 仅 **SPSC** 安全——**单一**生产者线程只调 `push`，**单一**消费者线程只调 `pop`/`peek`；
  多生产者或多消费者并发须**外部加锁**（或改用带锁队列）。
- `init`/`drop`/`clear` 仅在**无并发访问**时调用（建立/拆除阶段）。
- `peek` 返回指向缓冲区内部的指针，仅供消费者线程在下一次 `pop` 前读取。
- `head`/`tail` 同 cache 行存在**伪共享**，极端吞吐场景可自行加 padding 分离到不同 cache 行。

```sc
inc adt.sc
inc mt.sc
inc sys.sc

@def shared: { q: ring ; ... }

@rpc producer: s: shared&, n: i4          # 单生产者线程
    var i: i4 = 1
    for i = 1; i <= n; i++
        while !s->q.push(&i)              # 满则自旋让出
            usleep(0)

@rpc consumer: s: shared&, n: i4          # 单消费者线程
    var v: i4 = 0
    ...
        if s->q.pop(&v)                    # 空则自旋
            ...

fnc main: i4
    var s: shared
    s.q.init(sizeof(v), 1024)              # 容量取 2 的幂
    run producer(&s, n), &tp
    run consumer(&s, n), &tc
    tp->join(); tc->join()
    s.q.drop()
```

完整用例见 `tests/cases/ring_at.sc`（单线程 FIFO/满空语义 + 并发 SPSC 校验和压力）。

### bst —— AVL/红黑 融合有序映射

key→`@`（裸自动指针）的**有序**映射，与 dict 同为**拥有 value**（每条一份 retain，put 替换/remove/clear/drop 自动 release），
但额外维护 key 全序：中序遍历升序、支持 index_of/at 序号换算与 most(≤)/least(≥) 最接近项查询。因 `init` 带参，**不参与「声明即构造」**，须显式 `t.init(...)`。

**创新点：一棵树同时是 AVL 与红黑**——`init` 的 `red_depth` 参数切换平衡策略（本质是「容忍的不平衡深度」）：

| red_depth | 等价 | 平衡限度 |
|-----------|------|----------|
| `0` | **AVL** | 左右子树高差≤ 1；查找最快、写旋转最勤 |
| `1`（典型） | **红黑** | 允许多一层不平衡（红节点）；写旋转/变色更少 |

`key_size` 三态同 dict（接口 key 均为 `const &` 裸指针）；数值键默认**按宽度有符号比较**（memcpy 载入，对齐安全），可传自定 comparator 覆盖：

| key_size | 含义 | 存储 | 比较 |
|----------|------|------|------|
| `> 0` | 定长数值/POD（i4=4、指针=8） | 内联拷贝 key_size 字节 | 宽度 1/2/4/8 按有符号值，其余 memcmp |
| `== 0` | 引用字符串 | 仅存借用指针，本体由 value 自持 | strcmp |
| `== -1` | 拷贝字符串 | put 时复制一份，remove/clear/drop 回收 | strcmp |

传入 `cmp`（非 nil）时忽略以上内置规则，全走自定比较：`bst_cmp_fn: i4, a: const &, b: const &, ctx: &`（返回负/0/正，a 为待查 key、b 为节点 key）。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `red_depth: u1, key_size: i4, cmp: bst_cmp_fn, ctx: &` | 构造（red_depth、key 模式、可选 comparator；无则传 nil/nil） |
| drop | | 释放全部 retain + 回收节点 |
| len | `u8` | 元素个数 |
| has | `bool, key: const &` | 是否含 key |
| get | `@, key: const &` | 借用 value 句柄（未命中返回空句柄，不改计数） |
| put | `bool, key: const &, value: @` | 插入/替换（retain 新、替换 release 旧；插入后自动重平衡） |
| remove | `bool, key: const &` | 删除并 release value（未命中返回 false；自动重平衡） |
| clear | | 清空并 release 全部 value |
| each | `fn: bst_each_fn, ctx: &` | **中序（升序）**遍历，回调返 false 即停 |
| first / last | `i8` | 最小/最大 key 游标（空集 0） |
| next / prev | `i8, cur: i8` | 中序后继/前驱游标（无则 0） |
| key_at | `const &, cur: i8` | 游标处 key（无效返回 nil） |
| value_at | `@, cur: i8` | 游标处 value 借用（无效返回空句柄） |
| index_of | `i8, key: const &` | key 的 0 起中序序号（不存在 -1） |
| at | `i8, index: u8` | 第 index（0 起）个中序节点游标（越界 0） |
| most | `i8, key: const &` | ≤ key 的最大项游标（前驱或相等；无则 0） |
| least | `i8, key: const &` | ≥ key 的最小项游标（后继或相等；无则 0） |

回调类型 `bst_each_fn: bool, key: const &, value: @, ctx: &`（返回 false 提前终止）。游标为节点指针的 `i8` 表示（0 为终端），中序链表维护 prev/next，`first/next` 与 `each` 同序。

#### 对齐安全与崩溃根因

bst 采**父指针节点**设计（每节点含 `parent`，旋转/重平衡沿父链上溯），**无外部搜索栈**；数值 key 一律经 `memcpy` 载入后比较，不做错位重解释强转。

> 这正是从 C 原型移植时修复的崩溃根因：原型在 `SUPPORT_PARENT_FIELD==0` 分支用 `#pragma pack(1)` 的搜索栈数组（`{node* p; int8_t i;}` sizeof=9、align=1），数组元素使 `node* p` 落在非对齐偏移（+9/+18…）。高通 Cortex-A 允许非对齐访问故无事，TI 严格对齐核访问即 SIGBUS。父指针设计（节点自然对齐、无 packed 数组）+ memcpy 载入同时消除了该崩溃与「地址转换未考虑对齐」问题。

完整用例见 `tests/cases/bst_at.sc`（AVL/红黑 双模式、三态 key、中序/游标/index_of/most/least 全接口）。

### heap —— 数组背二叉堆 / 优先队列

`(key, value)` 对的优先队列：key 决定优先级，value 为 `@`（裸自动指针），heap **拥有 value**（每元素一份 retain，pop/clear/drop 自动 release）。
**数组背**完全二叉树（隐式编码：父 `(i-1)/2`、左右子 `2i+1`/`2i+2`），连续内存缓存友好；push 上滤 / pop 下滤均 O(log n)。因 `init` 带参，**不参与「声明即构造」**，须显式 `h.init(...)`。

`min` 决定堆向：

| min | 堆向 | 堆顶 | 典型场景 |
|-----|------|------|----------|
| `1` | **最小堆** | 最小 key | Dijkstra 最短路、定时器（最近截止）、事件调度 |
| `0` | **最大堆** | 最大 key | top-k、优先级递减调度 |

`key_size` 三态与比较器语义**完全同 bst**（接口 key 均为 `const &` 裸指针）：`>0` 定长数值（按宽度有符号，memcpy 载入）/ `==0` 引用字符串 / `==-1` 拷贝字符串（strcmp）；传 `cmp`（非 nil）则走自定比较 `heap_cmp_fn: i4, a: const &, b: const &, ctx: &`。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `min: u1, key_size: i4, cmp: heap_cmp_fn, ctx: &` | 构造（堆向、key 模式、可选 comparator；无则 nil/nil） |
| drop | | 释放全部 retain + 回收槽数组 |
| len | `u8` | 元素个数 |
| is_empty | `bool` | 是否空 |
| clear | | 清空并 release 全部 value（保留容量） |
| reserve | `bool, n: u8` | 预留至少 n 槽 |
| push | `bool, key: const &, value: @` | 入堆（retain value，上滤；失败 false） |
| pop | `bool` | 弹出堆顶并 release（下滤；空返回 false） |
| peek | `@` | 借用堆顶 value 句柄（不改计数；空返回空句柄） |
| peek_key | `const &` | 借用堆顶 key（空返回 nil） |

取出语义同 dict「取用分离」：`peek`/`peek_key` 借用堆顶不改计数，`pop` 删除并 release。要取并保留：`var x: T@ = (h.peek(): T@)`（借用→绑定 retain）再 `h.pop()`（release），净余一份。

> **不提供遍历游标**——堆底层数组非优先序，迭代会误导（与 C++ `priority_queue` 一致）。未内置 decrease-key（需位置句柄）；优先级变更用「推新值 + pop 时跳过陈旧项」的惰性删除（Dijkstra 常规做法）。

完整用例见 `tests/cases/heap_at.sc`（最小/最大堆 双向、数值/字符串 key、peek/pop/clear/reserve 全接口）。

### trie —— 前缀树

字符串键 → `@`（裸自动指针）的映射，**拥有 value**（每键一份 retain，put 替换/remove/clear/drop 自动 release）。键被**逐字节分解进树路径**（路径即键，不另存键串），天然适合**前缀查询 / 自动补全 / 路由最长匹配**。`init` 无参，**参与「声明即构造」**（`var t: trie` 自动 `trie_init`）。

**节点表示：首子/次兄有序链 + 父指针**。每节点仅存一个边字节，兄弟按字节**升序**排列 —— 所以 `each`/`each_prefix` 遍历**天然得字典序**，无需额外排序；子查找在兄弟链上线性扫描（分支因子通常很小）。节点同时记 `subkeys`（子树内键数），使 `count_prefix` 退化为 **O(prefix)** 而非扫子树。

键恒为 NUL 结尾字符串（接口 key/prefix 均为 `const char&`），**无 key_size 三态**（区别于 dict/bst/heap）——这是 trie 的专长。空串 `""` 是合法键（任意串的前缀，`longest_prefix` 对任意输入返 0）。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造（空树；参与声明即构造） |
| drop | | 释放全部 retain + 回收全部节点 |
| len | `u8` | 键个数 |
| has | `bool, key: const char&` | 是否含**精确**键（非前缀） |
| get | `@, key: const char&` | 借用键对应 value（未命中空句柄，不改计数） |
| put | `bool, key: const char&, value: @` | 插入/替换（retain 新、替换 release 旧） |
| remove | `bool, key: const char&` | 删除并 release（未命中 false；**剪枝悬挂空节点但保留共享前缀**） |
| clear | | 清空并 release 全部 value |
| has_prefix | `bool, prefix: const char&` | 是否存在以 prefix 开头的键 |
| count_prefix | `u8, prefix: const char&` | 以 prefix 开头的键数（**O(prefix)**） |
| each | `fn: trie_each_fn, ctx: &` | **按字典序**遍历全部键（回调返 false 即停） |
| each_prefix | `prefix: const char&, fn: trie_each_fn, ctx: &` | 字典序遍历 prefix 开头的键（**自动补全**） |
| longest_prefix | `i8, text: const char&` | text 的最长「键前缀」长度（无则 -1；空串键返 0） |

回调类型 `trie_each_fn: bool, key: const char&, value: @, ctx: &`（`key` 为完整键串，返 false 提前终止）。

> **不提供整数游标**——键串须沿路径重建，故用**回调**在 DFS 中增量拼键（父指针回溯迭代，无递归无显式栈），O(总字符数)。`put`/`remove` 也是迭代（深度 = 键长，避免超长键深递归）；`clear`/`drop` 用**后序**迭代释放（子节点先于父回收）。

完整用例见 `tests/cases/trie_at.sc`（put/replace、has/get、前缀计数/字典序补全/最长前缀、空串键、remove 剪枝全接口）。

### lru —— LRU 缓存（有界容量 + 最近最少使用淘汰）

**组合容器**：内嵌 `dict`（key → 节点指针，**O(1) 查找**） + 侵入**双向链表**（head=最近使用 MRU、tail=最久未用 LRU）。value 为 `@`，**拥有 value**（每键一份 retain，put 插入/替换 retain、remove/淘汰/clear/drop release）。因 `init` 带参，**不参与「声明即构造」**，须显式 `c.init(key_size, cap)`。

**节点独立分配（地址稳定）**是该设计的关键：dict 开放寻址 rehash 会搬动条目，故内联条目无法被链表稳定引用；本实现让 dict **仅存「借用句柄」（tar=NULL/own=RAW，绑定解绑均不计数）**指向独立 chunk 的节点，节点拥有 value 与键拷贝——dict 负责哈希与三态键，链表负责 O(1) 触顶与 O(1) 淘汰。

**访问语义**是缓存的核心：

| 操作 | 是否改最近度 | 说明 |
|------|------------|------|
| `get` | **是**（触顶，移至 MRU） | LRU 语义的「访问」；未命中返空句柄 |
| `peek` | 否 | 只看不动（调试/旁路查看） |
| `has` | 否 | 仅存在性判定 |
| `put` | 是（插入/替换后置 MRU） | 超容淘汰队尾 |

`cap=0` 表**无界**（退化为可保插入/访问顺序的 dict）；`cap>0` 时 `put` 新键超容即**淘汰队尾（LRU）**；`set_cap` 缩容立即淘汰至 `len<=cap`。key 三态同 dict（`>0` 定长数值 / `==0` 引用字符串 / `==-1` 拷贝字符串）。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `key_size: i4, cap: u8` | 构造（key 模式 + 容量；cap=0 无界） |
| drop | | 释放全部 retain + 回收全部节点/字典 |
| len | `u8` | 当前键数 |
| is_empty | `bool` | 是否空 |
| cap | `u8` | 当前容量上限（0 = 无界） |
| set_cap | `cap: u8` | 调整容量（缩容立即淘汰 LRU 至 len<=cap） |
| has | `bool, key: const &` | 是否含键（**不触顶**） |
| get | `@, key: const &` | 取值并**触顶**（未命中空句柄；借用） |
| peek | `@, key: const &` | 取值**不触顶**（未命中空句柄；借用） |
| put | `bool, key: const &, value: @` | 插入/替换 + 触顶（retain 新、替换 release 旧；超容淘汰） |
| remove | `bool, key: const &` | 删除并 release（未命中 false） |
| clear | | 清空并 release 全部 value（保留容量） |
| mru_key | `const &` | 最近使用键（空返 nil） |
| lru_key | `const &` | 最久未用键（下一被淘汰；空返 nil） |
| each | `fn: lru_each_fn, ctx: &` | 按 **MRU→LRU** 顺序遍历（回调返 false 即停） |

回调类型 `lru_each_fn: bool, key: const &, value: @, ctx: &`（MRU→LRU，返 false 提前终止）。取出语义同 dict「取用分离」：`get`/`peek` 借用不改计数，`remove` 删除并 release。

完整用例见 `tests/cases/lru_at.sc`（字符串/定长键、get 触顶 vs peek 不触顶、超容淘汰/set_cap 缩容、replace、mru_key/lru_key、MRU→LRU 遍历全接口）。

### 使用示例


完整示例见 `examples/feature6.sc`：

```sc
inc adt.sc

fnc main: i4
    var s: string              # 声明即构造
    s.append("hello")
    printf("%s len=%llu\n", s.cstr(), s.len())
    s.drop()                   # 手动析构
    return 0
```

## op —— 语法机制模块（默认导入）

`builtins/op.sc` 与 `platform.h` 一样属**默认导入**，无需 `inc`。它汇集编译器
需要感知的「语言底层（语法层面）机制」声明，据此识别方法调用糖、字段访问、
链表注入等语法糖。它同样作为正式单元进入模块图、生成 `op.c`，其 C 运行时
`op_impl.c` 经拼接机制并入同一 TU（无需 inc）：

| 文件 | 角色 |
|------|------|
| op.sc | sc 侧机制声明（operand、chain；后续切片/容器/COM 等） |
| op.h | C ABI 契约（chain 结构体/原型），随 `platform.h` 默认带入每个生成单元 |
| op_impl.c | 机制运行时默认实现，经拼接机制并入 op 单元 `.c`（同一 TU） |

### operand —— 设备操作数通用指令

`operand` 伪结构体的每个成员函数声明一条作用于**基础/任意类型操作数**的通用
指令。语法上对基础类型（`i4`、`type&` 等）扩展 `.` 操作调用：`v.get()` /
`p->set(x)`。scc 不生成 `operand_xxx`，而是透传为 `platform.h` 的同名
`sc_<op>` 宏（接收者以指针传入，值接收者自动取址）；指令类型无关
（C 侧 `__typeof__` 推导）。新增指令时在 op.sc 加一行 `fnc`、并在 platform.h
加同名 `sc_<op>` 宏即可。

| 指令 | 调用 | 透传 | 说明 |
|------|------|------|------|
| get | `v.get()` | `sc_get(&v)` | 原子读（relaxed） |
| set | `v.set(x)` | `sc_set(&v, x)` | 原子写（relaxed） |
| get_acq | `v.get_acq()` | `sc_get_acq(&v)` | 原子读（acquire） |
| set_rel | `v.set_rel(x)` | `sc_set_rel(&v, x)` | 原子写（release） |

### chain —— 侵入式双向链表

配合链表结构体 `def T: ~ {}` 使用（编译器在 T 成员首位注入 `void *_prev` /
`void *_next`）。`chain` 为 op.sc 默认导入机制，**无需 `inc`**；C 结构体与运行时
由 `op.h` / `op_impl.c` 自动提供。机制约定：

- `head` 指向首元素；首元素的 `_prev` 指向尾元素（即 rear），尾元素的
  `_next` 为 `nil`——取队尾 O(1)，正向遍历以 `nil` 结尾。
- 同一 `chain` 只能存放**同一种**结构体；`_prev`/`_next` 固定在元素首部。
- **不拥有元素**：pop/remove/cut 只摘除链接，不释放元素内存。
- 元素入链前无需初始化 `_prev`/`_next`；同一元素不可同时挂在两条链上。

| 方法 | 签名 | 说明 |
|------|------|------|
| append | `it&:` | 添加到队尾 |
| push | `it&:` | 添加到队首 |
| pop | `&` | 移除并返回首元素（空返回 nil） |
| before | `pos&:, it&:` | 插入到 pos 前面 |
| after | `pos&:, it&:` | 插入到 pos 后面 |
| remove | `it&:` | 摘除指定元素 |
| first | `&` | 首元素（空返回 nil） |
| last | `&` | 尾元素（即 first 的 `_prev`） |
| revert | | 首尾翻转 |
| append_to | `dst&: chain` | 整链接到 dst 尾部，自身清空 |
| push_to | `dst&: chain` | 整链接到 dst 头部，自身清空 |
| cut | `from&:, to&:, out&: chain` | 截取 `[from..to]` 段为新链 out（out 被覆盖） |
| sort | `cmp: chain_cmp` | Simon Tatham 自底向上 O(n log n) 归并排序（稳定、原地，与 utlist `DL_SORT` 同源） |

比较回调类型 `@fnc chain_cmp: i4, a: &, b: &`：实参为元素节点首址，用 `(a: T&)`
还原回元素再比较，返回 `<0 / 0 / >0`（升序时 `a` 应在 `b` 前则负值，取相反符号
得降序）；等键保持原序，排序后 `head._prev`/`rear._next` 约定不变。

```sc
def task: ~ { id: i4 }

var l: chain                  # chain 默认可用，无需 inc
var t[3]: task
l.append(&t[0])
l.append(&t[1])
l.push(&t[2])              # 2 0 1
var it&: task = l.first(): task&
while it != nil
    printf("%d ", it->id)
    it = it->_next
```

完整示例见 `examples/feature6.sc`。

### stringify(...) —— 类型 JSON 格式化（关键字）

`stringify` 是 JSON 格式化关键字（区别于类型 `string` 与堆构造
`string()`）：编译器按实参的静态类型合成格式化函数。选项类型 `stringify_t`
属语言底层机制（默认导入，C ABI 见 `op.h`，无需 `inc`）；格式化器返回
`string`，故仍需 `inc adt.sc`。转 C 时按类型生成的
静态内联格式化器写入独立的 `stringify.h`，由生成的 `.c` 在类型定义之后 `#include`。
两种形态按实参个数静态派发：

```sc
var s: string = stringify(t)       # 默认多行美化，返回 string，调用方负责 drop
print("t=%s", s.cstr())
s.drop()

var b[256]: char
print("t=%s", stringify<compact:1>(t, b, 256)) # 紧凑 + 缓存：截断保证 NUL 结尾，
                                   # 返回 char&（即缓存首址），无需 drop
```

选项块 `stringify<key:val, ...>(值)`：编译器据此构造 `(stringify_t){...}` 传入格式化器，
值限整数字面量。当前支持 `compact`：`compact:1` 紧凑单行 `{"x":3,"y":4}`；默认
（无选项 / `compact:0`）多行美化，对象/数组逐层 2 空格缩进、嵌套换行。

格式化规则（输出合法 JSON，对象键加双引号）：

| 实参类型 | 输出 |
| --- | --- |
| 整数 / 浮点 / 枚举 | 数字（`%g` 浮点） |
| bool | `true` / `false` |
| char | `'a'` |
| char&（C 字符串）/ char 一维数组 | `"文本"`，nil → `nil` |
| 结构体指针（成员） | `"类型名@0x地址"`（不深递归），nil → `nil` |
| 标量指针（成员） | `"&值"`（取值输出），nil → `nil` |
| 其他指针（`void&` / 多级） | `"0x` 十六进制地址"，nil → `nil` |
| 结构体 / 联合体（值） | `{"字段": 值, ...}` JSON 对象；子成员为结构体（值）递归展开；函数指针字段与合成字段（`_prev` 等）跳过 |
| 结构体 / 联合体一级指针（顶层实参） | 自动解引用展开，nil → `"nil"` |
| 一维数组 | `[v, v, ...]` |
| string | `"内容"` |

限制：暂不支持多维数组（编译报错）；联合体按全部字段展开（按值语义读取）。

## m —— 多线程语言支持标准

多线程将逐步成为 sc 语言特性的一部分，本模块是其支持标准；后续按
语言特性需要扩展（条件变量/原子操作/线程局部存储等）。

> **跨线程协同的完整机制规格**（PORT 单收件箱、sync 铁律、循环死锁替代、
> `deferred` 延迟应答与 C 转译方案）见 `builtins/mt.md`；本节为 API 速查。

目录结构（`builtins/mt/`）：mt.sc（sc 侧接口声明）、mt.h（C ABI 契约）、
mt_impl.c（默认实现，跨平台经由 `platform.h`：POSIX pthread / Windows 线程）。
Linux 等平台链接时自动追加 `-lpthread`。

句柄约定：`h&:` 为实现私有指针（void 指针），调用方不直接访问；结构布局因此
跨平台稳定。

### run 语句与 thread —— 线程

线程由 `run` 语句创建（语言特性），目标必须是 **rpc 调用**：rpc 参数
天然可打包，正好作线程上下文，无需额外定义入口函数类型。
第二参数决定执行形态：

```sc
run work(a, b)        # detach：独立线程，结束后自释放
run work(a, b), &t    # joinable：t&: thread，须 t->join() 等待并回收
run<p> work(a, b)     # 入池：p 为 pool&（op 层接口协议，inc mt.sc 经 default_pool 构造）
```

实现机制：run 内部单次分配 `sizeof(thread) + sizeof(rpc参数) + 实现私有区`
的联合实体，rpc 参数紧随 thread 对象之后（`p + sizeof(thread)` 即参数），
线程实体与参数同生命周期。语法层面能拿到的 thread 必为 joinable，
所以 thread 对象非常简洁：

| 成员/方法 | 类型/签名 | 说明 |
|------|------|------|
| id | `u8` | 跨平台统一线程 id（线程启动后由其自身填写） |
| join | | 等待结束并回收（含 thread 对象本身，之后指针失效） |

线程休眠用 sys 模块的 `usleep(us)`（`inc sys.sc`）。

thread 不可手工构造（无 init）；`run` 是唯一创建途径。

### mutex —— 互斥锁

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造（声明即构造适用） |
| drop | | 析构 |
| lock / unlock | | 加锁 / 解锁 |
| try_lock | `bool` | 取锁成功返回 1，已被占用返回 0 |

### cond —— 条件变量

条件等待由 cond 的 `wait` 方法完成（普通方法调用，映射到 C 侧 `cond_wait`）：

```sc
c.wait(&mu)            # 无限等待（调用前须已持有 mu）
c.wait(&mu, nsec, sec) # 超时等待（nsec/sec 省略或全 0 等价于无限等待）
```

接收者为 cond（对象自动取地址）；首参为 mutex 指针（对象须显式 `&mu`）。
返回 i4：0 被唤醒 / 1 超时 / -1 错误；被虚假唤醒需循环复查条件。
Windows 下超时精度为毫秒（纳秒向上取整）。

| 方法 | 签名 | 说明 |
|------|------|------|
| init / drop | | 构造 / 析构 |
| one | | 唤醒一个等待者 |
| all | | 唤醒全部等待者 |
| wait | `i4, m: mutex&, nsec: u8, sec: u8` | 条件等待，返回 0 唤醒 / 1 超时 / -1 错误 |

### pool —— 线程池（op 层接口协议）

pool 是 `run` 语句的另一种执行目标，也是 op 层定义的「线程池接口协议」
（仿 com：vtable 全为每对象方法指针，默认导入；C 结构体见 op.h）。任务提交
复用 run 语句，没有新增提交方法——“线程”与“线程池”在语言层面是同一个动词。
实现模块 mt 按策略提供具名构造（当前默认 `default_pool(n)`，返回 `pool&`，
犹如 io 的 `file()` 之于 com）：

```sc
inc mt.sc

var p: pool& = default_pool(4)  # 接口协议：mt 按默认策略构造（0 → CPU 核数）
run<p> work(&c, 1000)           # 入池排队（与独立线程同一语句）
p->join()                       # 屏障：等全部已提交任务完成（之后仍可提交）
p->drop()                       # 析构：等任务完成 → 停工作线程 → 回收
```

| 成员 | 签名 | 说明 |
|------|------|------|
| run | `bool, fn, params, psize` | 入池一个 rpc 任务（编译器经 run 语句派发，见 op.h） |
| join | | 屏障：等待全部已提交任务完成（可反复使用） |
| drop | | 析构：等已提交任务全部完成后停池回收（含 pool 对象本身，不丢任务） |

构造入口（mt，inc mt.sc）：`default_pool(n: u4) -> pool&`——默认策略
（FIFO 队列 + n 个固定工作线程）。将来可按其它策略另起 `*_pool(n)`。

按类型静态分派机制：编译器在生成 run 语句时推断第二参类型——
`pool&`（值则自动取址）→ 经协议指针 `p->run(p, fn, &参数, sizeof)` 入池；其余
（`&t` 形态）→ 生成 `thread_run` 创建独立线程。入池经协议指针派发，语言内核
零 emit mt 符号（彻底解耦 mt 与语言）。任务节点延续联合分配哲学：`[节点][rpc
参数]`单块分配，参数拷贝入节点，调用点无需保活。

刻意不提供 future/cancel/动态扩容：任务级同步用 cond + wait 方法，
需要隔离时建多个 pool。

#### drain_pool —— pool 的「按需自调度」策略

`pool` 是 sc 对「线程调度组件」的核心抽象，**投递动词只有 `run`**——`default_pool` 与
`drain_pool` 都是 `pool&`、都凭 `run` 投递，只是运行时 `run` 指针指向不同策略：

| 策略 | `run<p> f()` 的 f | worker |
|------|------|------|
| `default_pool(n)` | 入内部 FIFO 队列，**执行一次** | n 个常驻，阻塞等下一个任务 |
| `drain_pool(n)` | 工作单元 rpc，worker **反复跑**直到一轮无新投递 | 按需 0..n，无新 `run` 即退 |

`drain_pool` 无任务队列，`run<dp> f()` = 通知有新活 + 按需激活一个 worker；工作单元 rpc
自身循环排空至「本视角无活」后返回。适合「任务在外部图/队列里、由应用自调度」的场景
（如 workflow 的 `back` drain）。

```sc
inc mt.sc

rpc work_unit: id: i4                 # 工作单元：自身循环排空至「本视角无活」后返回
    while <还有活>
        <处理一单位>
    return 0

var dp: pool& = drain_pool(4)         # 上限 4 worker；构造时不启 worker
run<dp> work_unit(0)                  # 生产者投放后调用：通知有新活，内部按需激活 worker
dp->join()                            # 屏障：等当前 worker 全部退出（running→0）
dp->drop()                            # 析构：置停 → 等 worker 退出 → 回收
```

构造入口（mt，inc mt.sc）：`drain_pool(n: u4) -> pool&`。
**一致性**：`running`（在跑 worker 数，本质信号量）与「有新活」世代 `gen` 由池内部锁
守护，经**世代代检**消除丢唤醒（`run` 先 `gen++` 再判 `running<max` 激活；worker 每轮跑
work_unit 前快照 `gen`、返回后锁下复检 `gen` 变则再来一轮）——故应用层无须再手搓「在跑
计数 + 补投」信号量，这正是 `pool` 抽象该承担的职责。

### queue —— 消息队列（op 层接口协议）

queue 与 com/pool 同属 op 层「接口协议」（vtable 全为每对象方法指针，默认导入；
C 结构体见 op.h）。实现模块 mt 按策略提供具名构造（当前默认 `default_queue(host)`，
返回 `queue&`）。`<<` 投递把一个 rpc 调用整体打包成消息入队，`pull` 在当前线程
取一条消息执行：

```sc
inc mt.sc

rpc add: a: acc&, v: i4               # rpc 即消息处理体，参数即消息载荷
    a->sum = a->sum + v

var q: queue& = default_queue(main)   # 宿主=main：当前线程自跑 pull 循环消费
q << add(&a, 10)                      # 投递：rpc 整体打包成消息入队（不触发本地调用）
q << add(&a, 20) << add(&a, 30)       # 链式投递多条
while q->pull(0) > 0                  # 排空：取一条执行，队空返 0 退出
    skip
q->drop()                            # 析构：解绑宿主 → 排空残留 → 回收
```

| 成员 | 签名 | 说明 |
|------|------|------|
| post | `bool, fn, params, psize, prio: i4, delay_ms: i8` | 投递一条 rpc 消息（编译器经 `<<` 派发，见 op.h）；`<<` 恒传 prio=0/delay=0 |
| sync | `i4, fn, params, psize, prio: i4, delay_ms: i8, timeout_ms: i8` | 阻塞带回复（编译器经 `sync<q> work(args)` 派发）：投递后阻塞至别的消费者执行完，结果回填 `params` 首字段返回槽 `_`；`timeout_ms<=0` 无限等 / `>0` 最多等 N 毫秒。返回 0 成功 / 1 超时 / -1 已关闭或失败（可经可选状态出参 `sync<q, timeout:ms> w(), &st` 取回）。**铁律（R2）**：消息一旦被 pull 开始执行（PULLING），调用方必死等至完成（绝不放弃）；`timeout>0` 仅在消息**尚未被 pull**（QUEUED）时干净撤回、零执行、返回超时——故会话寄存调用方栈即安全，无需堆影子会话。同线程 sync 到自己消费的队列、或循环互锁（A↔B），运行时改为本地直接执行该 rpc（循环死锁替代，P5d；消费者即谁 pull）；线程池宿主队列天然禁用替代 |
| async | `promise&, fn, params, psize, prio: i4, delay_ms: i8` | 非阻塞带回复（编译器经 `async<q> work(args)` 派发）：投递后立即返回 `promise&`，消费者执行完后兑现，调用方 `p->wait()` 阻塞取结果；参数缓冲堆分配由 promise 拥有。返回 NULL=投递失败 |
| pull | `i4, timeout_ms: i8` | 取一条消息在当前线程执行；`<0` 无限等 / `0` 立即 / `>0` 毫秒；返回 1 处理一条 / 0 超时空 / -1 已关闭 |
| drop | | 析构：解绑宿主 → 排空残留消息（ready + delaying 两链）→ 回收（含 queue 对象本身） |

构造入口（mt，inc mt.sc）：`default_queue(host: pool&) -> queue&`——默认 FIFO 策略。
宿主三态（`host`）：`nil` 未绑/延迟、`main` 当前/主线程（手动 pull）、
`&pool` 线程池消费（`<<` 自动转交 `pool->run` 并发消费，用 `p->join()` 作屏障）。
`main` 是 op 提供的 `pool&` 哨兵常量（值 -1）。

`<<` 投递经协议指针派发（`q->post(q, work_rpc, &参数, sizeof)`），语言内核零 emit
mt 符号。消息节点延续联合分配哲学：`[节点][rpc 参数]`单块分配，参数拷贝入节点，
投递点无需保活。`queue&` 是协议指针，方法用箭头：`q->pull(...)` / `q->drop()`。

`sync<q> work(args)` 阻塞带回复经 `q->sync(q, work_rpc, &参数)` 派发（语句表达式求值，
结果取 `参数._`）。实现上 `sync` 把 (fn, 参数缓冲指针, 应答描述符) 打成 trampoline 经
`post` 投递，消费者在调用方参数缓冲上实跑 rpc 后置位唤醒，同一机制兼容「另线程
pull」与「线程池宿主」两种消费路径。宿主必须是别的线程，同线程 sync 到自身死锁。

`async<q> work(args)` 非阻塞带回复经 `q->async(q, work_rpc, &参数, sizeof)` 派发（语句表达
式求值为 `promise*`）。与 sync 不同：async 不阻塞，可先连发多个拿 `promise` 再逐个
`wait`（投递与取值解耦）。实现上 `async` 堆分配 promise 盒子（嵌返回缓冲），经 `post`
投递蹦床，消费者在堆缓冲上实跑 rpc 后兑现并唤醒 `wait`。

`sync`/`async` 可在选项块首项后跟 `prio:N, delay:ms` 尖括号选项（值为任意表达式，仿 `run<...>`）：
`prio` 高者先被消费（同优先级稳定 FIFO），`delay` 入延迟链按到期时刻升序、`pull`
阻塞至成熟再消费。**二者仅作用于 FIFO-pull 消费路径**（`nil`/`main` 宿主被 `pull` 时）；
**池宿主队列忽略** prio/delay（投递即转交池并发自调度）。`<<` 不带选项（恒 prio=0/
delay=0），带优先级/延迟的即发即忘用 `async<q, ...>` 丢弃返回的 promise。`sync` 另支持
`timeout:ms` 把「无限等」限为有限超时（见 sync 行铁律）；可加状态出参 `, &st`（0=成功/
1=超时/-1=关闭）区分（须同时带 timeout）。`async` 暂不支持 timeout。循环死锁替代已落地
（P5d，见 sync 行与 `builtins/mt.md` §6）。详见 syntax.md §15.3 与 `examples/feature42.sc`。

#### deferred——rpc 延迟应答句柄（op 层接口协议）

`sync<q> work(args)` 默认由 rpc 体 `return` 即时应答。若消费者无法当场算出结果（等 io、
攒批、转交），可在被 sync 驱动的 rpc 体内裸 `async`（不接调用、直接换行）取出当前待应答
调用，求值为 `deferred&`，本次 sync 转为**延迟应答**（rpc 体 `return` 值被忽略）；之后任意
线程、任意时刻 `done s, result` 兑现——写回最初调用方返回槽并唤醒，调用方一直阻塞到那刻。

机制与 future（fnc 单线程异步 `async`→`future`→`done`）对称，仅换一层语境（mt 跨线程
应答）。`deferred` 与 queue/promise 同属 op 层「接口协议」（vtable 全为每对象方法指针，
默认导入；C 结构体 `{ h; respond }` 见 op.h），由实现模块 mt 填充 `respond` 指针——故
`done` 经协议指针派发、语言内核**零 emit mt 符号**。待应答调用身份（当前正执行的 rpc 调用）由
op 内核按线程 TLS 维护（`op_deferred_begin/current/taken`，op_impl.c，与异步后端无关）。

| 关键字 | 形态 | 说明 |
|------|------|------|
| `async`（裸） | rpc 体内、其后换行 | 取当前 sync 待应答调用 → `deferred&`；本次 sync 转延迟应答。与 `async rpc(...)`（→`future&`）按是否接调用区分 |
| `done s [, r]` | 语句 | 延迟应答：`r` 按其**静态类型原值**写回调用方返回槽（与即时应答原地写 `_` 同布局/宽度，非 future 的 `void*` 擦除）；省略 `r`=无结果。`done` 按操作数类型多态（`future&`→`future_done`；`deferred&`→延迟应答） |

延迟应答句柄寄存调用方栈（随其阻塞存活）；调用方超时/drop 放弃后，将来的 `done` 安全丢弃
（会话置 CLOSED）。循环死锁替代/自替代路径（本地直接执行）不支持延迟应答（防误领，置空
会话）。详见 syntax.md §15.3 与 `examples/feature45.sc`。构造由 mt 在 `sync` 内部完成
（会话嵌在调用方栈帧），无独立构造入口。

#### promise——mt-future（async 投递的结果句柄）

promise 与 queue/pool/task 同属 op 层「接口协议」（vtable 全为每对象方法指针），由
`queue.async` 具名构造返回。与 libuv `future`（单线程协作、绑事件循环）不同，promise
是线程世界的阻塞型未来（内部 mutex+cond）：消费者执行完 rpc 后兑现，调用方 wait 取。

| 成员 | 签名 | 说明 |
|------|------|------|
| ready | `bool` | 非阻塞轮询：是否已完成 |
| wait | `&` | 阻塞至完成，返回结果（类型擦除 `void*`，调用点 `p->wait(): T` 还原，与 `future.get()` 同语义） |
| drop | | 释放（含堆参数缓冲与 promise 对象本身） |

生命周期：参数缓冲与返回槽由 promise 堆拥有，投递后调用方无需保活；须先 `p->wait()`
取结果再 `p->drop()`（消费者兑现前 drop 会 UAF，引用计数化安全释放为后续）。`promise&`
是协议指针，方法用箭头。

> 示例：`nil`/`main` 手动 pull 见 `examples/feature38.sc`；`&pool` 自动并发消费
> 见 `examples/feature39.sc`；`sync<q> ...` 带回复（池宿主 + 专用消费线程）见
> `examples/feature40.sc`；`async<q> ...` 返 promise（同两种消费路径）见 `examples/feature41.sc`。

### 使用示例

完整示例见 `examples/feature7.sc`：

```sc
inc mt.sc

def ctx: {
    mu: mutex
    n: i4
}

rpc work: c&: ctx, rounds: i4      # rpc 即线程体，参数即线程上下文
    var i: i4 = 0
    for i = 0; i < rounds; i++
        c->mu.lock()
        c->n = c->n + 1
        c->mu.unlock()

fnc main: i4
    var c: ctx
    c.n = 0
    c.mu.init()        # 嵌套字段不自动构造，手动 init
    var t&: thread = nil
    run work(&c, 10000), &t    # joinable
    run work(&c, 10000)        # detach（仅示意，结束后自释放）
    t->join()                  # 等待并回收，之后 t 失效
    usleep(50000)              # 等 detach 线程结束（sys）
    c.mu.drop()
    printf("n=%d\n", c.n)
    return 0
```

## io —— 输入输出子项目

`inc io.sc` 引入 file / stream / tcp 三种 com 设备原语（声明见 io.sc、
C ABI 见 io.h、默认实现 io_impl.c）。`print` 关键字不属于 io，见下。

### print —— C 风格日志输出（关键字）

`print` 是编译器内置关键字，运行时由 `op_impl.c` 提供（C ABI 见 op.h；
op 模块始终链接，**无需 inc**）。第一参数为通道 `chn`（`print<级别>`）：

```sc
print("n=%d s=%s", 42, "hello")    # 通道 0：纯 stdout，无色、不过滤
print<E>("错误 code=%d", -1)        # 通道 E：错误级别（红），彩色 stdout
print<W> "内存偏低"                  # 通道 W：警告级别（黄）
```

- 通道即级别：`def log` 枚举 `F/E/W/I/D/V = 1..6`（见 op.sc / op.h）——
  F 致命(紫) / E 错误(红) / W 警告(黄) / I 状态(默认色) / D 调试(青) /
  V 详尽(灰)；无 `<>` 即通道 0，纯 stdout、不着色、不过滤。
- 格式串与 printf 完全一致（vsnprintf 实现，参考 stdc 的简化移植）；
  自动补换行，单行上限 2048 字节（编译期 `-DSC_PRINT_BUF=N` 可覆盖），超出截断。
- 着色：仅当输出为终端（isatty）且级别为 1..6 时按级别加 ANSI 颜色；
  重定向到管道/文件时退化为纯文本；Windows 自动开启 VT 序列。
- 过滤：环境变量 `SC_LOG=F/E/W/I/D/V`（首次 print 时读取一次），默认 `D`
  （级别数值大于阈值即丢弃，故默认 V 不输出）；通道 0 不受过滤。
- 系统日志：设 `SC_LOG_SYS=1` 时，级别 1..6 的日志经 `P_log_sys`
  （platform.h）镜像到系统日志——Windows `OutputDebugString`、
  macOS/iOS `os_log`、Android `logcat`、QNX `slog2`、Linux/BSD `syslog`。
- `print` 是上下文关键字：作用域内存在同名函数/变量时按普通标识符解析。

## sys —— 运行环境 / 系统路径子项目

`inc sys.sc` 引入。提供跨平台的系统路径查询，全部为 **C 侧实现的自由函数**
（`@fnc name::` 无函数体，链接期由 `sys_impl.c` 注入）。平台适配统一经由
`platform.h`（`P_WIN`/`P_DARWIN`/`P_BSD`/`P_LINUX`），实现内不散落平台分支。

目录结构（`builtins/sys/`）：sys.sc（sc 侧声明）、sys.h（C ABI 契约 +
返回码）、sys_impl.c（默认实现）。

### 调用约定

所有函数把结果路径写入调用方提供的 `buf`（NUL 结尾），`size` 为字节容量，
返回 `ret`（即 i4 语义别名）：

| 返回码 | 含义 |
|--------|------|
| `0` | 成功 |
| `-1` | 系统调用失败（`SYS_ERR`） |
| `-2` | `buf` 容量不足（`SYS_ERR_CAPACITY`） |

建议 `buf` 至少 `PATH_MAX`（4096）字节；`download_dir`/`exe_file` 在部分平台
要求 `buf >= PATH_MAX`，过小返回 `-2`。

| 函数 | 签名 | 说明 |
|------|------|------|
| sys_work_dir | `ret, buf: char&, size: u4` | 当前工作目录（cwd） |
| sys_home_dir | `ret, buf: char&, size: u4` | 用户 home：POSIX 优先 `$HOME`，回退 `getpwuid_r`；Windows 取用户配置目录 |
| sys_download_dir | `ret, buf: char&, size: u4` | 下载目录：Win 已知文件夹 / macOS sysdir / Linux `$XDG_DOWNLOAD_DIR` 回退 `~/Downloads` |
| sys_exe_file | `ret, buf: char&, size: u4` | 当前可执行文件的规范化绝对路径 |
| sys_tmp_file | `ret, buf: char&, size: u4` | 在系统临时目录**创建**一个唯一空临时文件并返回路径（调用方负责删除） |

实现移植自 stdc 的 `P_*` 同名函数，并修正若干健壮性问题：work_dir 在
Windows 补全缓冲不足判定；home_dir 改用线程安全的 `getpwuid_r` 并优先
`$HOME`；download_dir 在 macOS 用 `PATH_MAX` 局部缓冲承接 `sysdir` 写入以
避免越界；exe_file 未知平台优雅返回错误（不再 `#error`）；tmp_file 两端
统一「真实创建唯一空文件」语义（Windows 用 `GetTempFileNameA`）。

> Windows 链接：`SHGetKnownFolderPath` / `CoTaskMemFree` 依赖 `shell32` 与
> `ole32`。MSVC 已由 `#pragma comment(lib, ...)` 自动链接；MinGW/clang 需
> 自行追加 `-lshell32 -lole32`。

### 使用示例

```sc
inc sys.sc

fnc main: i4
    var b[4096]: char
    if sys_work_dir(b, 4096) == 0
        printf("cwd = %s\n", b)
    if sys_exe_file(b, 4096) == 0
        printf("exe = %s\n", b)
    return 0
```

## mem —— 内存池子项目

`inc mem.sc` 引入。提供池化内存管理，作为 `malloc`/`calloc`/`realloc`/`free`
的高性能替代：小对象走每线程 TLS 堆（无锁），大对象（>64KiB）直走系统分配；
跨线程释放经无锁 MPSC 队列回收到属主线程。另提供 `arena` 竞技场用于批量同
生命周期分配，以及 `shm` 跨进程命名共享内存。核心为 **C 侧自由函数**（`@fnc
name::` 无函数体，链接期由 `mem_impl.c` 注入），平台适配经由 `platform.h`
（`TLS` + `sc_*` 原子）。

目录结构（`builtins/mem/`）：mem.sc（sc 侧声明）、mem.h（C ABI 契约）、
mem_impl.c（默认实现）。

### 自由函数

| 函数 | 签名 | 对应 | 说明 |
|------|------|------|------|
| chunk | `&, size: u8` | malloc | 申请至少 `size` 字节；`size==0` 当 1 处理，永不返回 nil（除非内存耗尽） |
| chunk0 | `&, size: u8` | calloc | 同 chunk 但内容清零 |
| chunk_array | `&, count: u8, size: u8` | calloc(n,size) | 分配并清零 `count*size` 字节；`count*size` 溢出（超过 `SIZE_MAX`）返回 nil |
| chunk_aligned | `&, size: u8, align: u8` | aligned_alloc | 起始地址对齐到 `align`（须 2 的幂）；`align<=16` 退化为 chunk，非 2 的幂返回 nil。块可 recycle/refit/mem_usable。用于 SIMD（32/64）、缓存行（64）、页对齐 |
| refit | `&, p: &, size: u8` | realloc | 扩缩到 `size` 并保留旧内容；`p==nil` 等价 chunk；`size==0` 等价 recycle 并返回 nil；超对齐块 refit 保持原对齐 |
| recycle | `p: &` | free | 归还内存到池；`p==nil` 安全；**可由任意线程调用**（跨线程释放安全） |
| mem_usable | `u8, p: &` | malloc_usable_size | 返回该块实际可用字节数（≥ 申请值，因按尺寸档对齐） |
| mem_trim | `u8` | malloc_trim | 归还**当前线程**空闲堆页回 OS，返回释放字节数；仅当本线程无存活分配（`count==0`）时生效，否则保守不动。适合工作线程闲暇时压缩驻留 |
| mem_stat | `out: mem_stat_t&` | mallinfo | 填充统计快照（见下）；`out==nil` 安全空操作 |
| mem_teardown | （无） | — | 归还所有池化页与线程堆，并清零全部统计；**仅在所有线程静止时调用**（通常进程退出前） |

### 内存统计 mem_stat

`mem_stat(&s)` 填充一个 `mem_stat_t` 快照：

| 字段（`u8`） | 含义 |
|------|------|
| reserved | 向 OS 申请并仍持有的总字节（池化页 + 活跃大对象，含对象头），近似内存占用 |
| live | 当前分配给用户的可用字节（usable 口径，小对象按尺寸档计） |
| peak_live | `live` 历史峰值（最高水位）。**单线程精确**；多线程下为各线程峰值之和的上界 |
| count | 当前活跃（未归还）分配块数 |
| allocs | 累计成功分配次数 |
| frees | 累计成功归还次数 |

- 恒等式 `count == allocs - frees`（含在途的跨线程归还）；`reserved >= live`。
- **快照口径**：小对象遍历各线程 TLS 堆累加（无锁），大对象走全局原子计数。
  并发分配/释放时各线程堆计数为近似值；需精确一致请在所有线程静止时调用。
- **跨线程归还延迟计入**：A 线程分配、B 线程 `recycle` 的块，在物主线程 A
  下次分配并回之前仍计入 `live`/`count`，尚未计入 `frees`。
- `arena` 走独立分配器，**不计入** `mem_stat`。
- `mem_teardown` 会把全部统计清零（含累计 allocs/frees）。

### arena 竞技场

`arena` 用于批量、同生命周期的分配：逐块分配但整体释放，无需对每块调用
`recycle`。适合「一次请求/一帧/一轮解析」内大量小对象的场景。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `cap: u8` | 初始化，`cap` 为每块底层页的建议容量（`0` 取默认 64KiB） |
| chunk | `&, size: u8` | 从竞技场线性分配 `size` 字节（16 对齐）；容量不足时自动追加新页 |
| reset | （无） | 重置游标复用已有页，**不释放内存**（快速重用） |
| drop | （无） | 释放竞技场全部页，置 `h=nil` |

### 设计与多线程语义

- **分尺寸档空闲链**：1..65536 字节映射到 44 个 16 对齐尺寸档，回收即入对应
  空闲链，再分配 O(1) 命中。对象头 16 字节（owner + info，按「已分配 / 空闲 /
  跨线程」状态分时复用）。
- **每线程 TLS 堆**：小对象分配/本线程释放全程无锁。唯一共享状态是堆注册表
  与每堆的跨线程释放 MPSC 队列，均用 `sc_*` 原子（CAS 入队 + 单消费者
  exchange 排空，ABA 安全）。已通过 TSan / ASan 200 万次跨线程分配/释放压测。
- **线程退出堆回收**：线程结束时其私有堆被标记为「废弃」，新线程优先抢占复用
  （CAS 标志 1→0，注册表只增不删故无 ABA），避免死线程堆永驻、其上未并回的
  跨线程释放永不消费。POSIX 经 `pthread_key` 析构器、Windows 经 FLS 回调自动触发。
- **保留语义与主动压缩**：底层页一经取得默认不归还 OS（换取分配热路径零 `mmap`）；
  需要「用完即还」时调用 `mem_trim()`（当前线程无存活分配时释放其全部空闲页），
  或对批量场景用 `arena` + `drop`，进程退出/测试用 `mem_teardown`。
- **超对齐对象**：`chunk_aligned` 经独立哨兵路径（不进尺寸档），过量分配后回退
  指针保存原始基址与对齐，故仍可 `recycle`/`refit`/`mem_usable`。
- **大对象旁路**：>64KiB 直接 `malloc`/`free`，不进尺寸档。
- **调试构建**：以 `-DMEM_DEBUG` 编译 `mem_impl.c` 时，对象头增设魔数守卫，
  `recycle`/`refit` 校验，捕获双重释放、释放非本池/已损坏指针（best-effort，
  缓冲区越界检测由编译器 `--check=mem` 负责）。默认关闭，零开销。
- **ABI**：sc `u8` ↔ C `uint64_t`（精确匹配，内部转 `size_t`）；返回指针
  恒 16 字节对齐（`max_align_t`）。

### 使用示例

```sc
inc mem.sc

fnc main: i4
    var p: & = chunk(100)
    var s: char& = p: char&
    sprintf(s, "pooled-%d", 42)
    printf("usable=%llu text=%s\n", mem_usable(p), s)
    p = refit(p, 5000)          # 扩容保留内容
    recycle(p)

    var a: arena                # 竞技场批量分配
    a.init(0)
    var i: i4 = 0
    for i = 0; i < 1000; i++
        var q: & = a.chunk(48)
    a.drop()                    # 整体释放

    mem_teardown()              # 进程退出前归还池
    return 0
```

### shm 跨进程命名共享内存

一块由 `name` 标识的命名内存区，可被多个进程映射到各自地址空间共享读写。
跨平台：POSIX 用 `shm_open` + `mmap`；Windows 用 `CreateFileMapping` + `MapViewOfFile`。

| 接口 | 类型/签名 | 说明 |
|------|---------|------|
| shm.make | `bool, name: const char&, size: u8, flags: u4` | 创建或附着命名区，映射 `size` 字节（0 为 1，向上取整到页）；已存在则附着（要求容量足够，`size()` 回报真实容量）。`flags`：0 默认读写、1 只读（`SHM_RDONLY`，仅附着不创建）、2 独占创建（`SHM_EXCL`，已存在则失败），可按位或。成功 1 / 失败 0 |
| shm.data | `&` | 本进程映射首地址；未映射返回 nil |
| shm.size | `u8` | 实际映射字节数（附着已存在区时为其底层真实容量）；未映射返回 0 |
| shm.drop | （无） | 解除映射 + 关闭句柄（不删除命名）；可重复调用 |
| shm_remove | `bool, name: const char&`（自由函数） | 删除命名区（POSIX `shm_unlink`）；Windows 无需删除返回 1 |

- **标志**：`SHM_RDONLY`(1) 以只读保护映射（POSIX `PROT_READ` / Windows `FILE_MAP_READ`），
  单独使用时仅附着已存在区、不创建；`SHM_EXCL`(2) 独占创建，区已存在则 `make` 失败
  （POSIX `O_EXCL` / Windows `ERROR_ALREADY_EXISTS`），用于「确保自己是创建者」。

- **命名约定**：用简单标记（字母/数字/下划线），勿含路径分隔符；POSIX 实现内部自动加前导 `/`。
- **生命周期**：POSIX 命名区一经创建即持久，所有进程 `drop` 后仍需 `shm_remove(name)` 才真正销毁；
  Windows 为内核对象引用计数，最后句柄关闭即销毁（`shm_remove` 空操作）。
- **并发**：共享页内存一致（同一物理页），区内并发读写同步由调用方负责（可将 m 模块原语或 `sc_*` 原子置于区内）。
- **链接**：较老 glibc 的 `shm_open`/`shm_unlink` 在 librt（需 `-lrt`）；glibc >= 2.34 已并入 libc；macOS/BSD 在 libc 内。

```sc
inc mem.sc

fnc main: i4
    var nm: char& = "my_region"
    var s: shm
    if s.make(nm, 4096, 0)
        var d: char& = s.data(): char&
        sprintf(d, "hello from pid")   # 其他进程 make(nm, ..., 0) 后可读到
        s.drop()                       # 解除本进程映射
    shm_remove(nm)                      # 销毁命名区（POSIX）
    return 0
```

## platform.h —— 跨平台基础头

`builtins/platform.h` 不是 sc 模块，而是面向用户的单头文件 C 跨平台层
（参考摘取自 stdc），builtins 内各 C 实现（adt_impl.c / mt_impl.c ...）
也统一经由它做平台适配。builtins 根目录默认加入编译 `-I`，因此
用户代码中 `inc "platform.h"` 与 `inc stdint.h` 一样开箱即用；
随其他 builtins 资源一并内嵌/释放。

提供：平台判定宏（`P_WIN`/`P_DARWIN`/`P_BSD`/`P_LINUX`/`P_POSIX`/
`P_POSIX_LIKE`）、平台基础头引入、路径分隔符（`P_SEP`/`P_IS_SEP`）、
`TLS` 线程局部存储、字节序（`BYTE_ORDER`）、时钟（`clk_t` 类型，
`P_time_now` 墙钟 / `P_clock_now` 单调 / `P_cost_now` CPU 耗时，
`clock_s/ms/us` 取值与 `*_diff` 差值等宏族，`sc_tick_s/ms/us` 快捷计时）。

新增 builtins 实现时应统一经由本头文件做平台适配，不在实现内散落
`#ifdef` 平台分支。
