# 由 scc --emit-sc 从 AST 再生成

cls Dog: {
    age: i4
    init: fnc
        this->age = 5
    drop: fnc
        printf("Dog drop\n")
    LEGS: fnc: tril, out: i4&
        *out = 4
        return positive
}

cls Node: ~ {
    age: i4
    init: fnc
        this->age = 7
    drop: fnc
        printf("Node drop\n")
    LEGS: fnc: tril, out: i4&
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
    return 0
