# m —— sc 多线程语言支持标准（run/wait/thread/mutex/cond）
#
# 本文件是 m 的唯一事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分）
#   @fnc T::m 方法声明（无函数体）：extern 原型，实现在 C 侧
#   @rpc 仅声明：调用包装由编译器生成，实际函数（*_rpc）在 C 侧实现
#
# 默认实现：同目录 m_impl.c（编译器自动编译并链接，
#           跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程）
#
# 线程由 run 语句创建（语言特性，目标必须是 rpc 调用）：
#   run work(a, b)        # detach：线程结束后自释放
#   run work(a, b), &t    # joinable：t&: thread，须 t->join() 等待并回收
#
# 条件等待由 wait 语句完成（语言特性，编译器生成 cond_wait 调用）：
#   wait c, mu            # 无限等待（调用前须已持有 mu）
#   wait c, mu, nsec, sec # 超时等待（nsec/sec 全 0 等价于无限等待）
#   c/mu 可为对象或指针，对象自动取地址；被虚假唤醒需循环复查条件
#
# 实现机制：run 单次分配 sizeof(thread) + sizeof(rpc参数) + 实现私有区，
#   rpc 参数紧随 thread 对象之后（p + sizeof(thread) 即参数），
#   线程实体与参数同生命周期；语法层面能拿到的 thread 必为 joinable。
#
# 定位：多线程将逐步成为 sc 语言特性的一部分，本模块是其支持标准；
#       后续按语言特性需要扩展（原子操作/线程局部存储等）。

# ---------------- thread：线程（run 创建，不可手工构造） ----------------

@def thread: {
    id: u8        # 跨平台统一线程 id（线程启动后由其自身填写）
    h&:           # 实现私有区指针（同块分配，调用方不直接访问）
}

@fnc thread::join          # 等待结束并回收（含 thread 对象本身，之后指针失效）

@rpc msleep: ms: u4        # 当前线程休眠 ms 毫秒

# ---------------- mutex：互斥锁 ----------------

@def mutex: {
    h&:           # 平台锁句柄（实现私有）
}

@fnc mutex::init           # 构造（声明即构造适用）
@fnc mutex::drop           # 析构
@fnc mutex::lock           # 加锁（阻塞）
@fnc mutex::unlock         # 解锁
@fnc mutex::try_lock: bool # 取锁成功返回 1，已被占用返回 0

# ---------------- cond：条件变量（配合 wait 语句使用） ----------------

@def cond: {
    h&:           # 平台条件变量句柄（实现私有）
}

@fnc cond::init            # 构造（声明即构造适用）
@fnc cond::drop            # 析构
@fnc cond::one             # 唤醒一个等待者
@fnc cond::all             # 唤醒全部等待者
