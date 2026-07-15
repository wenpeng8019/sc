# 堆专属指针显式 drop 的幂等性：重复调用安全，条件未执行时仍由 T@1 退域回收。
inc adt.sc

@fnc main: i4
    var a: string@1 = string("a")
    a->drop()
    a->drop()

    var b: string@1 = string("b")
    if false
        b->drop()
    return 0
