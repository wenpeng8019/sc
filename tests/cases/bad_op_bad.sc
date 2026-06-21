# 负向用例：两个指针相加 → 报非法二元运算（缺失操作符/类型不匹配）。
@fnc main: i4
    var p: i4&
    var q: i4&
    var x: i4 = p + q
    return x
