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

# 函数类型定义
fnc add_f: i4, a:i4, b: i4

# 用预定义函数类型定义函数实现
fnc add1 -> add_f
    return a + b
fnc add2 -> add_f
    return a + 2*b

# 无值返回函数类型定义
fnc dec_f: v:i4
fnc dec -> dec_f
    --v

# 直接定义并实现函数
fnc clamp: i4, v:i4, lo:i4, hi:i4
    if v < lo
        return lo
    if v > hi
        return hi
    return v

# 直接定义并实现函数 + 换行形式定义多行参数
# + 这里要用 '-' 分隔函数体
fnc area: i4
    r&: rect
    -
    var w: i4 = r->rb.x - r->lt.x
    var h: i4 = r->rb.y - r->lt.y
    return w * h

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

    # 变量定义不指定类型时，默认根据字面量类型推断
    var msg: = "hello" " " "sc"        # 相邻字符串字面量拼接（C 风格）
    printf("%s\n", msg)
    printf("add1(3,4) = %d\n", add1(p.x, p.y))
    printf("add2(3,4) = %d\n", add2(p.x, p.y))
    printf("area = %d\n", area(&r))
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10))

    # 多行条件表达式（同样支持单行条件表达式
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
