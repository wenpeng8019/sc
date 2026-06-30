# 数组背二叉堆 / 优先队列 heap 回归用例：覆盖
#   1. min 双向——最小堆(1) 弹出升序、最大堆(0) 弹出降序，同一套接口；
#   2. key 三态——数值定长(>0) 按宽度有符号、拷贝字符串(-1) strcmp；
#   3. push 取一份 retain、pop/clear/drop 释放；value 拥有（容器无需知 T）；
#   4. peek 借用堆顶 value（不改计数）、peek_key 借用堆顶 key；len/is_empty；
#   5. 乱序 push 后反复 pop，堆顶始终为当前极值（堆序正确性）；
#   6. reserve 预留 + 触零经句柄自带 dtor 自析构。
# 用 --check=ref 运行可验证每 value retain/release 守恒（无悬挂、无泄漏）。
inc adt.sc

@def node: {
    v: i4
    drop: fnc
        printf("drop %d\n", this->v)
}

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

# 反复 pop，按弹出序打印 key（即优先序）；返回弹出个数
@fnc drain_keys: i4, h: heap&
    var n: i4 = 0
    while !h->is_empty()
        var kp: i4& = (h->peek_key(): i4&)
        printf(" %d", kp[0])
        h->pop()
        n += 1
    printf("\n")
    return n

@fnc main: i4
    # ---------- A：最小堆（min=1），数值 key（i4）----------
    var ta: heap
    ta.init(1, 4, nil, nil)
    ta.reserve(16)                                   # 预留不改语义
    var ha[7]: node@                                 # holder：持有 value root 引用至退域
    var keys[7]: i4
    keys[0] = 50
    keys[1] = 30
    keys[2] = 70
    keys[3] = 20
    keys[4] = 40
    keys[5] = 60
    keys[6] = 80
    var i: i4 = 0
    while i < 7
        ha[i] = make(keys[i] * 10)
        ta.push((&keys[i]: const &), (ha[i]: *))
        i += 1
    printf("A len=%llu empty=%d\n", ta.len(), ta.is_empty())

    # peek 堆顶：最小键 20，value 200（借用不改计数）
    var topk: i4& = (ta.peek_key(): i4&)
    printf("A top_key=%d top_val=%d\n", topk[0], (ta.peek(): node&)->v)

    # 反复 pop → 升序 20 30 40 50 60 70 80
    printf("A drain:")
    var cnt: i4 = drain_keys(&ta)
    printf("A drained=%d empty=%d\n", cnt, ta.is_empty())

    # ---------- B：最大堆（min=0），同样的 key ----------
    var tb: heap
    tb.init(0, 4, nil, nil)
    var hb[7]: node@
    i = 0
    while i < 7
        hb[i] = make(keys[i] * 10)
        tb.push((&keys[i]: const &), (hb[i]: *))
        i += 1
    # 堆顶为最大键 80
    var tbk: i4& = (tb.peek_key(): i4&)
    printf("B top_key=%d len=%llu\n", tbk[0], tb.len())
    # 反复 pop → 降序 80 70 60 50 40 30 20
    printf("B drain:")
    drain_keys(&tb)
    tb.drop()

    # ---------- C：拷贝字符串 key（key_size=-1，strcmp）最小堆 ----------
    var tc: heap
    tc.init(1, 0 - 1, nil, nil)
    var hc[4]: node@
    hc[0] = make(4)
    tc.push("delta", (hc[0]: *))
    hc[1] = make(1)
    tc.push("alpha", (hc[1]: *))
    hc[2] = make(3)
    tc.push("charlie", (hc[2]: *))
    hc[3] = make(2)
    tc.push("bravo", (hc[3]: *))
    printf("C len=%llu top_key=%s\n", tc.len(), (tc.peek_key(): char&))  # alpha
    # 弹出两个最小：alpha bravo
    printf("C pop:")
    var topc: char& = (tc.peek_key(): char&)
    printf(" %s", topc)
    tc.pop()
    topc = (tc.peek_key(): char&)
    printf(" %s\n", topc)
    tc.pop()
    printf("C after_pop len=%llu\n", tc.len())
    tc.clear()                                       # 清空并 release 剩余 charlie/delta
    printf("C after_clear len=%llu empty=%d\n", tc.len(), tc.is_empty())
    tc.drop()
    # main 退域：holder 数组逆序释放 value root → 集中析构

    return 0
