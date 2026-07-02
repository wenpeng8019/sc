# 特性 14：每对象方法指针（MethodPtr）—— 伪类无派生下的"每对象虚方法/接口"
#
# 语法：在结构体内以 `fnc name: 返回类型, 参数...`（fnc 前置、无函数体）声明，
#       即得到一个"每对象方法指针"字段（占存储、默认 nil）。
# 调用端：隐藏接收者，按成员函数约定自动注入——
#         o.m(x)  → (*o.m)(&o, x)；  p->m(x) → (*p->m)(p, x)
# 赋值端：显式接收者——所赋函数须以 _this: T& 作为首参（与普通函数指针对齐）。
# 对比：`name: fnc:`（名字前置）是"普通函数指针"字段，接收者不注入、调用须自行传。

def handler: {
    tag: i4
    fnc op: i4, x: i4          # 每对象方法指针：返回 i4、参数 x（接收者 handler& 隐藏）
}

# 实现函数：接收者显式为首参 _this: handler&
fnc dbl: i4, _this: handler&, x: i4
    return x * 2 + _this->tag

fnc neg: i4, _this: handler&, x: i4
    return 0 - x - _this->tag

fnc main: i4
    var a: handler
    a.tag = 100
    a.op = dbl                 # 绑定实现（函数地址直存）

    var b: handler
    b.tag = 1
    b.op = neg

    # 值接收者调用：自动注入 &a / &b
    ::printf("a.op(5) = %d\n", a.op(5))      # dbl: 5*2+100 = 110
    ::printf("b.op(5) = %d\n", b.op(5))      # neg: -5-1    = -6

    # 指针接收者调用：自动注入 p
    var p: handler& = &a
    ::printf("p->op(7) = %d\n", p->op(7))    # dbl: 7*2+100 = 114

    # 未绑定默认 nil：可判空
    var c: handler
    c.tag = 0
    if c.op == nil
        ::printf("c.op is nil\n")

    # 绑定后再调用
    c.tag = 10
    c.op = dbl
    ::printf("c.op(5) = %d\n", c.op(5))      # 5*2+10 = 20
    return 0
