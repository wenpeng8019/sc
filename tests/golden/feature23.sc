# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def point: {
    x: i4
    y: i4
}

fnc copy_n: i4, dst: i4& restrict, src: const i4& restrict, n: i4
    var i: i4 = 0
    while i < n
        dst[i] = src[i]
        i = (i + 1)
    return 0

fnc sum_point: i4, p: const point&
    return p->x + p->y

fnc main: i4
    var src[3]: i4 = [10, 20, 30]
    var dst[3]: i4 = [0, 0, 0]
    copy_n(&dst[0], &src[0], 3)
    printf("copy:      %d %d %d\n", dst[0], dst[1], dst[2])
    var pt: point = {x = 9, y = 11}
    printf("sum_point: %d\n", sum_point(&pt))
    var flag: volatile i4 = 1
    printf("flag:      %d\n", flag)
    let q: point& = &pt
    q->x = 100
    printf("q->x:      %d\n", q->x)
    return 0
