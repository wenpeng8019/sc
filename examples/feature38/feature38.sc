# 特性 38：跨模块泛型实例化（cross-module generics）
#
# 泛型模板 def Name: <T,...>, N 经 mix 单态化为具体类型/函数。本例演示：
#   - 模板在附属模块 gvec.sc 中定义并实例化（mix Vec(i4, int) → Vec_int）；
#   - 入口模块同样 mix Vec(i4, int)，与附属模块得到「同名同形」的 Vec_int；
#   - 编译器把所有单元的泛型实例聚合进工程级 generic.h（按类型名去重、跨单元一致），
#     使附属模块导出的 vec_push3/vec_sum（形参为 Vec_int）在入口可直接调用，
#     入口自身也能声明 var vi: Vec_int —— 同一类型，跨模块完全一致。
#
# 机制：自包含实例（字段仅基本类型/指针）其完整定义进 generic.h；非自包含实例
#   （按值内嵌用户聚合）仅各单元内联，generic.h 给前向 typedef（指针签名仍可跨模块）。

inc gvec.sc                          # 附属模块：泛型模板 + 导出实例签名函数

mix Vec(i4, int)                     # 入口侧也实例化 Vec_int（与 gvec.sc 同型，generic.h 去重）

fnc main: i4
    var vi: Vec_int
    vec_push3(&vi, 10, 20, 30)       # 调用附属模块导出函数（形参 Vec_int&）
    ::printf("sum=%d len=%d\n", vec_sum(&vi), vi.len)
    var p: i4& = Vec_int_at(&vi, 1)  # 入口侧直接用模板生成的成员函数
    ::printf("at1=%d\n", *p)
    return 0
