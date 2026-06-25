# 特性 42：sync/async 的 <prio:N, delay:ms> 选项 —— 队列消费的优先级与延迟
#
#   - 在 sync/async 驱动 rpc 入队时，可在关键字后加尖括号选项块（仿 run<...>）：
#       async<prio:5> work(args), q        # 高优先级：先于低优先级被消费
#       async<delay:50> work(args), q      # 延迟 50ms：到期后才可被 pull
#       sync<prio:3, delay:10> work(), q   # 两者可同时给（逗号分隔，值限非负整数）
#   - 语义（仅作用于「FIFO-pull 消费路径」，即 nil/main 宿主队列被 pull 时）：
#       · prio  —— 就绪链按优先级排序，pull 总取最高优先级；同优先级稳定 FIFO。
#       · delay —— 入「延迟链」按到期时刻升序；pull 阻塞至最早到期项成熟再消费。
#   - 池宿主队列（default_queue(&pool)）忽略 prio/delay：投递即转交池并发自调度。
#   - << 投递不带选项（语法上与移位/比较冲突）；要「延迟/带优先级的即发即忘」，
#     用 async<...> 驱动并丢弃返回的 promise 即可。
#   - 编译器仍零 emit mt 符号：选项经协议指针透传 q->post/sync/async 的 prio/delay 形参。
#   - 有限超时 sync 与循环死锁替代为后续架构升级阶段（需 per-thread 端口 + 影子会话）。
#
#   本例用 nil 宿主队列 + 主线程顺序 pull：消费在单线程内逐条进行，故 printf 的输出
#   顺序即「消费顺序」，可确定性观测优先级/延迟的排序效果。
#
inc mt.sc

# rpc 即消息处理体；首类型 i4 = 返回类型。执行时打印自身 tag —— 输出顺序即消费顺序。
rpc serve: i4, tag: i4, info: i4
    printf("  served tag=%d (%d)\n", tag, info)
    return tag

fnc main: i4
    # ===== ① 优先级：高者先被消费 =====
    # nil 宿主：先把 3 条非阻塞入队（此刻无消费者），就绪链即按优先级排好：5 > 3 > 1
    var q: queue& = default_queue(nil)
    var a1: promise& = async<prio:1> serve(1, 1), q     # 最低优先级
    var a2: promise& = async<prio:5> serve(2, 5), q     # 最高优先级
    var a3: promise& = async<prio:3> serve(3, 3), q
    printf("priority order (expect tag 2,3,1):\n")
    q->pull(-1)                                          # 取 prio5 → tag2
    q->pull(-1)                                          # 取 prio3 → tag3
    q->pull(-1)                                          # 取 prio1 → tag1
    a1->wait()                                           # 已兑现，取值并回收
    a2->wait()
    a3->wait()
    a1->drop()
    a2->drop()
    a3->drop()
    q->drop()

    # ===== ② 延迟：按到期先后出队（与入队顺序无关）=====
    var q2: queue& = default_queue(nil)
    var d1: promise& = async<delay:60> serve(100, 60), q2   # 60ms 后成熟
    var d2: promise& = async<delay:20> serve(200, 20), q2   # 20ms（最早）
    var d3: promise& = async<delay:40> serve(300, 40), q2   # 40ms
    printf("delay order (expect tag 200,300,100):\n")
    q2->pull(-1)                                        # 阻塞至 20ms 成熟 → tag200
    q2->pull(-1)                                        # 阻塞至 40ms 成熟 → tag300
    q2->pull(-1)                                        # 阻塞至 60ms 成熟 → tag100
    d1->wait()
    d2->wait()
    d3->wait()
    d1->drop()
    d2->drop()
    d3->drop()
    q2->drop()

    return 0
