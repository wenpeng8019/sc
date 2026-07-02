# 特性 44：循环死锁替代 —— sync 形成互锁环时改本地执行（mt 最后一块基石）
#
#   - 消费者即「谁 pull 它」：que_pull 时惰性把队列绑给当前线程身份（q->consumer=&g_self）。
#   - 当线程 A 向「自己消费的队列」或「沿 consumer→waiting 链回到自己」的队列 sync 时，
#     直接投递+阻塞会死锁；运行时改为在当前线程直接执行该 rpc（替代），打破循环。
#   - 替代对无限等 / 有限超时两路同效；命中即同步成功返回（不投递、不阻塞）。
#   - 编译器仍零 emit mt 符号——替代完全发生在队列协议的 sync 实现内部（运行时）。
#   - 线程池宿主队列 consumer 恒空（池 worker 不 pull）→ 替代天然禁用，照常转交池。
#
inc mt.sc

rpc add: i4, a: i4, b: i4
    return a + b

# 在 A 上执行：向 B 消费的队列 qb 投递 innerB —— A 阻塞等回复（A.waiting=qb）
rpc kickA: i4, qb: queue&, qa: queue&
    var v: i4 = sync<qb> innerB(qa)
    return v

# 在 B 上执行：向 A 消费的队列 qa 投递 add —— 沿 qa→A→qb→B 成环 → 替代，本地算出 30
rpc innerB: i4, qa: queue&
    var v: i4 = sync<qa> add(10, 20)
    return v

# 在某消费线程上执行：向「自己正消费的队列」qs 投递 add —— 自替代，本地算出 15
rpc selfish: i4, qs: queue&
    var v: i4 = sync<qs> add(7, 8)
    return v

# 专用消费线程体：pull 一条消息执行后退出（pull(-1) 无限等来活）
rpc consume1: qq: queue&
    qq->pull(-1)

fnc main: i4
    # ① 循环替代 A↔B：A 消费 qa 并 sync 到 qb；B 消费 qb 并 sync 回 qa（互锁环）
    var qa: queue& = default_queue(nil)
    var qb: queue& = default_queue(nil)
    var ta: thread& = nil
    var tb: thread& = nil
    run consume1(qa), &ta            # 线程 A：pull qa（成为 qa 消费者），处理 kickA
    run consume1(qb), &tb            # 线程 B：pull qb（成为 qb 消费者），处理 innerB

    var rc: i4 = sync<qa> kickA(qb, qa)   # main 投 kickA 给 A → A sync qb → B sync 回 qa → 替代
    ::printf("circular substitution: rc=%d\n", rc)   # 期望 30

    ta->join()
    tb->join()
    qa->drop()
    qb->drop()

    # ② 自替代：消费线程处理 selfish 时，向自己正消费的队列 qs 再 sync（须本地执行）
    var qs: queue& = default_queue(nil)
    var ts: thread& = nil
    run consume1(qs), &ts            # 线程 S：pull qs（成为 qs 消费者），处理 selfish

    var rs: i4 = sync<qs> selfish(qs)     # S 处理 selfish 时 sync add 回 qs → 自替代
    ::printf("self substitution: rs=%d\n", rs)       # 期望 15

    ts->join()
    qs->drop()

    return 0
