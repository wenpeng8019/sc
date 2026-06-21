# 负向用例：同名自由函数重复定义 → 编译期报错（sc 无前向声明，定义顺序无关）。
@fnc foo: i4
    return 1

@fnc foo: i4
    return 2

@fnc main: i4
    return foo()
