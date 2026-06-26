# 特性 46 附属：配置单例模块。
#   @mod config —— 导出的单例对象（@mod → 类型/实例 extern，跨模块可见），自动构造
#   （init 设默认等级）。导出 set_level / level 供其它兄弟模块与根读写；内部 clamp 为
#   私有成员函数（未 @ 导出 → static，不可跨模块）。

@mod config:
    lvl: i4

    fnc init:                       # 自动构造：默认日志等级 1
        this->lvl = 1
        return

    @fnc set_level: n: i4
        this->lvl = this->clamp(n)  # 调用私有成员函数
        return

    @fnc level: i4
        return this->lvl

    fnc clamp: i4, n: i4            # 私有：限定 0..5（未导出）
        if n < 0
            return 0
        if n > 5
            return 5
        return n
