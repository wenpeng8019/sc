# 特性 12：分身/切片（def T: <S> {}）
# > 与 ~（链表）、<C, I>（容器）并列的第三套"结构注入"机制：
#   - <S>      ：声明实体 T 的"分身/切片"类型为 S。编译器在 S 首部注入回指
#                成员 _self: T&（synthetic），使分身能反查所属本体 T。
#
# 机制要点：
#   - T 是实体类型（如 buffer）；S 是它的一个"视图/切片"（如 view）。
#   - T 必须提供两个成员函数：
#       alloc: fnc: S&, ...   # 分身构造器：隐式 this=T*，余参=切片参数，返回 S&
#       free:  fnc: S&        # 分身析构器
#   - 句柄类型 T[...]：方括号内是 alloc 的切片实参初值，本质是一个匿名结构体
#       { <alloc 参数...>; _: S& }，_ 指向当前绑定的分身实例（nil=未绑定）。
#   - 赋值语法糖：
#       s = 本体   →  s._ = T_alloc(&本体, 切片参数...); s._->_self = &本体
#       s = nil    →  if (s._) { T_free(s._->_self, s._); s._ = nil; }
#   - self：分身 S 的成员函数内上下文关键字，等价 _this->_self，指向本体实体 T。

#-------------- S：分身/切片类型（实体的一个"视图"）------------
def view: {
    p: char&
    n: i4

    # 分身方法内可用 self 反查本体实体 buffer（self = buffer&）
    capacity: fnc: i4
        return sizeof(self->data)
}

#-------------- T：实体类型，<view> 标记其分身/切片类型 ----------
def buffer: <view> {
    data[256]: char

    # alloc：分身构造器（隐式 this=buffer&，余参=切片参数），返回 view&
    alloc: fnc: view&, off: i4, len: i4
        var v: view& = ::malloc(sizeof(view)): view&
        v->p = &this->data[off]
        v->n = len
        return v

    # free：分身析构器
    free: fnc: v: view&
        ::free(v)
}

fnc main: i4

    var b: buffer
    var i: i4
    for i = 0; i < 26; i++
        b.data[i] = 'a' + i

    #-------------- 句柄：切片参数 off=2, len=5，未绑定（_=nil）----
    var s: buffer[2, 5]

    #-------------- 绑定：s = 本体（生成 alloc + 回写 _self）-------
    s = b

    ::printf("切片:")
    var j: i4
    for j = 0; j < s._->n; j++
        ::printf(" %c", s._->p[j])
    ::printf("\n")                          # c d e f g

    #-------------- self：分身经回指反查本体容量 -----------------
    ::printf("本体容量: %d\n", s._->capacity())   # 256

    #-------------- 解绑：s = nil（生成 free + 置空）-------------
    s = nil
    ::printf("已解绑: %d\n", s._ == nil)     # 1

    return 0
