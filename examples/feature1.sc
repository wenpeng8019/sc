# 特性 1：核心语法 —— def（枚举/结构/联合/别名）、fnc（函数类型/实现/多行参数）、
# var/let、指针 &、控制流（if/for/while/break）、多行条件表达式
# 系列起点：后续 feature N 在此基础上逐个验证新增特性

# 枚举
def color: i1
    Red = 0
    Green
    Blue

# 结构
def point: {
    x: i4
    y: i4
}

# 单行结构定义
def rect: { lt: point, rb: point }

# 联合
def value: (
    i: i4
    f: f4
)

# 自定义类型别名
def byte -> u1

# 函数类型定义 + 实现
fnc binop_t: i4, a:i4, b: i4

fnc add -> binop_t
    return a + b

fnc sub -> binop_t
    return a - b

# 直接定义并实现（多行参数，'-' 分隔函数体）
fnc area:
    r&: rect
    -
    var w: i4 = r->rb.x - r->lt.x
    var h: i4 = r->rb.y - r->lt.y
    return w * h

# 单行函数定义
fnc clamp: i4, v:i4, lo:i4, hi:i4
    if v < lo
        return lo
    if v > hi
        return hi
    return v

# 全局变量/常量
let MAX: i4 = 100
var counter: i4 = 0

fnc main: i4
    var p: point
    p.x = 3
    p.y = 4

    var r: rect
    r.lt.x = 0, r.lt.y = 0
    r.rb.x = 10, r.rb.y = 5

    var r_ptr&: rect = &r

    var msg: = "hello sc"
    printf("%s\n", msg)
    printf("add(3,4) = %d\n", add(p.x, p.y))
    printf("sub(3,4) = %d\n", sub(p.x, p.y))
    printf("area = %d\n", area(&r))
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10))

    # 多行条件表达式
    if p.x > 1
        && p.y < 10
        -
        printf("cond ok\n")
    else
        printf("cond fail\n")

    # 循环
    var i: i4
    for i = 0; i < 3; i++
        counter += i
    printf("counter = %d\n", counter)

    while counter > 0
        counter--
        if counter == 1
            break

    var c: color = Green
    printf("color = %d, MAX = %d\n", c, MAX)
    return 0
