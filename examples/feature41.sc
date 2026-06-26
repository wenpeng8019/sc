# 特性 41：sync<q> ... —— 非阻塞带回复的队列投递（rpc 作流原语的第五种驱动）
#
#   - 带队列 `async<q> work(args)`：把 rpc 调用【非阻塞】投递给队列 q，立即返回
#     promise&（mt-future 句柄）；某消费者（另一线程 pull / 线程池工作线程）执行完成
#     后兑现该 promise，调用方经 p->wait() 阻塞取回结果（或 p->ready() 非阻塞轮询）。
#   - 与 sync<q>（特性 40）对照：sync 立即阻塞等回复：sync 先拿 promise，可并发
#     发起多个、做别的事，再逐个 wait —— 这才是 async 的价值（投递与取值解耦）。
#   - 编译器经队列协议指针派发 `q->async(q, work_rpc, &参数, sizeof(参数))`——语言内核
#     零 emit mt 符号，求值为 promise&。
#   - promise 与 libuv future（单线程协作、绑事件循环）不同：它是线程世界的阻塞型
#     未来（内部 mutex+cond）。参数缓冲与返回槽由 promise 堆拥有，投递后无需保活；
#     须先 p->wait() 取结果再 p->drop()（消费者兑现前 drop 会 UAF，待后续引用计数化）。
#   - 目标在 `<>` 内（与 print<chn> 对齐）：`var p = async<q> work(a, b)`。超时 / 优先级 / 取消为后续阶段。
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
    # ① 线程池宿主：async 经 trampoline 转交 pool->run，池工作线程并发执行后兑现 promise
    var p: pool& = default_pool(2)
    var q: queue& = default_queue(p)

    # 并发发起两个：先各拿 promise（不阻塞），再逐个 wait（投递与取值解耦）
    var f1: promise& = async<q> compute(3, 4)       # 非阻塞投递，立即得 promise
    var f2: promise& = async<q> compute(100, 23)
    var r1: i4 = f1->wait(): i4                      # 阻塞取回 7（类型擦除，: i4 还原）
    var r2: i4 = f2->wait(): i4                      # 123
    printf("pool async: r1=%d r2=%d\n", r1, r2)
    f1->drop()
    f2->drop()

    q->drop()
    p->drop()

    # ② 另一线程 pull 宿主：专用消费线程 pull 执行，main 经 promise 取回
    var q2: queue& = default_queue(nil)
    var ct: thread& = nil
    run consume_n(q2, 2), &ct                        # 起消费线程：处理 2 条后退出

    var g1: promise& = async<q2> compute(10, 20)     # 非阻塞投递
    var g2: promise& = async<q2> compute(5, 6)
    var s1: i4 = g1->wait(): i4                       # 阻塞 → 消费线程算 30 → 取回
    var s2: i4 = g2->wait(): i4                       # 11
    printf("thread async: s1=%d s2=%d\n", s1, s2)
    g1->drop()
    g2->drop()

    ct->join()                                       # 消费线程已处理完 2 条，回收
    q2->drop()                                        # 此时无并发 pull，安全析构

    return 0
