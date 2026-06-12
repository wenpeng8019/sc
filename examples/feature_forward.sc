# 专项验证：定义顺序无关（结构前置声明 + 函数原型，含互递归）
inc stdio.h

# 先引用后定义：node_a 里用到 node_b，node_b 在后面定义
@def node_a: {
    pb&: node_b
}

@def node_b: {
    pa&: node_a
}

# 互递归函数（先声明/后定义顺序不重要）
@fnc is_even: bool, n:i4
    if n == 0
        return true
    return is_odd(n - 1)

@fnc is_odd: bool, n:i4
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
