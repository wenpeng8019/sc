# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def obj: {
    abc: i4
    func: fnc: i4, o&: obj, x: i4, y: i4
}

fnc obj_add: i4, o&: obj, x: i4, y: i4
    return (o->abc + x) + y

fnc main: i4
    var ok: bool = true
    var no: bool = false
    printf("ok=%d no=%d\n", ok, no)
    var p&: i4 = nil
    if p == nil
        printf("p is nil\n")
    var o: obj
    o.abc = 10
    if o.func == nil
        printf("func is nil\n")
    o.func = obj_add
    printf("o.func(2,3) = %d\n", o.func(&o, 2, 3))
    var po&: obj = &o
    printf("po->func(4,5) = %d\n", po->func(po, 4, 5))
    return 0
