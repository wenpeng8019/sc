# path_stack 冒烟测试：验证移植自 c_prototype C_pth_* 的路径段栈（proto 底座）。
# 运行：scc templates/utils/path_stack_test.sc && 生成的可执行直接跑。
inc path.sc
inc mem.sc

@fnc show: label: const char&, ps: path_stack&
    var s: char& = ps->build()
    ::printf("%-22s depth=%llu  -> %s\n", label, ps->depth(), s)
    recycle(s)

@fnc main: i4
    # A：绝对路径 + 多段 + ".." 上溯
    var a: path_stack
    a.init(true)
    a.push("usr/local")
    a.push("bin")
    show("A push usr/local/bin", &a)
    a.push("../lib")                 # 弹出 bin，压 lib
    show("A push ../lib", &a)
    a.up()                            # 上溯 lib
    show("A up()", &a)
    a.drop()

    # B：相对路径 + "." 忽略 + 一次多段 push
    var b: path_stack
    b.init(false)
    b.push("./a/./b/c")              # "." 全忽略
    show("B a/b/c", &b)
    b.push("..")                      # 弹出 c
    show("B ..", &b)
    b.drop()

    # C：首段以 '/' 起自动判定绝对；back 多段
    var c: path_stack
    c.init(false)
    c.push("/opt/app/data/cache")    # 自动绝对
    show("C /opt/app/data/cache", &c)
    c.ascend(2)                       # 弹出 cache, data
    show("C ascend(2)", &c)
    c.drop()

    # D：空栈边界（绝对→"/"，相对→"."）
    var d: path_stack
    d.init(true)
    show("D empty abs", &d)
    d.clear()
    d.init(false)
    show("D empty rel", &d)
    d.drop()

    # E：build_to 到用户缓冲 + 测长
    var e: path_stack
    e.init(true)
    e.push("var/log/sys.log")
    var need: i4 = e.build_to(nil, 0)
    var buf[64]: char
    var wrote: i4 = e.build_to(&buf[0], 64)
    ::printf("E need=%d wrote=%d buf=%s\n", need, wrote, &buf[0])
    e.drop()

    return 0
