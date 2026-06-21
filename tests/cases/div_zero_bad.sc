# 负向用例：除数为整数字面量 0 → 编译期报「除数为零」（C 中为未定义行为）。
@fnc main: i4
    var a: i4 = 10
    return a / 0
