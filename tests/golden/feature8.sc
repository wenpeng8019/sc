# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc stdlib.h

def point: {
    x: i4
    y: i4
    op: fnc: i4, p&: point, k: i4
    init: fnc
        this->x = 1
        this->y = 2
    sum: fnc: i4, dx: i4, dy: i4
        return ((this->x + this->y) + dx) + dy
}

fnc point_scale: i4, p&: point, k: i4
    return (p->x + p->y) * k

fnc add3: i4, a: i4, b: i4, c: i4
    return (a + b) + c

fnc desc: i4, s&: char, pt: point
    if s == nil
        return pt.x + pt.y
    return 100

rpc job: i4, a: i4, b: i4
    return a + b

fnc main: i4
    var big: i8 = 300
    var small: i4 = (big: i4)
    printf("cast assign: %d\n", small)
    var buf&: char = (malloc(8): char&)
    free((buf: void&))
    var f: f8 = 3.75
    printf("cast arg: %d\n", (small + f: i4))
    var pt: point
    var pv&: void = &pt
    printf("paren cast deref: %d\n", (pv: point&)->x)
    printf("add3(7) = %d\n", add3(7))
    printf("add3(1,2) = %d\n", add3(1, 2))
    printf("desc() = %d\n", desc())
    printf("pt.sum(10) = %d\n", pt.sum(10))
    pt.op = point_scale
    printf("pt.op(&pt) = %d\n", pt.op(&pt))
    printf("job(5) = %d\n", job(5))
    printf("init: x=%d y=%d\n", pt.x, pt.y)
    var pp&: point = &pt
    printf("pp->sum(3,4) = %d\n", pp->sum(3, 4))
    var hp&: point = point()
    printf("heap: sum() = %d\n", hp->sum())
    free((hp: void&))
    return 0
