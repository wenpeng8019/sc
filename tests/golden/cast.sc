# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc stdlib.h

def box: {
    v: i4
}

fnc main: i4
    var big: i8 = 1000
    var n: i4 = (big: i4)
    printf("assign=%d\n", n)
    var b: box
    b.v = 42
    var pv: void& = &b
    printf("deref=%d\n", (pv: box&)->v)
    var raw: void& = malloc(8)
    var pb: box& = (raw: box&)
    pb->v = 7
    printf("heap=%d\n", pb->v)
    free((raw: void&))
    var sp: box& = &b
    var ppb: box&& = &sp
    var qq: box&& = (ppb: box&&)
    printf("pp=%d\n", qq[0]->v)
    return 0
