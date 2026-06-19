# m —— sc 多线程语言支持标准（mutex/cond/barrier/pool；thread 与 run 线程创建已下沉至 op.sc 内核）
#
# 本文件是 m 的唯一事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分）
#   fnc name:: 方法声明（无函数体）：extern 原型，实现在 C 侧
#   @rpc 仅声明：调用包装由编译器生成，实际函数（*_rpc）在 C 侧实现
#
# 默认实现：同目录 m_impl.c（编译器自动编译并链接，
#           跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程）
#
# 线程由 run 语句创建（语言特性，目标必须是 rpc 调用），
# 第二参数决定执行形态（按类型静态分派）：
#   run work(a, b)        # detach：独立线程，结束后自释放
#   run work(a, b), &t    # joinable：t: thread&，须 t->join() 等待并回收
#   run work(a, b), p     # 入池：p 为 pool（对象或指针），任务排队执行
# 其中 thread 类型与 detach/joinable 线程创建属语言内核（op.sc，默认导入，无需 inc）；
# 本模块仅提供入池形态 run work(...), p 的 pool 执行目标。
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

# ---------------- mutex：互斥锁 ----------------

@def mutex: {
    h: &           # 平台锁句柄（实现私有）

    fnc init::           # 构造（声明即构造适用）
    fnc drop::           # 析构
    fnc lock::           # 加锁（阻塞）
    fnc unlock::         # 解锁
    fnc try_lock:: bool # 取锁成功返回 1，已被占用返回 0
}

# ---------------- cond：条件变量（配合 wait 方法使用） ----------------

@def cond: {
    h: &           # 平台条件变量句柄（实现私有）

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
    h: &                  # 实现私有区指针（mutex + cond + 计数，实现私有）

    fnc init:: n: u4      # 构造：n 方汇合（0 视为 1）
    fnc drop::            # 析构
    fnc wait:: bool       # 汇合点：阻塞至全部到达；最后到达者返回 1，其余返回 0
}

# ---------------- pool：线程池（run 语句的另一种执行目标） ----------------

@def pool: {
    h: &           # 实现私有区指针（队列 + 同步原语 + 工作线程）

    fnc init:: n: u4     # n 个工作线程；0 → CPU 逻辑核数
    fnc join::            # 屏障：等待全部已提交任务完成（后续仍可提交）
    fnc drop::            # 析构：等任务完成 → 停工作线程 → 回收
}

# 任务提交复用 run 语句（无新增方法）：run work(a, b), p
# 任务节点延续联合分配哲学：[节点][rpc 参数]，参数拷贝入节点，
# 调用点无需保活；不提供 future/cancel，任务级同步用 cond + wait 方法
