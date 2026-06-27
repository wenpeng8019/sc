# tok MT 压力测试（非黄金回归：时序不确定）。
# 目标：多线程并发 set 一个 '/' 前缀 MT token，验证细粒度无锁（seqlock 读 + 每-dep 自旋门 +
#   follow 锁外）的正确性与数据竞争（配合 TSan：SCC_CFLAGS/LDFLAGS=-fsanitize=thread）。
#
# 拓扑：
#   /mt.counter  combine 取较大者（峰值），N 线程各狂 set 递增值；
#   /mt.peak     enforce 从，follow 把 counter 当前值搬过去（跨 token 副作用，锁外运行）；
#   dep any: counter 变更即 follow → peak.set(counter.get())。
# 期望：无 TSan 报告；结束后 counter==peak==所有线程写入的最大值。

inc mt.sc

# ---- form 候选：MT 计数器，combine 取较大者（峰值/去抖）----
tok counter: "/mt.counter"
    var b: i8 = (this->base: i8)
    var i: i8 = (this->input: i8)
    var m: i8 = b
    if i > b
        m = i
    return (m: @)

# ---- enforce 从：峰值镜像 ----
tok peak: "/mt.peak"

# ---- 依赖：counter 任一变更 → 把当前值搬到 peak（follow 锁外运行，跨 token 副作用）----
dep any: c:"/mt.counter"
    var v: i8 = (c->get(): i8)
    peak->set((v: @), 0)
    return false

# 线程体：直接以标量参数传基数/轮数（对齐 feature9 的 rpc 形参约定），
# 狂 set counter，值 = base + i（递增），制造并发写 + 级联 follow。
rpc hammer: base: i4, rounds: i4
    var i: i4 = 0
    for i = 0; i < rounds; i++
        var v: i8 = (base: i8) + (i: i8)
        counter->set((v: @), 0)
        var seen: i8 = (counter->get(): i8)     # 读路径并发参与（seqlock 无锁读）
        if seen < v
            printf("BUG: counter regressed seen=%lld < v=%lld\n", seen, v)

fnc main: i4
    form counter, (0: @)            # 初始化 form 主

    var T: i4 = 8                   # 线程数
    var R: i4 = 20000              # 每线程轮数
    var ths[8]: thread&             # 线程句柄数组（thread& 指针，run 内部分配并回填）

    var k: i4 = 0
    for k = 0; k < T; k++
        run hammer(k * 100000, R), &ths[k]   # 各线程不同基数 → 最大者来自最高 base 末轮

    for k = 0; k < T; k++
        ths[k]->join()

    # 期望最大值 = (T-1)*100000 + (R-1)
    var expect: i8 = ((T - 1): i8) * (100000: i8) + ((R - 1): i8)
    var cv: i8 = (counter->get(): i8)
    var pv: i8 = (peak->get(): i8)
    printf("counter=%lld peak=%lld expect=%lld\n", cv, pv, expect)
    if cv == expect && pv == expect
        printf("OK: MT lockless consistent\n")
    else
        printf("FAIL: mismatch\n")

    return 0
