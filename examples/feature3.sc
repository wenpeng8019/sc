# 新特性测试：b 布尔类型 / true,false,nil 常量 / 伪 class 方法字段
inc stdio.h

# 伪 class：带方法字段的结构体
def obj: {
    abc: i4
    func: fnc: i4, x:i4, y:i4
}

# 方法实现：首参隐式接收者，函数体内用 this 访问
fnc obj_add: i4, this&: obj, x:i4, y:i4
    return this->abc + x + y

fnc main: i4
    # b 布尔类型与 true/false 常量
    var ok: b = true
    var no: b = false
    printf("ok=%d no=%d\n", ok, no)

    # nil 常量
    var p&: i4 = nil
    if p == nil
        printf("p is nil\n")

    # 伪 class：方法默认 nil，绑定后通过 obj.func() 调用
    var o: obj
    o.abc = 10
    if o.func == nil
        printf("func is nil\n")
    o.func = obj_add
    printf("o.func(2,3) = %d\n", o.func(2, 3))

    # 指针接收者：ptr->func() 直接传指针
    var po&: obj = &o
    printf("po->func(4,5) = %d\n", po->func(4, 5))
    return 0
