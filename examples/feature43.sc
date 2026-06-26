# 特性 43：sync<timeout:ms> —— 有限超时的阻塞同步调用（P5c）
#
#   - 在 sync 关键字后加 `<timeout:ms>` 选项，把「阻塞带回复」从无限等升级为有限超时：
#       sync<timeout:100> work(args), q          # 至多等 100ms
#     若 100ms 内某消费者执行完成 → 正常取回结果；否则放弃等待并返回（超时）。
#   - 可选状态出参（仿 run ..., &t）区分超时 vs 成功：
#       var st: i4
#       var r: i4 = sync<timeout:100> compute(3, 4), q, &st   # &st 接收状态码
#     状态码：0=成功 / 1=超时 / -1=队列关闭。表达式仍求值为结果（超时时为 0）。
#   - 实现要点：无限等路径在 caller 栈上放描述符（零分配）；有限超时路径改用堆盒子 +
#     引用计数——执行方只在盒子堆缓冲上跑 rpc，绝不碰 caller 栈，故 caller 超时 abort
#     不会 UAF。编译器仍零 emit mt 符号，经协议指针 q->sync 透传 timeout。
#   - 与无 timeout 的 sync（特性 40）完全兼容：不写 <timeout> 即原无限阻塞语义。
#   - 循环死锁替代（同线程 sync 到自身）仍为后续架构升级阶段。
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
    # ① 正常路径：有消费线程及时执行，timeout 内成功取回
    var q: queue& = default_queue(nil)
    var ct: thread& = nil
    run consume_n(q, 1), &ct                          # 起消费线程：处理 1 条后退出

    var st1: i4 = -9
    var r1: i4 = sync<timeout:2000> compute(3, 4), q, &st1   # 至多等 2s；消费线程会及时算 7
    printf("ok path: r=%d st=%d\n", r1, st1)          # r=7 st=0
    ct->join()

    # ② 超时路径：无任何消费者，timeout 到期后放弃等待
    var st2: i4 = -9
    var r2: i4 = sync<timeout:50> compute(10, 20), q, &st2   # 50ms 无人消费 → 超时
    printf("timeout path: r=%d st=%d\n", r2, st2)     # r=0 st=1（结果未回填，状态=超时）

    q->drop()
    return 0
