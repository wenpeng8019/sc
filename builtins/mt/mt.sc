# mt —— sc 多线程语言支持标准（mutex/cond/barrier/pool/queue；thread 与 run 线程创建已下沉至 op.sc 内核）
#
# 本文件是 mt 的唯一事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分）
#   fnc name:: 方法声明（无函数体）：extern 原型，实现在 C 侧
#   @rpc 仅声明：调用包装由编译器生成，实际函数（*_rpc）在 C 侧实现
#
# 默认实现：同目录 mt_impl.c（编译器自动编译并链接，
#           跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程）
#
# 线程由 run 语句创建（语言特性，目标必须是 rpc 调用），
# 第二参数决定执行形态（按类型静态分派）：
#   run work(a, b)        # detach：独立线程，结束后自释放
#   run work(a, b), &t    # joinable：t: thread&，须 t->join() 等待并回收
#   run work(a, b), p     # 入池：p 为 pool&（op 层接口协议，本模块经 default_pool 构造）
# 其中 thread 类型与 detach/joinable 线程创建属语言内核（op.sc，默认导入，无需 inc）；
# pool 亦为 op 层接口协议（默认导入），本模块按「默认策略」提供其具名构造 default_pool
# 与工作线程实现，作为入池形态 run work(...), p 的执行目标。
#
# 条件等待由 cond 的 wait 方法完成（普通方法调用，映射到 C 侧 cond_wait）：
#   c.wait(&mu)            # 无限等待（调用前须已持有 mu）
#   c.wait(&mu, nsec, sec) # 超时等待（nsec/sec 省略或全 0 等价于无限等待）
#   返回 i4：0 被唤醒 / 1 超时 / -1 错误；被虚假唤醒需循环复查条件
#
# 实现机制：run 单次分配 sizeof(thread) + sizeof(rpc参数) + 实现私有区，
#   rpc 参数紧随 thread 对象之后（p + sizeof(thread) 即参数），
#   线程实体与参数同生命周期；语法层面能拿到的 thread 必为 joinable。
#
# 定位：多线程将逐步成为 sc 语言特性的一部分，本模块是其支持标准；
#       后续按语言特性需要扩展（原子操作/线程局部存储等）。

# 平台锁层 opt-in：拉起 platform.h 的互斥/条件/屏障层，使下方 @def 可直接以平台
# 句柄类型（::mutex_t 等，'::' 逃逸到 C 域）为字段——布局精确、无堆分配、无占位缓冲。
inc "mt_p.h"
inc os.sc                 # 默认线程池取 CPU 核数（os 的 sc_ncpu）

# ---------------- mutex：互斥锁 ----------------

@def mutex: {
    h: ::mutex_t      # 平台锁句柄（内联、无堆分配；实现私有，仅经 P_mutex_* 操作）

    fnc init::           # 构造（声明即构造适用）
    fnc drop::           # 析构
    fnc lock::           # 加锁（阻塞）
    fnc unlock::         # 解锁
    fnc try_lock:: bool # 取锁成功返回 1，已被占用返回 0
}

# ---------------- cond：条件变量（配合 wait 方法使用） ----------------

@def cond: {
    h: ::cond_t       # 平台条件变量句柄（内联；实现私有，仅经 P_cond_* 操作）

    fnc init::            # 构造（声明即构造适用）
    fnc drop::            # 析构
    fnc one::             # 唤醒一个等待者
    fnc all::             # 唤醒全部等待者
    # wait：条件等待，调用前须持有 m；nsec/sec 省略或全 0 → 无限等待，否则相对超时
    #       返回 0 被唤醒 / 1 超时 / -1 错误
    fnc wait:: i4, m: mutex&, nsec: u8, sec: u8
}

# ---------------- barrier：屏障（N 方汇合） ----------------

@def barrier: {
    h: ::barrier_t     # 平台屏障句柄（内联 = mutex+cond+计数；实现私有，仅经 P_barrier_* 操作）

    fnc init:: n: u4      # 构造：n 方汇合（0 视为 1）
    fnc drop::            # 析构
    fnc wait:: bool       # 汇合点：阻塞至全部到达；最后到达者返回 1，其余返回 0
}

# ---------------- pool：线程池协议的「默认」实现 ----------------
# pool 类型与 run/join/drop 方法是 op 层「线程池接口协议」（默认导入，vtable，
# 见 op.sc）；本模块按「默认策略」提供具名构造 default_pool，犹如 io 的 file()
# 之于 com——填充协议指针并返回 pool&。
#   var p: pool& = default_pool(4)   # 4 个工作线程（0 → CPU 逻辑核数）
#   run work(a, b), p                # 入池（经 p->run(...) 协议指针派发）
#   p.join()                         # 屏障：等全部已提交任务完成（pool 仍可用）
#   p.drop()                         # 析构：等任务完成 → 停池 → 回收
# 任务节点延续联合分配哲学：[节点][rpc 参数]，参数拷贝入节点，调用点无需保活；
# 不提供 future/cancel，任务级同步用 cond + wait 方法。
# 将来可按其它策略另起 *_pool(n) 构造（如 work-stealing / 优先级），均返回 pool&。
@fnc default_pool:: pool&, n: u4     # 构造默认线程池（FIFO 队列 + n 个固定工作线程）

# ---------------- sem_pool：pool 协议的「信号量限流」策略实现 ----------------
# sem_pool 与 default_pool / drain_pool 并列——同为 pool&、同凭 run 投递。它是「有界并发的
# 一次性 worker 派发器」：run = 任务入队 + 计数自增，永不阻塞；调度按「剩余信号量」(n - 在跑)
# 启动线程，每个 worker 只跑一个任务即退（线程结束、不自循环）；腾出槽位时把下一个排队任务
# 交给新线程。与 default_pool 的别（都封顶 n 并发、fn 各执行一次）：default_pool 是 n 个常驻
# 线程复用、空闲驻留；sem_pool 无常驻线程，按需起、一任务一线程、用完即弃（线程级隔离、空闲零驻留，
# 代价是每任务一次线程创建）。与 drain_pool 的别：drain_pool 反复重跑 fn 排空池外工作源（需世代代检）；
# sem_pool 的 fn 只跑一次、任务由池自有队列承载（不变式「在跑<上限 ⟹ 队列空」即足，无须代检）。
#   var sp: pool& = sem_pool(4)      # 并发上限 4；构造时不启线程
#   run<sp> work(a, b)               # 入队 + 计数；剩余信号量>0 即起线程跑一次
#   sp->join()                       # 屏障：等已提交任务全部完成（running→0 且队空）
#   sp->drop()                       # 析构：置停 → 弃排队任务 → 等在跑退出 → 回收
# C 结构体（vtable）同 pool（见 op.h）；构造、信号量调度与一次性 worker 见 mt_impl.c。
@fnc sem_pool:: pool&, n: u4         # 构造信号量限流池（并发上限 n；一任务一线程、用完即退）

# ---------------- drain_pool：pool 协议的「按需自调度」策略实现 ----------------
# drain_pool 与 default_pool 并列——同为 pool&、同凭 run 投递，仅策略相反：default_pool
# 是常驻 worker 消费内部 FIFO 任务队列（run = 入队，fn 执行一次）；drain_pool 没有任务队列，
# worker 反复跑投递的工作单元 rpc 直到一轮无新投递即退；run = 通知有新活 + 按需激活一个
# worker（上限 n）。适合「任务在外部图/队列里、由应用自调度」的场景（如 workflow 的 back
# drain：worker 自身 while 循环反向遍历认领可用节点，无节点可认领即返回）。
#   var dp: pool& = drain_pool(4)    # 上限 4 worker；构造时不启 worker
#   run<dp> work_unit(a, b)          # 通知有新活：内部按需补投一个 worker（反复跑 work_unit）
#   dp->join()                       # 屏障：等当前 worker 全部退出（running→0）
#   dp->drop()                       # 析构：置停 → 等 worker 退出 → 回收
# 工作单元 rpc 自身应循环排空至「本视角无活」后返回；drain_pool 经世代代检（running 计数 +
# 工作世代 gen 由池内部锁守护）保证：worker 返回后若期间有新 run 则再来一轮，否则 running--
# 退出——故末个 worker 不会在尚有活时退出，应用层无须再手搓「在跑计数 + 补投」信号量。
# C 结构体（vtable）同 pool（见 op.h）；构造、按需激活与世代代检见 mt_impl.c。
@fnc drain_pool:: pool&, n: u4       # 构造按需自调度池（上限 n worker；run<dp> 投递工作单元 rpc）

# ---------------- queue：消息队列协议的「默认」实现 ----------------
# queue 类型与 post/pull/drop 方法是 op 层「消息队列接口协议」（默认导入，vtable，
# 见 op.sc）；本模块按「默认 FIFO」策略提供具名构造 default_queue，犹如 io 的 file()
# 之于 com——填充协议指针并返回 queue&。
#   var q: queue& = default_queue(main)  # 宿主=当前/主线程（自行跑 pull 循环消费）
#   q << work(a, b)                      # 投递：rpc 整体打包入队（经 q->post(...)）
#   for q->pull(0) > 0                   # 排空：取一条执行，队空返 0 退出
#       skip
#   q->drop()                            # 析构：解绑 → 排空残留 → 回收
# 宿主三态（host: pool&）：nil 未绑/延迟、main 当前/主线程（手动 pull）、&pool 线程池消费。
# main 是 op 提供的 pool& 哨兵常量（值 -1）。宿主为真实线程池时，<< 投递自动转交
# 池并发消费（用 p->join() 等待排空，无需 pull；q、p 各自 drop）。消息节点延续联合
# 分配哲学：[节点][rpc 参数]，参数拷贝入节点，投递点无需保活。将来可按其它策略
# 另起 *_queue(host) 构造，均返回 queue&。
@fnc default_queue:: queue&, host: pool&   # 构造默认 FIFO 消息队列，host 三态绑定宿主
