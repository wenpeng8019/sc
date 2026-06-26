# 特性 43：sync<timeout:ms> —— 有限超时的阻塞同步调用（R2 铁律）
#
#   - 在 sync 选项块内加 `timeout:ms`（首项为队列目标），把「阻塞带回复」从无限等升级为有限超时：
#       sync<q, timeout:100> work(args)          # 至多等 100ms（但见下「铁律」）
#   - 铁律（执行一旦开始，超时只挂起、不放弃）：
#       · 超时在「消息被消费者 pull 之前」触发 → 干净摘除消息、零执行浪费，返回超时；
#       · 超时在「消息已被 pull、执行进行中」触发 → 不放弃，死等至执行完成，返回成功。
#     即 timeout 只约束「排队等待被消费」的时段；一旦某消费者开工，结果必被取回（不丢动作）。
#   - 可选状态出参（仿 run ..., &t）区分超时 vs 成功：
#       var st: i4
#       var r: i4 = sync<q, timeout:100> compute(3, 4), &st   # &st 接收状态码
#     状态码：0=成功 / 1=超时（仅 pull 前）/ -1=队列关闭被中断。表达式仍求值为结果（超时时为 0）。
#   - 实现要点（无堆 shadow）：调用方会话留在自己栈上、消息仅持其指针、rpc 参数不复制；
#     状态机 SS_QUEUED→PULLING→DONE 由全局锁保护，pull 与超时摘除串行 → 原子无竞态。
#     pull 前超时摘除消息，pull 后调用方死等——其栈与返回槽在执行全程有效，故无 UAF。
#     编译器仍零 emit mt 符号，经协议指针 q->sync 透传 timeout。
#   - 与无 timeout 的 sync（特性 40）完全兼容：不写 <timeout> 即原无限阻塞语义。
#   - 循环死锁替代（同线程 sync 到自身、或互锁环）由 sync 实现内部本地执行打破（特性 44）。
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
    var r1: i4 = sync<q, timeout:2000> compute(3, 4), &st1   # 至多等 2s；消费线程会及时算 7
    printf("ok path: r=%d st=%d\n", r1, st1)          # r=7 st=0
    ct->join()

    # ② 超时路径：无任何消费者，timeout 到期后放弃等待
    var st2: i4 = -9
    var r2: i4 = sync<q, timeout:50> compute(10, 20), &st2   # 50ms 无人消费 → 超时
    printf("timeout path: r=%d st=%d\n", r2, st2)     # r=0 st=1（结果未回填，状态=超时）

    q->drop()
    return 0
