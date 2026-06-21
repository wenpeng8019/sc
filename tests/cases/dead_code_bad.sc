# 负向用例：return 之后存在不可达语句 → 编译期报死代码。
@fnc main: i4
    return 0
    var x: i4 = 5
