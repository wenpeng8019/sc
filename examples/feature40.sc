# 特性 40：sync<q> ... —— 阻塞带回复的队列投递（rpc 作流原语的第四种驱动）
#
#   - 无目标 `sync work(args)`：当前线程直接执行该 rpc，返回结果（替代裸 rpc()，见特性 3）。
#   - 带队列 `sync<q> work(args)`：把 rpc 调用阻塞投递给队列 q，阻塞至某消费者
#     （另一线程 pull / 线程池工作线程）执行完成，取回 rpc 的返回值。
#   - 编译器经队列协议指针派发 `q->sync(q, work_rpc, &参数)`——语言内核零 emit mt 符号。
#     结果回填参数结构体首字段（返回槽 _），语句表达式求值为该返回值。
#   - 目标在 `<>` 内（与 print<chn> 对齐）：`var r = sync<q> work(a, b)`。
#   - 消费者须是别的线程/池（同线程 sync 到自己消费的队列会死锁，由调用者负责）。
#     超时 / 优先级 / 循环死锁替代为后续阶段。
#
inc mt.sc

# rpc 即消息处理体；首类型 i4 = 返回类型，return 值即回复
rpc compute: i4, a: i4, b: i4
    return a + b

# 专用消费线程体：从队列 q 取 n 条消息执行后退出（pull(-1) 无限等来活）
rpc consume_n: qq: queue&, n: i4
    var i: i4 = 0
    for i = 0; i < n; i++
        qq->pull(-1)

fnc main: i4
    # ① 线程池宿主：sync 经 trampoline 转交 pool->run，池工作线程执行并回复
    var p: pool& = default_pool(2)
    var q: queue& = default_queue(p)

    var r1: i4 = sync<q> compute(3, 4)       # 阻塞 → 池工作线程算 7 → 取回
    var r2: i4 = sync<q> compute(100, 23)    # 123
    ::printf("pool sync: r1=%d r2=%d\n", r1, r2)

    q->drop()
    p->drop()

    # ② 另一线程 pull 宿主：专用消费线程 pull 执行，main 阻塞等回复
    var q2: queue& = default_queue(nil)
    var ct: thread& = nil
    run consume_n(q2, 2), &ct                # 起消费线程：处理 2 条后退出

    var s1: i4 = sync<q2> compute(10, 20)    # 阻塞 → 消费线程算 30 → 取回
    var s2: i4 = sync<q2> compute(5, 6)      # 11
    ::printf("thread sync: s1=%d s2=%d\n", s1, s2)

    ct->join()                               # 消费线程已处理完 2 条，回收
    q2->drop()                               # 此时无并发 pull，安全析构

    return 0
