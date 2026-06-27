# object@：类型擦除自动指针。验证三件套——
#   1. 维度调用经擦除的 _class 槽派发（(*(object)o.p)(o.p, SC_DIM_Dim, ...)）；
#   2. 绑定时 .p 存 _class 槽指针（非实体基址，覆盖 _class 偏移≠0 的链表类）、.tar 携带引用头；
#   3. 入边归零经通用蹦床 sc_obj_drop → SC_DIM_DROP 动态派发到正确析构。

cls Dog: {
    age: i4
    init: fnc
        this->age = 5
    drop: fnc
        printf("Dog drop\n")
    dim LEGS: tril, out: i4&
        *out = 4
        return positive
}

# 链表类：_class 在 _prev/_next 之后（偏移≠0），证明擦除后仍经派发器 container_of 正确还原 this。
cls Node: ~ {
    age: i4
    init: fnc
        this->age = 7
    drop: fnc
        printf("Node drop\n")
    dim LEGS: tril, out: i4&
        *out = this->age
        return positive
}

fnc main: i4
    var d: Dog@ = Dog()
    var od: object@ = d
    var nd: i4
    od.LEGS(&nd)
    printf("dog legs=%d\n", nd)

    var k: Node@ = Node()
    var ok: object@ = k
    var nk: i4
    ok.LEGS(&nk)
    printf("node legs=%d\n", nk)

    # object@ → 具体类 T@ 还原（downcast）：源 .p 是擦除的 _class 槽，
    # 须 container_of 回算实体基址（Node 为 ~ 链表类，_class 偏移≠0，覆盖偏移修复）。
    var bk: Node@ = (ok: Node@)
    printf("back age=%d\n", bk->age)

    # object@ → 裸 @ → 具体类 T@：裸 @ 承接 object@ 即 object 风味（dtor=sc_obj_drop、
    # .p=&_class）。还原 Node@ 时运行时据 dtor==sc_obj_drop 补 offsetof(Node,_class)，
    # 验证 object@→@→T@ 缺口已补（Node 偏移≠0，错读会得 age=0）。
    var be: @ = ok
    var back2: Node@ = (be: Node@)
    printf("back2 age=%d\n", back2->age)

    # 具体类 T@ → 裸 @ → object@：class 类型擦除统一走 object 风味（.p 偏移到 &_class、
    # dtor=sc_obj_drop），裸 @ 始终携带派发能力，故可无损还原 object@（四向对称）。
    var d2: Dog@ = Dog()
    var bd: @ = d2
    var od2: object@ = (bd: object@)
    var nd2: i4
    od2.LEGS(&nd2)
    printf("od2 legs=%d\n", nd2)
    return 0
