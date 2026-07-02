# 特性 15：分身/切片实体的 alloc/free 用「每对象方法指针」（MethodPtr）
#
# 对比 feature12：那里 buffer 的 alloc/free 是类方法（带函数体、所有对象共享实现）。
# 这里 dev 的 alloc/free 是 MethodPtr 字段（fnc 前置、无函数体）——每个对象各自持有，
# 运行时绑定。用于"设备相关"的分身构造（如 com 通讯：不同设备各有 io 实现，伪类无派生
# 时以每对象方法指针充当接口）。分身赋值语法糖（s = 本体 / s = nil）对两种形态一致。

#-------------- 分身/切片类型 S ----------------------------------
def view: {
    p: char&
    n: i4
}

#-------------- 实体类型 T：alloc/free 为每对象方法指针字段 --------
def dev: <view> {
    data[64]: char
    fnc alloc: view&, off: i4, len: i4    # 每对象分身构造器（隐藏接收者 dev&，返回 view&）
    fnc free: v: view&                    # 每对象分身析构器
}

# 实现：接收者显式为首参 _this: dev&（与普通函数指针赋值对齐）
fnc dev_alloc: view&, _this: dev&, off: i4, len: i4
    var v: view& = ::malloc(sizeof(view)): view&
    v->p = &_this->data[off]
    v->n = len
    return v

fnc dev_free: _this: dev&, v: view&
    ::free(v)

fnc main: i4
    var d: dev
    var i: i4
    for i = 0; i < 26; i++
        d.data[i] = 'a' + i

    # 运行时绑定每对象 io（不同设备可绑不同实现）
    d.alloc = dev_alloc
    d.free = dev_free

    #-------------- 句柄：切片参数 off=2, len=5 -----------------
    var s: dev[2, 5]

    #-------------- 绑定：s = 本体 → 经 d.alloc 字段构造 + 回写 _self
    s = d
    ::printf("切片:")
    var j: i4
    for j = 0; j < s._->n; j++
        ::printf(" %c", s._->p[j])
    ::printf("\n")                          # c d e f g

    #-------------- 解绑：s = nil → 经 _self->free 字段析构 + 置空
    s = nil
    ::printf("已解绑: %d\n", s._ == nil)     # 1
    return 0
