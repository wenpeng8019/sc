# 特性 5：导入与导出
# - 导出（@）
# - 导入（inc）

#-------------- @ 导出标记 -----------------------------------
# @ 标记的类型/变量/函数在 --emit-c -o 时生成同名 .h 头文件

@def Color: [
    Red = 0
    Green
] : i1

@def Point: {
    x: i4
    y: i4
}

@var total: i4 = 0

@fnc add: i4, a: i4, b: i4
    return a + b

#-------------- @ 导出伪类：成员函数 / C 接口一并导出 ----------
# @ 标记的结构体，其成员函数和 :: 接口都会输出到 .h 头文件

@def tracker: {
    val: i4
    init: fnc
        this->val = 0
    add: fnc: k: i4
        this->val = this->val + k
    fnc read:: i4                       # C 侧实现，导出为 extern
}

fnc main: i4

    printf("add = %d\n", add(2, 3))
    total = add(10, 20)
    printf("total = %d\n", total)

    return 0
