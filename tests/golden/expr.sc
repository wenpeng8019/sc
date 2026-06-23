# 由 scc --emit-sc 从 AST 再生成

def pair: {
    a: i4
    b: i4
}

fnc main: i4
    var p: pair
    p.a = 3
    p.b = 4
    printf("sz_expr=%lld sz_type=%lld\n", (sizeof(p): i8), (sizeof(pair): i8))
    printf("off_a=%lld off_b=%lld\n", (offsetof(pair, a): i8), (offsetof(pair, b): i8))
    var m: i4 = (p.a > p.b) ? p.a : p.b
    printf("max=%d\n", m)
    var arr[3]: i4
    arr[0] = p.a
    arr[1] = p.b
    arr[2] = m
    printf("sum=%d\n", (arr[0] + arr[1]) + arr[2])
    return 0
