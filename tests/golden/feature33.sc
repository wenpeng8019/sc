# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def Point: {
    x: i4
    y: i4
}

def Vec: <T>, N
    def Vec_\N: {
        data[8]: T
        len: i4
    }
    fnc Vec_\N\_push: void, v: Vec_\N&, x: T
        v->data[v->len] = x
        v->len = (v->len + 1)
    fnc Vec_\N\_get: T, v: Vec_\N&, i: i4
        return v->data[i]

def max_of: <T>, N
    fnc max_\N: T, a: T, b: T
        if a > b
            return a
        return b

mix Vec(i4, int)

mix Vec(f8, dbl)

mix Vec(Point, pt)

mix max_of(i4, i)

mix max_of(f8, d)

fnc main: i4
    var vi: Vec_int
    vi.len = 0
    Vec_int_push(&vi, 10)
    Vec_int_push(&vi, 20)
    Vec_int_push(&vi, 30)
    printf("Vec_int: %d %d %d\n", Vec_int_get(&vi, 0), Vec_int_get(&vi, 1), Vec_int_get(&vi, 2))
    var vd: Vec_dbl
    vd.len = 0
    Vec_dbl_push(&vd, 1.5)
    Vec_dbl_push(&vd, 2.5)
    printf("Vec_dbl: %g %g\n", Vec_dbl_get(&vd, 0), Vec_dbl_get(&vd, 1))
    var vp: Vec_pt
    vp.len = 0
    var p0: Point = {3, 4}
    Vec_pt_push(&vp, p0)
    var got: Point = Vec_pt_get(&vp, 0)
    printf("Vec_pt[0]: (%d, %d)\n", got.x, got.y)
    printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", max_i(3, 7), max_d(1.5, 0.5))
    return 0
