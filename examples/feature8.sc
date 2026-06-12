# 特性 8：语法优化三件套
#   1. 裸强转：右值位置 expr: type& 免括号（仅 -> 等后续操作时仍需括号）
#   2. 默认补 0：调用实参少于形参时，剩余参数自动以 0/nil/{0} 补全
#   3. 成员函数：结构体内直接实现（签名字段 + 缩进函数体）；无函数体即普通函数指针字段
inc stdio.h
inc stdlib.h

def point: {
    x: i4
    y: i4
    # 成员函数：有缩进函数体 → 方法（隐式 this）
    init: fnc
        this->x = 1
        this->y = 2
    sum: fnc: i4, dx: i4, dy: i4
        return this->x + this->y + dx + dy
    # 无函数体 → 普通函数指针字段（默认 nil）
    op: fnc: i4, p&: point, k: i4
}

fnc point_scale: i4, p&: point, k: i4
    return (p->x + p->y) * k

# 默认补 0：普通函数
fnc add3: i4, a: i4, b: i4, c: i4
    return a + b + c

# 默认补 0：指针/聚合参数 → nil / {0}
fnc desc: i4, s&: char, pt: point
    if s == nil
        return pt.x + pt.y
    return 100

rpc job: i4, a: i4, b: i4
    return a + b

fnc main: i4
    # --- 1. 裸强转 ---
    var big: i8 = 300
    var small: i4 = big: i4                  # 赋值右值
    printf("cast assign: %d\n", small)
    var buf&: char = malloc(8): char&        # 调用结果裸转指针
    free(buf: void&)                         # 实参位置裸转
    var f: f8 = 3.75
    printf("cast arg: %d\n", small + f: i4)  # 表达式尾部裸转
    # 三目分支内仍需括号；取消引用等后续操作也需括号
    var pt: point
    var pv&: void = &pt
    printf("paren cast deref: %d\n", (pv: point&)->x)

    # --- 2. 默认补 0 ---
    printf("add3(7) = %d\n", add3(7))            # b,c 补 0 → 7
    printf("add3(1,2) = %d\n", add3(1, 2))       # c 补 0 → 3
    printf("desc() = %d\n", desc())              # s 补 nil、pt 补 {0} → 0
    printf("pt.sum(10) = %d\n", pt.sum(10))      # 方法尾参补 0 → 1+2+10
    pt.op = point_scale
    printf("pt.op(&pt) = %d\n", pt.op(&pt))      # 函数指针字段，k 补 0 → 0
    printf("job(5) = %d\n", job(5))              # rpc 直接调用：b 补 0 → 5

    # --- 3. 成员函数 ---
    printf("init: x=%d y=%d\n", pt.x, pt.y)      # 声明即构造 → init 已调用
    var pp&: point = &pt
    printf("pp->sum(3,4) = %d\n", pp->sum(3, 4))
    var hp&: point = point()                     # 堆构造亦走 init
    printf("heap: sum() = %d\n", hp->sum())
    free(hp: void&)
    return 0
