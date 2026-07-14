# 消费单元（负向）：经 @@ 注入使用根类型 cfg，但在 @导出签名里【按值】引用 —— 触发守卫。
#   正确写法是指针形态 'cfg&'（见 tests/cases/rp_ptr）。
@fnc rp_take_x: i4, c: cfg
    return c.x
