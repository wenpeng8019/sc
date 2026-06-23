# 由 scc --emit-sc 从 AST 再生成

def point: {
    x: i4
    y: i4
}

def rect: {
    lt: point
    rb: point
}

fnc clamp: i4, v: i4, lo: i4, hi: i4
    if v < lo
        return lo
    if v > hi
        return hi
    return v

fnc area: i4, r: rect&
    var w: i4 = r->rb.x - r->lt.x
    var h: i4 = r->rb.y - r->lt.y
    return w * h

fnc add_f: i4, a: i4, b: i4

fnc add1 -> add_f
    return a + b

fnc add2 -> add_f
    return a + (2 * b)

fnc dec_f: v: i4

fnc dec -> dec_f
    --v

fnc add3: i4, a: i4, b: i4, c: i4
    return (a + b) + c

fnc desc: i4, s: char&, pt: point
    if s == nil
        return pt.x + pt.y
    return 100

def obj: {
    abc: i4
    func1: fnc: i4, o: obj&, x: i4, y: i4
    func2: add_f
    fnc scale: i4, k: i4
}

fnc obj_add: i4, o: obj&, x: i4, y: i4
    return (o->abc + x) + y

fnc obj_scale: i4, _this: obj&, k: i4
    return _this->abc * k

fnc sq: i4, x: i4
    return x * x

fnc my_printf: fmt: char&, ...
    var ap: va_list
    va_start(ap, fmt)
    vprintf(fmt, ap)
    va_end(ap)

fnc main: i4
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10))
    var r: rect
    (r.lt.x = 0) , (r.lt.y = 0)
    (r.rb.x = 10) , (r.rb.y = 5)
    printf("area = %d\n", area(&r))
    printf("add1(3,4) = %d\n", add1(3, 4))
    printf("add2(3,4) = %d\n", add2(3, 4))
    var cb: fnc: i4, x: i4
    cb = sq
    printf("cb(7) = %d\n", cb(7))
    var o: obj
    o.abc = 10
    if o.func1 == nil
        printf("func1 is nil\n")
    o.func1 = obj_add
    printf("o.func1(2,3) = %d\n", o.func1(&o, 2, 3))
    printf("po->func1(4,5) = %d\n", o.func1(&o, 4, 5))
    if o.scale == nil
        printf("scale is nil\n")
    o.scale = obj_scale
    printf("o.scale(3) = %d\n", o.scale(3))
    var po: obj& = &o
    printf("po->scale(4) = %d\n", po->scale(4))
    my_printf("sc says: %s %d\n", "hello", 42)
    printf("add3(7) = %d\n", add3(7))
    printf("add3(1,2) = %d\n", add3(1, 2))
    printf("desc() = %d\n", desc())
    printf("o.func1(&o) = %d\n", o.func1(&o))
    printf("cb() = %d\n", cb())
    return 0
