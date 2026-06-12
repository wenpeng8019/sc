# 特性 5：rpc 伪形参函数 —— 参数/返回值展开为同名结构体的语法糖
# 形式与 fnc 一致；转 C 展开为三件套：
#   struct name { ret _; ...params }   同名参数结构体（返回槽 _ 为首成员）
#   void name_rpc(struct name *_p)     实际函数（仅声明形态由 C 侧实现）
#   static inline ret name(...)        调用包装（装填 → 执行 → 取返回槽）
# 调用形式与普通函数完全一致；参数天然可打包、可转发（消息派发/RPC 场景）
inc stdio.h

# 定义形态：函数体内参数引用自动改写为 _p->x，return e 改写为 _p->_ = e
rpc add: i4, a: i4, b: i4
    return a + b

# 无返回值（v）：结构体不含返回槽 _
rpc greet: v, n: i4
    printf("hello rpc x%d\n", n)

# 指针参数（rpc 参数不支持数组，用 & 指针代替）
rpc strlen2: i4, s&: u1
    var n: i4 = 0
    while s[n] != 0
        n++
    return n * 2

# @导出：头文件中包含完整三件套，跨模块/纯 C 可直接调用
@rpc square: i4, x: i4
    return x * x

fnc main: i4
    printf("add(3,4) = %d\n", add(3, 4))
    greet(2)
    printf("strlen2 = %d\n", strlen2("abc"))
    printf("square(9) = %d\n", square(9))
    return 0
