# m —— sc 多线程语言支持标准（thread/mutex）
#
# 本文件是 m 的唯一事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分）
#   @fnc T::m 方法声明（无函数体）：extern 原型，实现在 C 侧
#
# 默认实现：同目录 m_impl.c（编译器自动编译并链接，
#           跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程）
#
# 句柄约定：h&: v 为实现私有的平台句柄（实现内部分配/释放），
#           调用方不直接访问；结构布局因此跨平台稳定。
#
# 定位：多线程将逐步成为 sc 语言特性的一部分，本模块是其支持标准；
#       后续按语言特性需要扩展（条件变量/原子操作/线程局部存储等）。

# ---------------- thread：线程 ----------------

@fnc thread_fn: v, arg&: v        # 线程入口函数类型

@def thread: {
    h&: v         # 平台线程句柄（实现私有）
}

@fnc thread::init: v                          # 构造为空（未启动）
@fnc thread::start: b, f&: thread_fn, arg&: v # 启动线程（已启动未回收返回 0）
@fnc thread::join: v                          # 等待结束并回收
@fnc thread::drop: v                          # 析构：未 join 的线程 detach 后释放

@fnc thread::sleep: v, ms: u4                 # 当前线程休眠（与接收者实例无关）

# ---------------- mutex：互斥锁 ----------------

@def mutex: {
    h&: v         # 平台锁句柄（实现私有）
}

@fnc mutex::init: v                           # 构造（声明即构造适用）
@fnc mutex::drop: v                           # 析构
@fnc mutex::lock: v
@fnc mutex::unlock: v
@fnc mutex::try_lock: b                       # 取锁成功返回 1，已被占用返回 0
