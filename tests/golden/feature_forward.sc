# 由 scc --emit-sc 从 AST 再生成

@def node_a: {
    pb: node_b&
}

@def node_b: {
    pa: node_a&
}

@fnc is_even: bool, n: i4
    if n == 0
        return true
    return is_odd(n - 1)

@fnc is_odd: bool, n: i4
    if n == 0
        return false
    return is_even(n - 1)

fnc main: i4
    var x: node_a
    var y: node_b
    x.pb = &y
    y.pa = &x
    printf("even(10)=%d odd(7)=%d\n", is_even(10), is_odd(7))
    return 0
