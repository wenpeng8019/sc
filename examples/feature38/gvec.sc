# 特性 38 附属模块：泛型容器模板 + 跨模块导出「实例类型签名」的函数。
#
# 关键点：本模块用 def Vec: <T>, N 定义泛型容器模板，并 mix Vec(i4, int)
#   单态化出具体类型 Vec_int。其后导出的 @fnc 形参直接以实例类型 Vec_int 书写。
#   实例类型 Vec_int 由编译器聚合进工程级 generic.h（跨单元一致、去重），故本模块
#   的导出接口头 scm_gvec.h 引用 Vec_int 时可见其定义——消费单元无需重复定义即可调用。

# 泛型容器：T=元素类型，N=实例名后缀
def Vec: <T>, N
    def Vec_\N: {
        data[8]: T
        len: i4
    }
    fnc Vec_\N\_push: v: Vec_\N&, x: T
        v->data[v->len] = x
        v->len = v->len + 1
    fnc Vec_\N\_at: T&, v: Vec_\N&, i: i4
        return &v->data[i]

# 在本模块内单态化出 Vec_int（其类型定义将进 generic.h，跨模块共享）
mix Vec(i4, int)

# 导出函数：形参类型为泛型实例 Vec_int（跨模块签名）
@fnc vec_push3: v: Vec_int&, a: i4, b: i4, c: i4
    v->len = 0
    Vec_int_push(v, a)
    Vec_int_push(v, b)
    Vec_int_push(v, c)

@fnc vec_sum: i4, v: Vec_int&
    var s: i4 = 0
    var i: i4 = 0
    while i < v->len
        s = s + v->data[i]
        i = i + 1
    return s
