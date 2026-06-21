# 特性 29：mem 内存池模块（chunk / refit / recycle）+ arena 竞技场
#
# 一、定位：替代 malloc / realloc / free 的池化分配器
#     chunk(size)          申请一块至少 size 字节的内存（malloc）
#     chunk0(size)         同上但清零（calloc）
#     chunk_array(n, size) 防溢出的数组分配（calloc(n,size)，n*size 溢出返回 nil）
#     chunk_aligned(sz, a) 超对齐分配（a 须 2 的幂，用于 SIMD/缓存行/页对齐）
#     refit(p, size)       原地或迁移扩缩到 size，保留旧内容（realloc）
#     recycle(p)           归还内存到池（free），可由任意线程调用（跨线程释放安全）
#     mem_usable(p)        返回该块实际可用字节数（≥ 申请值，因按尺寸档对齐）
#     mem_trim()           主动归还当前线程空闲堆页回 OS（仅本线程无存活分配时）
#     mem_teardown()       进程退出前归还所有池化页（仅在所有线程静止时调用）
#     mem_stat(&s)         填充统计快照（reserved/live/peak_live/count/allocs/frees）
#
# 二、设计：分尺寸档空闲链 + 每线程 TLS 堆（小对象无锁），>64KiB 直走系统分配；
#     跨线程释放经无锁 MPSC 队列回收到属主线程。线程退出时其堆被标记废弃、由新线程
#     抢占复用（避免死线程堆永驻）。页一旦取得默认不还 OS，除非 mem_trim/mem_teardown。
#
# 三、arena 竞技场：批量同生命周期分配，整体 reset / drop，无需逐块 recycle。
#     a.init(cap)   a.chunk(size)   a.reset()   a.drop()
#
# 四、shm 跨进程命名共享内存：make(name,size,flags) 创建/附着、data/size 访问、
#     drop 解除、shm_remove 删除命名。flags：0 默认读写，1 只读(SHM_RDONLY)，
#     2 独占创建(SHM_EXCL)。（POSIX shm_open+mmap / Windows CreateFileMapping）

inc stdio.h
inc mem.sc

fnc main: i4
    # ---- 池化分配：写入、查询可用尺寸 ----
    var p: & = chunk(100)
    var s: char& = p: char&
    sprintf(s, "pooled-%d", 42)
    printf("chunk(100): usable>=100 -> %d, text=%s\n", mem_usable(p) >= 100, s)

    # ---- refit 扩容并保留旧内容 ----
    p = refit(p, 5000)
    printf("refit(5000): usable>=5000 -> %d, keep=%s\n", mem_usable(p) >= 5000, p: char&)
    recycle(p)

    # ---- chunk0 清零 ----
    var z: & = chunk0(64)
    var zb: u1& = z: u1&
    printf("chunk0(64): first=%d last=%d\n", zb[0], zb[63])
    recycle(z)

    # ---- chunk_array 防溢出数组分配（calloc(n,size)）----
    var arr: & = chunk_array(10, 8)
    var ab: u1& = arr: u1&
    printf("chunk_array(10,8): usable>=80 -> %d zeroed=%d\n", mem_usable(arr) >= 80, ab[0])
    recycle(arr)
    printf("chunk_array overflow -> nil=%d\n", chunk_array(0xFFFFFFFFFFFFFFFFUL, 2) == nil)

    # ---- chunk_aligned 超对齐分配（SIMD/缓存行/页）----
    var al: & = chunk_aligned(200, 64)
    var alb: u1& = al: u1&
    alb[0] = 7
    alb[199] = 9
    printf("chunk_aligned(200,64): usable>=200 -> %d rw=%d\n",
           mem_usable(al) >= 200, alb[0] + alb[199])
    recycle(al)
    printf("chunk_aligned bad-align -> nil=%d\n", chunk_aligned(64, 48) == nil)

    # ---- arena 批量分配 + reset 复用 ----
    var a: arena
    a.init(0)
    var i: i4 = 0
    var n: i4 = 0
    for i = 0; i < 1000; i++
        var q: & = a.chunk(48)
        if q != nil
            n++
    printf("arena: 1000x48 allocated=%d\n", n)
    a.reset()
    var r: & = a.chunk(16)
    printf("arena: reset+reuse ok=%d\n", r != nil)
    a.drop()

    # ---- 内存统计快照 ----
    var p2: & = chunk(1000)
    var p3: & = chunk(2000)
    var st: mem_stat_t
    mem_stat(&st)
    printf("stat live: count=%llu live>=3000 -> %d peak>=live -> %d allocs>=2 -> %d\n",
           st.count, st.live >= 3000, st.peak_live >= st.live, st.allocs >= 2)
    recycle(p2)
    recycle(p3)
    mem_stat(&st)
    printf("stat freed: count=%llu live=%llu peak>=3000 -> %d\n",
           st.count, st.live, st.peak_live >= 3000)

    # ---- mem_trim 主动归还空闲页回 OS ----
    var k: i4 = 0
    for k = 0; k < 200; k++
        var t: & = chunk(64)
        recycle(t)
    var freed: u8 = mem_trim()
    printf("mem_trim: freed>0 -> %d\n", freed > 0)

    # ---- shm 命名共享内存（同进程内附着演示） ----
    var nm: char& = "sc_feature29_region"
    shm_remove(nm)
    var sm: shm
    if sm.make(nm, 256, 0)
        var sd: char& = sm.data(): char&
        sprintf(sd, "shm-%d", 29)
        var sm2: shm                              # 同名再附着，看到同一内容
        sm2.make(nm, 256, 0)
        printf("shm: size>=256 -> %d shared=%s\n", sm.size() >= 256, sm2.data(): char&)
        sm2.drop()
        sm.drop()
    shm_remove(nm)

    # ---- shm 标志：只读附着 + 独占创建 ----
    shm_remove(nm)
    var w: shm
    if w.make(nm, 256, 0)                          # 创建可写
        var wd: char& = w.data(): char&
        sprintf(wd, "ro-%d", 7)
        var ro: shm
        if ro.make(nm, 256, 1)                     # SHM_RDONLY 只读附着
            printf("shm rdonly: see=%s size>=256 -> %d\n", ro.data(): char&, ro.size() >= 256)
            ro.drop()
        var ex: shm                                # SHM_EXCL 独占创建：已存在应失败
        printf("shm excl on existing -> fail=%d\n", !ex.make(nm, 256, 2))
        w.drop()
    shm_remove(nm)

    mem_teardown()
    printf("mem feature ok\n")
    return 0
