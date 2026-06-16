# 特性 1：基本语法
# - inc 引入 C 头文件
# — def（枚举/结构/联合/别名）
# - var/let（变量/常量/全局定义/默认类型/类型推断/内联/初始化）
# - &/&&（指针/多级指针/类型强转）
# - []（数组/多维数组/初始化/遍历）
# - bool/true/false/nil/0x...（布尔类型/字面量/数值）
# - sizeof / offsetof / 初始化列表
# - 控制流（if/else if/for/while/do-while/break/continue/goto/case）、多行条件表达式

inc stdio.h
inc stdlib.h

#-------------- def：枚举 / 结构 / 联合 / 别名 -----------------

# 枚举（支持显式赋值与自动递增）
def color: i1
    Red = 0
    Green
    Blue

# 结构体
def point: {
    x: i4
    y: i4
}

# 单行结构定义
def rect: { lt: point, rb: point }

# 匿名内联结构：字段类型直接写 {} ，无需预先 def
def obj: {
    id: i4
    meta: {
        tag: i4
        flag: bool
    }
}

# 联合体
def value: (
    i: i4
    f: f4
)

# 类型别名
def byte -> u1

#-------------- var/let：变量 / 常量 --------------------------
# 全局
let MAX: i4 = 100
var counter: i4 = 0
let grid[2][3]: i4

# 指针默认值、类型推断
fnc main: i4

    # 显式类型 + 初值
    var a: i4 = 42
    printf("a=%d\n", a)

    # 无初值（默认零值）
    var b: i4
    printf("b=%d\n", b)

    # 类型推断：整数字面量 → i4，浮点 → f8
    var n: = 100
    var pi: = 3.14
    printf("n=%d pi=%.2f\n", n, pi)

    # 默认类型：省略类型和初值 → char&
    var msg:
    msg = "hello"
    printf("%s\n", msg)

    # 多行变量声明
    var i: i4 = 0
        j: i4 = 0

    # 内联类型：变量直接声明内嵌结构体
    var tmp: {
        x: i4
        y: i4
    }
    tmp.x = 1, tmp.y = 2
    printf("inline var: x=%d y=%d\n", tmp.x, tmp.y)

    # 结构体初始化列表
    var pt: point = {5, 6}
    printf("pt: x=%d y=%d\n", pt.x, pt.y)

    # 匿名内联结构字段访问
    var o: obj
    o.id = 1
    o.meta.tag = 10
    o.meta.flag = true
    printf("obj: id=%d meta.tag=%d meta.flag=%d\n", o.id, o.meta.tag, o.meta.flag)

    #-------------- & / &&：指针 / 多级指针 --------------------

    # 指针初值为 nil（fixme: 指针变量应该是 var np: i4& = nil，var np&: i4 = nil 语法不太合理）
    var np&: i4 = nil
    if np == nil
        printf("np is nil\n")

    # 无类型指针默认 void&
    var vp&: = nil
    printf("vp=%p\n", vp)

    var pt2: point
    pt2.x = 7, pt2.y = 8
    var px&: point = &pt2
    printf("px->x=%d\n", px->x)

    # 多级指针 &&
    var pp&&: point = &px
    printf("pp=%p\n", pp)

    #-------------- []：数组 / 多维数组 / 初始化列表 -----------

    # 初始化列表（一维）
    var arr[3]: i4 = {10, 20, 30}
    printf("arr: %d %d %d\n", arr[0], arr[1], arr[2])

    # 多维数组初始化列表（可嵌套、允许尾逗号）
    var tab[2][3]: i4 = {
        {1, 2, 3},
        {4, 5, 6},
    }
    printf("tab[0][1]=%d tab[1][2]=%d\n", tab[0][1], tab[1][2])

    # 多维数组遍历赋值
    var m[2][3]: i4
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            m[i][j] = i * 3 + j
    var s: i4 = 0
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            s += m[i][j]
    printf("sum = %d\n", s)

    # 多维字符数组
    var name[8][16]: char
    strcpy(name[0], "hi")
    printf("name0 = %s\n", name[0])

    #-------------- 字面量：bool/nil/0x/后缀 -------------------

    var ok: bool = true
    var no: bool = false
    printf("ok=%d no=%d\n", ok, no)

    # 十六进制
    var mask: u4 = 0xFF00
    printf("mask=0x%x\n", mask)

    # 字面量后缀：u/U/l/L/f 可组合
    var big: u8 = 100UL
    let pi_f: f4 = 3.14f
    printf("big=%llu pi_f=%.2f\n", big, pi_f)

    #-------------- 裸强转：expr : type& 右值免括号 --------------

    # 值类型强转
    var big2: i8 = 300
    var small: i4 = big2: i4
    printf("cast: %d\n", small)

    # 指针强转
    var buf&: char = malloc(8): char&
    free(buf: void&)

    var f: f8 = 3.75
    printf("cast expr: %d\n", small + f: i4)

    # -> 后续操作需括号
    var pv&: void = &tmp
    printf("paren cast: %d\n", (pv: point&)->x)

    #-------------- sizeof / offsetof -------------------------

    printf("sizeof(point)=%lu\n", sizeof(point))
    printf("offsetof(point,y)=%lu\n", offsetof(point, y))

    #-------------- 控制流 ------------------------------------

    # if / else if / else
    if tmp.x == 1
        printf("one\n")
    else if tmp.x == 2
        printf("two\n")
    else
        printf("other\n")

    # 多行条件表达式
    if tmp.x > 0
        && tmp.y < 10
        -
        printf("cond ok\n")
    else
        printf("cond fail\n")

    # for（三段表达式；可省略某段）
    counter = 0
    for i = 0; i < 3; i++
        counter += i
    printf("counter = %d\n", counter)

    for ; counter < 10; counter++
        printf("counter at %d\n", counter)

    # while + break
    while counter > 3
        counter--
        if counter == 5
            break

    # do ... while
    i = 0
    do
        i++
    while i < 3
    printf("do-while: i=%d\n", i)

    # continue
    for i = 0; i < 5; i++
        if i == 2
            continue
        printf("  i=%d\n", i)

    # case（替代 switch，默认自动 break，through 贯穿）
    var code: i4 = 2
    case code:
        1, 2:
            printf("case 1 or 2\n")
        3:
            printf("case 3\n")
            through
        4:
            printf("case 3 through to 4\n")
        :
            printf("default\n")

    # goto 与标签
    var cnt: i4 = 0
    again:
        cnt++
        if cnt < 2
            goto again
    printf("goto: cnt=%d\n", cnt)

    return 0
