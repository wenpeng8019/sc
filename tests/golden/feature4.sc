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
    drop: fnc
        printf("point(%d,%d) dropped\n", this->x, this->y)
}

def obj: {
    id: i4
    dump: fnc
    calc: fnc: i4, a: i4, b: i4
    fnc dump::
    fnc calc::: i4, a: i4, b: i4
}

fnc main: i4
    var pt: point
    printf("init: x=%d y=%d\n", pt.x, pt.y)
    printf("pt.sum(3,4) = %d\n", pt.sum(3, 4))
    var pp&: point = &pt
    printf("pp->sum(3,4) = %d\n", pp->sum(3, 4))
    var p2: point
    printf("op is nil (before bind): %d\n", p2.op == nil)
    p2.drop()
    var hp&: point = point()
    printf("heap: sum() = %d\n", hp->sum())
    hp->drop()
    free(hp)
    return 0
