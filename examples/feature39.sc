# 特性 39：pool 宿主消息队列（<< 投递自动转交线程池并发消费）
#
#   - queue 宿主三态之 &pool：构造 default_queue(p) 绑定一个线程池为宿主后，
#     << 投递不再入本队列 FIFO，而是经协议指针 q->post 内部直接转交 p->run，
#     由池的工作线程并发消费——生产者只管 q << work，无需手动 pull。
#   - 屏障用池的 p->join()（等全部已投递消息处理完），而非 pull。
#   - 生命周期：队列与池各自独立，分别 q->drop() / p->drop()；
#     须先 p->join() 确保消息处理完，再 drop。
#   - 并发安全由消息处理体自理：本例 acc 用 mutex 保护，故多线程消费下
#     sum/cnt 结果仍确定（加法可交换）。
#
inc mt.sc

# 共享累加器：互斥锁保护，支持多线程并发消费
def acc: {
    mu:  mutex
    sum: i4
    cnt: i4
}

# rpc 即消息处理体，参数即消息载荷
rpc add: a: acc&, v: i4
    a->mu.lock()
    a->sum = a->sum + v
    a->cnt = a->cnt + 1
    a->mu.unlock()

fnc main: i4
    var a: acc
    a.sum = 0
    a.cnt = 0
    a.mu.init()        # 嵌套字段不自动构造，手动 init

    # 线程池 + 以池为宿主的队列：<< 投递自动转交池并发消费
    var p: pool& = default_pool(4)     # 4 个工作线程
    var q: queue& = default_queue(p)   # 宿主 = 真实池 ⇒ post 转交 p->run

    # 投递 100 条消息：各自整体打包 → q->post → 转交 p->run，池并发执行
    var i: i4 = 0
    for i = 1; i <= 100; i++
        q << add(&a, i)

    p->join()          # 屏障：等池把所有已投递消息处理完
    ::printf("pool queue: sum=%d cnt=%d\n", a.sum, a.cnt)   # 5050 100

    q->drop()          # 队列解绑回收（宿主池另行 drop）
    p->drop()          # 停池回收

    return 0
