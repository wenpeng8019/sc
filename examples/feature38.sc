# 特性 38：消息队列 queue（op 层接口协议）
#
#   - queue 与 com/pool 同属 op 层「接口协议」：vtable 全为每对象方法指针（默认导入），
#     由 mt 模块按策略提供具名构造 default_queue(host) 填充并返回 queue&（仿 com 的 file()）。
#   - << 投递：q << work(参数) 把一个 rpc 调用整体打包成消息入队（不触发本地调用），
#     编译器经协议指针派发 q->post(q, work_rpc, &参数, sizeof)——语言内核零 emit mt 符号。
#   - pull 消费：q->pull(timeout) 取一条消息在当前线程执行；
#     timeout <0 无限等 / 0 立即返回 / >0 毫秒超时；返回 1 处理一条 / 0 超时空 / -1 已关闭。
#   - 宿主三态（host: pool&，构造时绑定）：nil 未绑/延迟、main 当前/主线程（自跑 pull
#     循环消费）、&pool 线程池消费（自动消费接通见后续）。main 是 op 提供的 pool& 哨兵常量。
#   - queue& 是协议指针，方法用箭头：q->pull(...) / q->drop()。
#
inc mt.sc

# 共享累加器：消息处理结果落到这里
def acc: {
    sum: i4
    cnt: i4
}

# rpc 即消息处理体，参数即消息载荷
rpc add: a: acc&, v: i4
    a->sum = a->sum + v
    a->cnt = a->cnt + 1

# 带字符串载荷的消息（字面量为静态存储，指针打包入消息后仍有效）
rpc tag: a: acc&, label: char&, v: i4
    printf("  [%s] += %d\n", label, v)
    a->sum = a->sum + v
    a->cnt = a->cnt + 1

fnc main: i4
    var a: acc
    a.sum = 0
    a.cnt = 0

    # 宿主 = main：当前线程自跑 pull 循环消费
    var q: queue& = default_queue(main)

    # << 投递：rpc 调用整体打包成消息入队（不触发本地调用）
    q << add(&a, 10)
    q << add(&a, 20) << add(&a, 30)        # 链式投递多条
    q << tag(&a, "boost", 40)

    # 排空：当前线程取一条执行，队空（pull 返 0）即退出
    var n: i4 = 0
    while q->pull(0) > 0
        n = n + 1
    printf("queue drained: msgs=%d sum=%d cnt=%d\n", n, a.sum, a.cnt)   # 4 100 4

    # drop：解绑宿主 → 排空残留 → 回收（含 queue 对象本身）
    q->drop()

    # 宿主 = nil：延迟/未绑缓冲，同样手动 pull 消费
    var q2: queue& = default_queue(nil)
    q2 << add(&a, 5)
    q2 << add(&a, 7)
    var m: i4 = 0
    while q2->pull(0) > 0
        m = m + 1
    printf("deferred queue: msgs=%d sum=%d cnt=%d\n", m, a.sum, a.cnt)  # 2 112 6
    q2->drop()

    return 0
