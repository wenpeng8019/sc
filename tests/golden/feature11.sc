# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

fnc demo_scalar
    var x: i4 = 0
    x.set(42)
    var y: i4 = x.get()
    printf("scalar: set(42) get()=%d\n", y)

fnc bump: p: i4&
    var cur: i4 = p->get_acq()
    p->set_rel(cur + 1)

fnc main: i4
    demo_scalar()
    var n: i4 = 10
    bump(&n)
    bump(&n)
    printf("pointer: bump x2 -> %d\n", n.get())
    var f: f8 = 1.5
    f.set(3.25)
    printf("f8: set(3.25) get()=%.2f\n", f.get())
    return 0
