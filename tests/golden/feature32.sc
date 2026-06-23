# 由 scc --emit-sc 从 AST 再生成

def def_max: T, N
    fnc max_\N: T, a: T, b: T
        if a > b
            return a
        return b

mix def_max(int32_t, i)

mix def_max(double, d)

def def_vec: T, N
    def Vec_\N: {
        data: T&
        len: i4
        cap: i4
    }
    fnc Vec_\N\_push: v: Vec_\N&, x: T
        v->data[v->len] = x
        v->len = (v->len + 1)
    fnc Vec_\N\_get: T, v: Vec_\N&, i: i4
        return v->data[i]

mix def_vec(int32_t, int)

mix def_vec(double, dbl)

def Point: {
    x: i4
    y: i4
}

mix def_vec(Point, pt)

fnc main: i4
    printf("max_i(3,7)=%d  max_d(1.5,0.5)=%g\n", max_i(3, 7), max_d(1.5, 0.5))
    var ibuf[4]: i4
    var iv: Vec_int = {ibuf, 0, 4}
    Vec_int_push(&iv, 10)
    Vec_int_push(&iv, 20)
    printf("Vec_int: %d %d\n", Vec_int_get(&iv, 0), Vec_int_get(&iv, 1))
    var dbuf[4]: f8
    var dv: Vec_dbl = {dbuf, 0, 4}
    Vec_dbl_push(&dv, 1.5)
    printf("Vec_dbl: %g\n", Vec_dbl_get(&dv, 0))
    var pbuf[4]: Point
    var pv: Vec_pt = {pbuf, 0, 4}
    var p0: Point = {3, 4}
    Vec_pt_push(&pv, p0)
    var p: Point = Vec_pt_get(&pv, 0)
    printf("Vec_pt: %d %d\n", p.x, p.y)
    return 0
