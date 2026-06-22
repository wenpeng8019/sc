# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

@def Color: [
    Red = 0
    Green
] : i1

@def Point: {
    x: i4
    y: i4
}

@var total: i4 = 0

@fnc add: i4, a: i4, b: i4
    return a + b

@def tracker: {
    val: i4
    init: fnc
        this->val = 0
    add: fnc: k: i4
        this->val = (this->val + k)
    fnc read::: i4
}

fnc main: i4
    printf("add = %d\n", add(2, 3))
    total = add(10, 20)
    printf("total = %d\n", total)
    return 0
