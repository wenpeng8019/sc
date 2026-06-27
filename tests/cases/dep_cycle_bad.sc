# 负向：token 依赖图（dep…map）存在有向环，编译期应报错（依赖图须为 DAG）。
tok a: "g.a"
tok b: "g.b"

dep any: x:"g.a" map y:"g.b"
    y->set((x->get(): @), 0)
    return false

dep any: x:"g.b" map y:"g.a"
    y->set((x->get(): @), 0)
    return false

fnc main: i4
    return 0
