# 负向用例：把指针赋给标量 → 报「类型不匹配」并显示具体类型。
@fnc main: i4
    var p: i4&
    var x: i4 = p
    return x
