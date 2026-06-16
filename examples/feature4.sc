# 特性 5：伪类
# + 这是一个语法糖层面特性，底层仍是结构体 + 函数指针的组合，本质是在语言层面约定了一套调用规范
#
# - 成员函数的定义
# - 调用糖
# - 构造函数（init）与栈构造/堆构造
# - 析构（drop）
# - 由 C 实现的方法（成员函数）接口（::）
# - @ 导出：成员函数与 C 实现接口一并导出到头文件（见特性 4）

inc stdio.h
inc stdlib.h

#-------------- 结构体 + 成员函数 = 伪类 ----------------------

def point: {
    x: i4
    y: i4
    init: fnc                         # 构造：声明时自动调用
        this->x = 1
        this->y = 2
    sum: fnc: i4, dx: i4, dy: i4      # 带参数/返回值的方法
        return this->x + this->y + dx + dy
    drop: fnc                         # 析构：手动调用 x.drop()
        printf("point(%d,%d) dropped\n", this->x, this->y)
    op: fnc: i4, p&: point, k: i4     # 无函数体 → 普通函数指针字段
}

# C 实现成员函数接口：fnc name:: 仅声明，实现在 C 侧
def obj: {
    id: i4
    fnc dump::                           # C 侧实现 obj_dump(obj *_this)
    fnc calc:: i4, a: i4, b: i4          # C 侧实现 obj_calc(obj *_this, a, b)
}

fnc main: i4

    #-------------- 声明即构造 + 方法调用糖 + 析构 -------------------

    var pt: point
    printf("init: x=%d y=%d\n", pt.x, pt.y)             # var 自动调用 init

    # 值接收者 o.m(...)
    printf("pt.sum(3,4) = %d\n", pt.sum(3, 4))

    # 指针接收者 p->m(...)
    var pp&: point = &pt
    printf("pp->sum(3,4) = %d\n", pp->sum(3, 4))

    # 无 body 的函数指针字段：默认 nil
    var p2: point
    printf("op is nil (before bind): %d\n", p2.op == nil)

    # 析构：手动调用 drop
    p2.drop()

    #-------------- 堆构造 -------------------

    # 堆构造：T() 伪调用 → malloc + 默认值/清零 + init
    var hp&: point = point()
    printf("heap: sum() = %d\n", hp->sum())
    hp->drop()
    free(hp)

    #-------------- C 实现成员函数接口 ------------------------
    # # obj.dump / obj.calc 的实现在 C 侧，sc 中只做声明和调用

    # var o: obj
    # o.id = 1
    # o.dump()                                # → obj_dump(&o)
    # var r: i4 = o.calc(2, 3)               # → obj_calc(&o, 2, 3)
    # printf("calc = %d\n", r)

    return 0
