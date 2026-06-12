# 特性 7：内置多线程支持（语言特性 run + builtins/m）
#   - run 语句以 rpc 调用创建线程（rpc 参数天然可打包，正好作线程上下文）：
#       run work(&c, 10000), &t1   # joinable：t1&: thread，join 等待并回收
#       run note(7)                # detach：线程结束后自释放
#   - thread 对象由 run 内部联合分配（thread + rpc 参数单块），
#     成员仅 id（跨平台统一线程 id）；join 后整块回收，指针失效
#   - msleep(ms)：当前线程休眠（m 的 rpc 仅声明，C 侧实现）
inc stdio.h
inc m.sc

# 共享上下文：互斥锁保护计数器
def ctx: {
    mu: mutex
    n: i4
}

# 线程体：rpc 即线程入口，参数即线程上下文
rpc work: v, c&: ctx, rounds: i4
    var i: i4 = 0
    for i = 0; i < rounds; i++
        c->mu.lock()
        c->n = c->n + 1
        c->mu.unlock()

# detach 线程体：自释放，无需 join
rpc note: v, tag: i4
    printf("detached note: tag=%d\n", tag)

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
    return 0
