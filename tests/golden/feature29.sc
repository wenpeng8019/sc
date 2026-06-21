# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc mem.sc

fnc main: i4
    var p: & = chunk(100)
    var s: char& = (p: char&)
    sprintf(s, "pooled-%d", 42)
    printf("chunk(100): usable>=100 -> %d, text=%s\n", mem_usable(p) >= 100, s)
    p = refit(p, 5000)
    printf("refit(5000): usable>=5000 -> %d, keep=%s\n", mem_usable(p) >= 5000, (p: char&))
    recycle(p)
    var z: & = chunk0(64)
    var zb: u1& = (z: u1&)
    printf("chunk0(64): first=%d last=%d\n", zb[0], zb[63])
    recycle(z)
    var arr: & = chunk_array(10, 8)
    var ab: u1& = (arr: u1&)
    printf("chunk_array(10,8): usable>=80 -> %d zeroed=%d\n", mem_usable(arr) >= 80, ab[0])
    recycle(arr)
    printf("chunk_array overflow -> nil=%d\n", chunk_array(0xFFFFFFFFFFFFFFFFUL, 2) == nil)
    var al: & = chunk_aligned(200, 64)
    var alb: u1& = (al: u1&)
    alb[0] = 7
    alb[199] = 9
    printf("chunk_aligned(200,64): usable>=200 -> %d rw=%d\n", mem_usable(al) >= 200, alb[0] + alb[199])
    recycle(al)
    printf("chunk_aligned bad-align -> nil=%d\n", chunk_aligned(64, 48) == nil)
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
    var p2: & = chunk(1000)
    var p3: & = chunk(2000)
    var st: mem_stat_t
    mem_stat(&st)
    printf("stat live: count=%llu live>=3000 -> %d peak>=live -> %d allocs>=2 -> %d\n", st.count, st.live >= 3000, st.peak_live >= st.live, st.allocs >= 2)
    recycle(p2)
    recycle(p3)
    mem_stat(&st)
    printf("stat freed: count=%llu live=%llu peak>=3000 -> %d\n", st.count, st.live, st.peak_live >= 3000)
    var k: i4 = 0
    for k = 0; k < 200; k++
        var t: & = chunk(64)
        recycle(t)
    var freed: u8 = mem_trim()
    printf("mem_trim: freed>0 -> %d\n", freed > 0)
    var nm: char& = "sc_feature29_region"
    shm_remove(nm)
    var sm: shm
    if sm.make(nm, 256, 0)
        var sd: char& = (sm.data(): char&)
        sprintf(sd, "shm-%d", 29)
        var sm2: shm
        sm2.make(nm, 256, 0)
        printf("shm: size>=256 -> %d shared=%s\n", sm.size() >= 256, (sm2.data(): char&))
        sm2.drop()
        sm.drop()
    shm_remove(nm)
    shm_remove(nm)
    var w: shm
    if w.make(nm, 256, 0)
        var wd: char& = (w.data(): char&)
        sprintf(wd, "ro-%d", 7)
        var ro: shm
        if ro.make(nm, 256, 1)
            printf("shm rdonly: see=%s size>=256 -> %d\n", (ro.data(): char&), ro.size() >= 256)
            ro.drop()
        var ex: shm
        printf("shm excl on existing -> fail=%d\n", !ex.make(nm, 256, 2))
        w.drop()
    shm_remove(nm)
    mem_teardown()
    printf("mem feature ok\n")
    return 0
