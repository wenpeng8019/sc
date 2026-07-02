# LRU 缓存 lru 回归用例：覆盖
#   1. 组合容器（内嵌 dict + 侵入双向链表）：put/get/has/len/is_empty；
#   2. 访问语义——get 触顶（移至 MRU）、peek 不改最近度、has 不触顶；
#   3. 容量淘汰——cap>0 时 put 新键超容淘汰队尾(LRU)；set_cap 缩容立即淘汰；
#   4. mru_key/lru_key 观测最热/最冷键；each 按 MRU→LRU 顺序遍历；
#   5. key 三态——拷贝字符串(-1) 与定长数值(>0)；replace 替换 release 旧值；
#   6. value 拥有（容器无需知 T），触零经句柄自带 dtor 自析构。
# 用 --check=ref 运行可验证每 value retain/release 守恒（无悬挂、无泄漏）。
inc adt.sc

@def node: {
    v: i4
    drop: fnc
        ::printf("drop %d\n", this->v)
}

@fnc make: node@, x: i4
    var p: node@ = node()
    p->v = x
    return p

# 按 MRU→LRU 打印键（字符串）与值
@fnc dump_str: bool, key: const &, value: *, ctx: &
    ::printf(" %s=%d", (key: char&), (value: node&)->v)
    return true

# 按 MRU→LRU 打印键（i4）与值
@fnc dump_int: bool, key: const &, value: *, ctx: &
    var kp: i4& = (key: i4&)
    ::printf(" %d=%d", kp[0], (value: node&)->v)
    return true

@fnc main: i4
    # ---------- A：字符串键（拷贝 -1），容量 3，淘汰 + 触顶 ----------
    var ca: lru
    ca.init(0 - 1, 3)                                # key_size=-1 拷贝字符串，cap=3
    var ha[5]: node@                                 # holder：持 value root 至退域
    ha[0] = make(1)
    ca.put("a", (ha[0]: *))                          # MRU: a
    ha[1] = make(2)
    ca.put("b", (ha[1]: *))                          # MRU: b a
    ha[2] = make(3)
    ca.put("c", (ha[2]: *))                          # MRU: c b a
    ::printf("A len=%llu cap=%llu mru=%s lru=%s\n",
           ca.len(), ca.cap(), (ca.mru_key(): char&), (ca.lru_key(): char&))

    # peek 不触顶：lru 仍为 a
    ::printf("A peek_a=%d lru_still=%s\n", (ca.peek("a"): node&)->v, (ca.lru_key(): char&))
    # get 触顶：a 移到 MRU，lru 变 b
    ::printf("A get_a=%d mru=%s lru=%s\n",
           (ca.get("a"): node&)->v, (ca.mru_key(): char&), (ca.lru_key(): char&))

    # put 新键 d 超容（>3）→ 淘汰队尾 b
    ha[3] = make(4)
    ca.put("d", (ha[3]: *))                          # MRU: d a c（淘汰 b）
    ::printf("A after_put_d: has_b=%d has_d=%d len=%llu\n",
           ca.has("b"), ca.has("d"), ca.len())
    ::printf("A each:")
    ca.each(dump_str, nil)                           # d=4 a=1 c=3
    ::printf("\n")

    # replace：put 已存在键 a 改挂新 value 99（容器 release 旧 1、retain 新；len 不变 + 触顶）
    ha[4] = make(99)
    ca.put("a", (ha[4]: *))                          # MRU: a d c
    ::printf("A replace_a=%d len=%llu\n", (ca.get("a"): node&)->v, ca.len())
    ::printf("A each2:")
    ca.each(dump_str, nil)                           # a=99 d=4 c=3
    ::printf("\n")
    ca.drop()

    # ---------- B：定长 i4 键，无界→set_cap 缩容淘汰 + remove ----------
    var cb: lru
    cb.init(4, 0)                                    # key_size=4 定长数值，cap=0 无界
    var keys[4]: i4
    keys[0] = 10
    keys[1] = 20
    keys[2] = 30
    keys[3] = 40
    var hb[4]: node@
    var i: i4 = 0
    while i < 4
        hb[i] = make((keys[i] + 1) * 10)             # 110 210 310 410
        cb.put((&keys[i]: const &), (hb[i]: *))
        i += 1
    # MRU→LRU: 40 30 20 10
    var mk: i4& = (cb.mru_key(): i4&)
    var lk: i4& = (cb.lru_key(): i4&)
    ::printf("B len=%llu cap=%llu mru=%d lru=%d empty=%d\n",
           cb.len(), cb.cap(), mk[0], lk[0], cb.is_empty())

    # remove 20 → MRU→LRU: 40 30 10
    ::printf("B remove20=%d miss=%d has20=%d len=%llu\n",
           cb.remove((&keys[1]: const &)), cb.remove((&keys[1]: const &)),
           cb.has((&keys[1]: const &)), cb.len())

    # set_cap(2) 缩容 → 淘汰队尾 10，剩 40 30
    cb.set_cap(2)
    var lk2: i4& = (cb.lru_key(): i4&)
    ::printf("B after_set_cap2: cap=%llu len=%llu has10=%d lru=%d\n",
           cb.cap(), cb.len(), cb.has((&keys[0]: const &)), lk2[0])
    ::printf("B each:")
    cb.each(dump_int, nil)                           # 40=410 30=310
    ::printf("\n")
    cb.drop()
    # main 退域：holder 逆序释放 value root → 集中析构

    return 0
