# add .sc 单元测试：验证顶级 `add <file>.sc` 的内联/拼接语义。
#   本质：两个 .sc 文件的拼接，被 add 的文件为前置，其默认（未导出/静态）
#   函数与类型对本容器单元可见。
# 运行：scc tests/cases/add_sc_test.sc --test

add add_sc_helper.sc

tst "add .sc：容器可见被 add 子单元的私有函数"
    assert h_add(20, 22) == 42, "h_add"
    assert h_add(-5, 5) == 0, "h_add zero"

tst "add .sc：容器可见被 add 子单元的私有类型"
    var v: Vec2
    v.x = -3
    v.y = 4
    assert h_manhattan(v) == 7, "h_manhattan(-3,4)"
    var o: Vec2
    o.x = 0
    o.y = 0
    assert h_manhattan(o) == 0, "h_manhattan origin"
