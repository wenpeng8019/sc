# 特性 27：字面量后缀扩展 + ret 调用语法糖
#
# 一、字面量后缀（限制为 C 标准后缀，并扩展 b/w）
#   整数：u/U（无符号）可与 l/L、ll/LL 组合；扩展 b/B（单字节）、w/W（双字节）
#     - 5b   → i1（单字节有符号）         5ub  → u1
#     - 300w → i2（双字节有符号）         300uw→ u2
#     - 5u   → u4    9000000000l → i8     10ul → u8
#   浮点：f/F（float）或 l/L（long double）—— 3.14f → f4
#   非法组合（如 5uu、5bw）在词法阶段报错。
#
# 二、ret 调用语法糖（针对返回 ret 的函数调用）
#   首次使用时自动创建函数级 ret 变量 $ 保存返回码，之后复用。
#   操作符与函数名之间必须有空格（与普通取反表达式消歧）：
#     ! func()   \n body   等价 if (($ = func()) != ok)  { body }   （失败 != ok 时进入）
#     > func()   \n body   等价 if (($ = func()) >  0)   { body }
#     < func()   \n body   等价 if (($ = func()) <  0)   { body }   （< <= >= 类推）
#     !! func()            等价 $ = func(); if ($ != ok) assert(false)
#   错误传播糖 ? 后缀（仅 ! 形态）：
#     ! func() ?  \n body  等价 if (($ = func()) != ok) { body; return $ }   （失败：处理后上报）
#     ! func() ?           （无体）失败即 return $ 上报
#   $ 在生成的 C 中映射为合法标识符 _sc_ret（$ 非 C99 标准标识符）。

#-------------- 字面量后缀 --------------
fnc show_suffix
    var a: = 5b          # i1
    var b: = 300w        # i2
    var c: = 7ub         # u1
    var d: = 100uw       # u2
    var e: = 5u          # u4
    var f: = 9000000000l # i8
    printf("suffix: %d %d %u %u %u %lld\n", a, b, c, d, e, f)

#-------------- ret 调用语法糖 --------------
# 返回 ret：约定 ok(0)=成功，>0 表示告警码，<0 表示错误码
fnc classify: ret, n: i4
    if n < 0
        return -1
    if n == 0
        return 0
    return 1

fnc demo_sugar
    # ! → 失败（非 ok）时进入；classify(-2) 返回 -1 → 进块
    ! classify(-2)
        printf("fail branch, $=%d\n", $)
    # ! → classify(0) 返回 ok=0 → 不进块
    ! classify(0)
        printf("never here\n")
    # > → 返回值 > 0 时进入
    > classify(7)
        printf("warn branch, $=%d\n", $)
    # < → 返回值 < 0 时进入
    < classify(-2)
        printf("err branch, $=%d\n", $)
    # !! → 失败（非 ok）即 assert(false) 中止；此处成功，继续
    !! classify(0)
    printf("after assert, $=%d\n", $)

# 错误传播糖 ?：失败时打印并向上层 return $
fnc check_pos: ret, n: i4
    if n < 0
        return -1            # 错误码
    return ok                # 成功

fnc do_step: ret, n: i4
    ! check_pos(n) ?
        printf("do_step: check_pos(%d) failed, $=%d, propagate up\n", n, $)
    printf("do_step: ok n=%d\n", n)
    return ok

fnc run_pipeline: ret
    ! do_step(5) ?           # 成功，继续
    ! do_step(-1) ?          # 失败，打印并 return $
    printf("run_pipeline: never reached\n")
    return ok

fnc main: i4
    show_suffix()
    demo_sugar()
    var r: ret = run_pipeline()
    printf("pipeline result = %d\n", r)
    return 0
