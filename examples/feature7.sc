# 特性 7：内置多线程支持（语言特性 run + builtins/m）
#   - run 语句以 rpc 调用创建线程（rpc 参数天然可打包，正好作线程上下文）：
#       run work(&c, 10000), &t1   # joinable：t1&: thread，join 等待并回收
#       run note(7)                # detach：线程结束后自释放
#   - thread 对象由 run 内部联合分配（thread + rpc 参数单块），
#     成员仅 id（跨平台统一线程 id）；join 后整块回收，指针失效
#   - msleep(ms)：当前线程休眠（m 的 rpc 仅声明，C 侧实现）
#   - wait 语句：条件变量等待 wait cond, mutex[, nsec[, sec]]
#     nsec/sec 全 0 或省略 → 无限等待；调用前须已持有 mutex
#   - pool 线程池：run 语句第二参为 pool 时任务入池排队执行
#     run work(...), p —— 与独立线程同一个动词，按类型静态分派
inc stdio.h
inc m.sc

# 共享上下文：互斥锁保护计数器
def ctx: {
    mu: mutex
    n: i4
}

# 线程体：rpc 即线程入口，参数即线程上下文
rpc work: c&: ctx, rounds: i4
    var i: i4 = 0
    for i = 0; i < rounds; i++
        c->mu.lock()
        c->n = c->n + 1
        c->mu.unlock()

# detach 线程体：自释放，无需 join
rpc note: tag: i4
    printf("detached note: tag=%d\n", tag)

# 条件变量信号：加锁置位后唤醒等待者
def sig: {
    mu: mutex
    cv: cond
    ready: i4
}

rpc ping: s&: sig
    s->mu.lock()
    s->ready = 1
    s->cv.one()
    s->mu.unlock()

fnc main: i4
    var c: ctx
    c.n = 0
    c.mu.init()        # 嵌套字段不自动构造，手动 init

    # joinable：第二参数接收 thread 指针
    var t1&: thread = nil
    var t2&: thread = nil
    run work(&c, 10000), &t1
    run work(&c, 10000), &t2
    printf("t1 id set: %d\n", t1 != nil)   # run 返回即拿到 thread 对象
    t1->join()         # 等待并回收（含 thread 对象本身）
    t2->join()
    printf("threads done: n=%d\n", c.n)    # 期望 20000

    # detach：无出参，线程结束后自释放
    run note(7)
    msleep(50)         # 等 detach 线程打印完

    # try_lock：未占用时成功
    if c.mu.try_lock()
        printf("try_lock ok\n")
        c.mu.unlock()

    c.mu.drop()

    # wait 语句：条件变量等待（虚假唤醒需循环复查条件）
    var s: sig
    s.ready = 0
    s.mu.init()
    s.cv.init()
    run ping(&s)
    s.mu.lock()
    while s.ready == 0
        wait s.cv, s.mu          # 无限等待，被 one() 唤醒
    s.mu.unlock()
    printf("cond wait ok: ready=%d\n", s.ready)

    # 超时等待：无人唤醒，约 5ms（5000000 纳秒）后超时返回
    s.mu.lock()
    wait s.cv, s.mu, 5000000, 0
    s.mu.unlock()
    printf("cond timeout ok\n")

    s.cv.drop()
    s.mu.drop()

    # pool：run 第二参为 pool → 任务入池（4 个 worker 跑 8 个任务）
    var c2: ctx
    c2.n = 0
    c2.mu.init()
    var p: pool
    p.init(4)                  # 0 → CPU 核数
    var k: i4 = 0
    for k = 0; k < 8; k++
        run work(&c2, 1000), p # 入池：与 run 独立线程同一语句
    p.join()                   # 屏障：等全部任务完成（pool 仍可用）
    printf("pool done: n=%d\n", c2.n)      # 期望 8000
    run work(&c2, 1000), p     # join 后继续提交
    p.drop()                   # 析构：等任务完成后停池回收
    printf("pool drop: n=%d\n", c2.n)      # 期望 9000
    c2.mu.drop()
    return 0
