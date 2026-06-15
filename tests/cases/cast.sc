# 专项回归：强制类型转换 Cast 节点（覆盖各 cast 形态）
#   - 赋值右值裸转  expr: type
#   - 调用结果/实参位置裸转
#   - 括号转后 -> 成员访问  (expr: type&)->m
#   - 多级指针转  type&&
inc stdio.h
inc stdlib.h

def box: {
    v: i4
}

fnc main: i4
    # 1. 赋值右值裸转（窄化）
    var big: i8 = 1000
    var n: i4 = big: i4
    printf("assign=%d\n", n)

    # 2. 括号转后 -> 访问
    var b: box
    b.v = 42
    var pv&: void = &b
    printf("deref=%d\n", (pv: box&)->v)

    # 3. 调用结果裸转 + 实参位置裸转
    var raw&: void = malloc(8)
    var pb&: box = raw: box&
    pb->v = 7
    printf("heap=%d\n", pb->v)
    free(raw: void&)

    # 4. 多级指针转（castPtr=2）
    var sp&: box = &b
    var ppb&&: box = &sp
    var qq&&: box = ppb: box&&
    printf("pp=%d\n", qq[0]->v)
    return 0
