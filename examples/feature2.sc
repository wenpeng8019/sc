# 特性 2：函数 —— fnc
# - 函数 / 多行参数函数
# - 预定义类型函数
# - 函数指针类型定义与调用
# - 由 C 实现的接口定义（::）
# - 可变参数函数（...）
# - 伪闭包机制
# - 每对象方法指针（fnc name: 无体）—— 隐藏接收者、自动注入
# - 实参默认自动补 0 机制

#-------------- 类型定义（函数依赖） -------------------------

def point: {
    x: i4
    y: i4
}

def rect: { lt: point, rb: point }

#-------------- 直接定义并实现函数 ---------------------------

fnc clamp: i4, v: i4, lo: i4, hi: i4
    if v < lo
        return lo
    if v > hi
        return hi
    return v

fnc area: i4
    r: rect&
    -
    var w: i4 = r->rb.x - r->lt.x
    var h: i4 = r->rb.y - r->lt.y
    return w * h

#-------------- 函数类型定义 ---------------------------------

fnc add_f: i4, a: i4, b: i4

fnc add1 -> add_f
    return a + b
fnc add2 -> add_f
    return a + 2*b

fnc dec_f: v: i4
fnc dec -> dec_f
    --v

#-------------- 默认补 0：缺参自动填零 -----------------------

fnc add3: i4, a: i4, b: i4, c: i4
    return a + b + c

fnc desc: i4, s: char&, pt: point
    if s == nil
        return pt.x + pt.y
    return 100

#-------------- 函数指针类型（）字段 ---------------------------------

def obj: {
    abc: i4
    func1: fnc: i4, o: obj&, x: i4, y: i4    # 普通函数指针字段（名字前置）：接收者不注入，调用须自传
    func2: add_f
    fnc scale: i4, k: i4                      # 每对象方法指针（fnc 前置、无函数体）：接收者 obj& 隐藏
}

fnc obj_add: i4, o: obj&, x: i4, y: i4
    return o->abc + x + y

# 每对象方法指针的实现：接收者显式为首参 _this: obj&（与普通函数指针赋值对齐）
fnc obj_scale: i4, _this: obj&, k: i4
    return _this->abc * k

# 函数指针回调
fnc sq: i4, x: i4
    return x * x

#-------------- :: C 实现接口 --------------------------------
# fnc name:: 仅声明，无函数体 → C 侧实现，sc 侧生成 extern 原型


#-------------- 可变参数函数（...） --------------------------
# ... 只能在参数列表末尾，且前面至少有一个具名参数
# va_list/va_start/va_end 直接透传 C（stdarg.h 已默认包含）

fnc my_printf: fmt: char&, ...
    var ap: va_list
    ::va_start(ap, fmt)
    ::vprintf(fmt, ap)
    ::va_end(ap)

#-------------- 主函数 ---------------------------------------

fnc main: i4

    # 直接函数实现
    ::printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10))

    # 多行参数函数实现
    var r: rect
    r.lt.x = 0, r.lt.y = 0
    r.rb.x = 10, r.rb.y = 5
    ::printf("area = %d\n", area(&r))

    # 预定义类型实现
    ::printf("add1(3,4) = %d\n", add1(3, 4))
    ::printf("add2(3,4) = %d\n", add2(3, 4))

    #-------------- 函数指针类型 -------------------------------

    # 函数内定义函数指针变量
    var cb: fnc: i4, x: i4
    cb = sq
    ::printf("cb(7) = %d\n", cb(7))

    # 函数指针字段：默认 nil，绑定后调用
    var o: obj
    o.abc = 10
    if o.func1 == nil
        ::printf("func1 is nil\n")
    o.func1 = obj_add
    ::printf("o.func1(2,3) = %d\n", o.func1(&o, 2, 3))
    ::printf("po->func1(4,5) = %d\n", o.func1(&o, 4, 5))

    #-------------- 每对象方法指针（fnc name: 无体）---------------
    # 与 func1（普通函数指针，名字前置、接收者自传）对照：
    # scale 为 fnc 前置、无函数体的「每对象方法指针」，按成员函数约定调用——
    # 接收者隐藏并自动注入：o.scale(3) → scale(&o, 3)；p->scale(3) → scale(p, 3)。
    if o.scale == nil
        ::printf("scale is nil\n")
    o.scale = obj_scale                          # 绑定实现（函数地址直存）
    ::printf("o.scale(3) = %d\n", o.scale(3))      # 隐藏接收者：abc*3 = 30
    var po: obj& = &o
    ::printf("po->scale(4) = %d\n", po->scale(4))  # 指针接收者注入：abc*4 = 40

    # C 实现接口调用（声明在本文件，实现在 C 侧）

    # 可变参数函数
    my_printf("sc says: %s %d\n", "hello", 42)

    #-------------- 伪闭包 ---------------------------------

    #     o.func2 = fnc: i4, a: i4, b: i4
    #         return a + b
    #     ::printf("fnc lit: %d\n", o.func2(3, 4))

    #-------------- 实参默认自动补 0：缺参自动填零 -------------

    ::printf("add3(7) = %d\n", add3(7))            # b,c 补 0 → 7
    ::printf("add3(1,2) = %d\n", add3(1, 2))       # c 补 0 → 3
    ::printf("desc() = %d\n", desc())              # s 补 nil, pt 补 {0} → 0

    # 函数指针变量/字段调用也自动补 0/nil
    ::printf("o.func1(&o) = %d\n", o.func1(&o))    # x,y 补 0
    ::printf("cb() = %d\n", cb())                  # x 补 0


    return 0
