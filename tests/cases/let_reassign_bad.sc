# 负向用例：对 let 声明的不可变绑定再赋值 → 编译期报错。
@fnc main: i4
    let n: i4 = 5
    n = 6
    return n
