# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

@fnc main: i4
    var a: string@1 = string("a")
    a->drop()
    a->drop()
    var b: string@1 = string("b")
    if false
        b->drop()
    return 0
