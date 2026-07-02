# 特性 11：operand 设备操作数通用指令（platform.h 的 sc 侧）
# > builtins/op.sc 模块：默认导入（与 platform.h 一样，无需 inc）
#
#   - operand 伪结构体声明一组对**基础/任意类型**操作数的通用硬件指令
#   - 语法上为基础类型扩展 . 操作来调用：v.get() / p->set(x)
#   - 接收者无同名方法时，. 操作透传为 platform.h 的同名 sc_<op> 宏
#   - 接收者一律以指针传入：值接收者 v.op() 自动取址 &v；指针接收者 p->op() 原样
#   - 指令类型无关，忽略入参/返回值类型；当前提供原子读写 get/set/get_acq/set_rel

# 任意类型操作数：值接收者 . → 透传 sc_<op>(&v, ...)
fnc demo_scalar:
    var x: i4 = 0
    x.set(42)                          # → sc_set(&x, 42)     原子写
    var y: i4 = x.get()                # → sc_get(&x)         原子读
    ::printf("scalar: set(42) get()=%d\n", y)

# 指针接收者 -> → 透传 sc_<op>(p, ...)，并演示 acquire/release 序
fnc bump: p: i4&
    var cur: i4 = p->get_acq()         # → sc_get_acq(p)      acquire 读
    p->set_rel(cur + 1)                # → sc_set_rel(p, ...) release 写

fnc main: i4
    demo_scalar()

    var n: i4 = 10
    bump(&n)
    bump(&n)
    ::printf("pointer: bump x2 -> %d\n", n.get())

    # 其它类型同样适配（指令类型无关）：f8 操作数
    var f: f8 = 1.5
    f.set(3.25)
    ::printf("f8: set(3.25) get()=%.2f\n", f.get())
    return 0
