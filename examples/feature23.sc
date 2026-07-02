# 特性 23：类型限定符 const/volatile/restrict + 现代初始化列表
#
# 限定符写在“类型一侧”（冒号之后）：
#   - const / volatile 写在类型名之前 → 限定被指对象
#   - restrict 跟在指针 & 之后        → 形参不别名
#   - 用 let 声明 → 绑定本身只读（标量只读 / 指针为 const 指针）
#   - 强转表达式同样可写限定符：(e: const T&) / (e: volatile T&) / (e: T& restrict)
# 初始化列表：数组用 [...] / 结构体用 {...} / 指定成员用 {name = expr}

def point: {
    x: i4
    y: i4
}

#-------- restrict 限定的形参：编译器可假设 dst/src 不别名 --------
fnc copy_n: i4, dst: i4& restrict, src: const i4& restrict, n: i4
    var i: i4 = 0
    while i < n
        dst[i] = src[i]
        i = i + 1
    return 0

#-------- 接收“指向 const 对象”的指针：只能读，不能改其内容 --------
fnc sum_point: i4, p: const point&
    return p->x + p->y

fnc main: i4
    # 数组初始化列表（方括号）
    var src[3]: i4 = [10, 20, 30]
    var dst[3]: i4 = [0, 0, 0]
    copy_n(&dst[0], &src[0], 3)
    ::printf("copy:      %d %d %d\n", dst[0], dst[1], dst[2])

    # 结构体初始化：指定成员（花括号）
    var pt: point = {x = 9, y = 11}
    ::printf("sum_point: %d\n", sum_point(&pt))

    # volatile 标量（映射寄存器、被外部修改的标志位等场景）
    var flag: volatile i4 = 1
    ::printf("flag:      %d\n", flag)

    # let 让指针本身只读（const 指针），但所指对象仍可改
    let q: point& = &pt
    q->x = 100
    ::printf("q->x:      %d\n", q->x)

    # 强转表达式上的类型限定符：与声明侧同义（const/volatile 前缀，restrict 尾置）
    var raw: i4& = &pt.x
    let ro: const i4& = (raw: const i4&)     # 转成“指向 const 的指针”
    ::printf("ro:        %d\n", *ro)
    var vp: volatile i4& = (&flag: volatile i4&)
    ::printf("vp:        %d\n", *vp)

    return 0
