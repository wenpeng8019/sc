# 专项回归：表达式节点（sizeof / offsetof / 三目 / 成员/下标）

def pair: {
    a: i4
    b: i4
}

fnc main: i4
    var p: pair
    p.a = 3
    p.b = 4

    # sizeof：表达式与类型两种形态
    ::printf("sz_expr=%lld sz_type=%lld\n", sizeof(p): i8, sizeof(pair): i8)

    # offsetof：text=类型名, op=成员名
    ::printf("off_a=%lld off_b=%lld\n", offsetof(pair, a): i8, offsetof(pair, b): i8)

    # 三目
    var m: i4 = p.a > p.b ? p.a : p.b
    ::printf("max=%d\n", m)

    # 成员/下标
    var arr[3]: i4
    arr[0] = p.a
    arr[1] = p.b
    arr[2] = m
    ::printf("sum=%d\n", arr[0] + arr[1] + arr[2])
    return 0
