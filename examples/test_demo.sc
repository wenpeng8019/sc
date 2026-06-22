inc stdio.h

fnc add: i4, a: i4, b: i4
    return a + b

fnc mul: i4, a: i4, b: i4
    return a * b

tst "加法"
    assert add(2, 3) == 5
    assert add(-1, 1) == 0, "负数相加归零"

tst "乘法"
    assert mul(3, 4) == 12
    assert mul(0, 9) == 0

tst "故意失败"
    assert add(2, 2) == 5, "这里应当失败"

tst.skip "暂时跳过"
    assert mul(2, 2) == 5
