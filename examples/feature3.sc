# 特性 3：异步函数 —— rpc 
# + 这是一个伪形参函数，本质是参数/返回值展开为同名结构体的语法糖
# 
# 形式与 fnc 一致；转 C 展开为三件套：
#   struct name { ret _; ...params }   同名参数结构体（返回槽 _ 为首成员）
#   void name_rpc(struct name *_p)     实际函数（仅声明形态由 C 侧实现）
#   static inline ret name(...)        调用包装（装填 → 执行 → 取返回槽）
# rpc 是「流程」原语，须经驱动调用（禁止裸 rpc()）：当前线程直接执行用
# `sync work(args)`（返回结果，等价旧的裸调用）；另有 async（事件循环）、
# run（独立线程/池）、队列 <<（投递）等形态。参数天然可打包、可转发。

# 定义形态：函数体内参数引用自动改写为 _p->x，return e 改写为 _p->_ = e
rpc add: i4, a: i4, b: i4
    return a + b

# 无返回值（v）：结构体不含返回槽 _
rpc greet: n: i4
    ::printf("hello rpc x%d\n", n)

# 指针参数：标量地址用 & 指针
rpc strlen2: i4, s: char&
    var n: i4 = 0
    while s[n] != 0
        n++
    return n * 2

# 数组参数：映射为结构体内 <T*, size> 两字段（指针 + 字节数）。
# 装填时自动传数组地址与其 sizeof，便于 C 传输层据 size 序列化数组。
# 体内按声明维度正常索引（多维亦可）。
rpc dot: i4, a[3]: i4, b[3]: i4
    var s: i4 = 0
    var i: i4 = 0
    for i = 0; i < 3; i++
        s = s + a[i] * b[i]
    return s

# @导出：头文件中包含完整三件套，跨模块/纯 C 可直接调用
@rpc square: i4, x: i4
    return x * x

fnc main: i4
    ::printf("add(3,4) = %d\n", sync add(3, 4))
    sync greet(2)
    ::printf("strlen2 = %d\n", sync strlen2("abc"))
    var u[3]: i4 = [1, 2, 3]
    var v[3]: i4 = [4, 5, 6]
    ::printf("dot = %d\n", sync dot(u, v))
    ::printf("square(9) = %d\n", sync square(9))
    return 0
