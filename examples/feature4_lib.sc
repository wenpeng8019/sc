# 特性 4 附属模块：演示「其他模块的静态全局对象」的生命周期注入。
#
# 本模块的静态全局对象 g_audit 由编译器自动生成的模块级生命周期函数托管：
#   sc_mod_feature4_lib_init —— 入口 main 序言调用，构造 g_audit（打印 [lib.init]）
#   sc_mod_feature4_lib_drop —— 入口 main 尾声调用，析构 g_audit（打印 [lib.drop]）
# 入口（feature4.sc）只需 inc 本模块，无需手动初始化/析构其全局状态。

inc stdio.h

@def audit: {
    seq: i4
    init: fnc                              # 构造：被 sc_mod_feature4_lib_init 注入
        this->seq = 0
        printf("[lib.init] audit ready\n")
    note: fnc                              # 记录一次（递增计数）
        this->seq++
        printf("[lib.note] #%d\n", this->seq)
    drop: fnc                              # 析构：被 sc_mod_feature4_lib_drop 注入
        printf("[lib.drop] total=%d\n", this->seq)
}

# 本模块的静态全局对象 g_audit 归属本模块（@ 导出仅为让入口可见其类型，
# 实体仍由本模块持有）：其构造/析构由 sc_mod_feature4_lib_init/drop 托管，
# 入口无需手动初始化/析构。
@var g_audit: audit

# 导出函数：供入口调用，间接驱动模块全局状态。
@fnc lib_audit
    g_audit.note()
