# 特性 7：内置多线程支持（builtins/m）—— thread/mutex
#   - inc m.sc：thread/mutex 方法声明，实现在 C 侧（m_impl.c，
#     跨平台经由 builtins/platform.h：POSIX pthread / Windows 线程）
#   - 线程入口：fnc xxx -> thread_fn（命名函数类型，参数 arg&: v）
#   - 声明即构造：var t: thread / var mu: mutex 自动调用 init
inc stdio.h
inc m.sc

# 共享上下文：互斥锁保护计数器
def ctx: {
    mu: mutex
    n: i4
}

# 线程入口：按 thread_fn 类型实现
fnc worker -> thread_fn
    var c&: ctx = (arg: ctx&)
    var i: i4 = 0
    for i = 0; i < 10000; i++
        c->mu.lock()
        c->n = c->n + 1
        c->mu.unlock()

fnc main: i4
    var c: ctx
    c.n = 0
    c.mu.init()        # 嵌套字段不自动构造，手动 init

    # 声明即构造：thread::init 自动调用（h 置 nil）
    var t1: thread
    var t2: thread
    t1.start(worker, &c)
    t2.start(worker, &c)
    t1.join()
    t2.join()
    printf("threads done: n=%d\n", c.n)   # 期望 20000

    # try_lock：未占用时成功
    if c.mu.try_lock()
        printf("try_lock ok\n")
        c.mu.unlock()

    # sleep：当前线程休眠（与接收者实例无关）
    t1.sleep(10)
    printf("slept 10ms\n")

    c.mu.drop()
    return 0
