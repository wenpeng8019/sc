# 特性 3：b 布尔类型 / true,false,nil 常量 / 函数指针字段
inc stdio.h

# 函数指针字段：无函数体的函数签名字段（默认 nil，可赋值后调用）
def obj: {
    abc: i4
    func: fnc: i4, o&: obj, x:i4, y:i4
}

# 被指向的普通函数：接收者显式传入
fnc obj_add: i4, o&: obj, x:i4, y:i4
    return o->abc + x + y

fnc main: i4
    # b 布尔类型与 true/false 常量
    var ok: bool = true
    var no: bool = false
    printf("ok=%d no=%d\n", ok, no)

    # nil 常量
    var p&: i4 = nil
    if p == nil
        printf("p is nil\n")

    # 函数指针字段：默认 nil，绑定后通过 obj.func() 调用
    var o: obj
    o.abc = 10
    if o.func == nil
        printf("func is nil\n")
    o.func = obj_add
    printf("o.func(2,3) = %d\n", o.func(&o, 2, 3))

    # 指针接收者：ptr->func() 直接传指针
    var po&: obj = &o
    printf("po->func(4,5) = %d\n", po->func(po, 4, 5))
    return 0
