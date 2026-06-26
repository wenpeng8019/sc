# 特性 46 附属：日志单例模块。
#   @mod logger —— 导出的单例对象。emit 直接引用兄弟模块 config 的导出成员函数
#   config.level()（无需 inc feature46_config.sc：与 @@ 根配合，兄弟模块默认互相可见）。

@mod logger:
    n: i4

    fnc init:                       # 自动构造：计数清零
        this->n = 0
        return

    @fnc emit: msg: char&
        # 兄弟模块互相可见：直接读取 config 单例的导出等级
        printf("[L%d] %s\n", config.level(), msg)
        this->n = this->n + 1
        return

    @fnc count: i4
        return this->n
