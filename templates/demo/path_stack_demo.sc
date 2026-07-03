# path_stack_demo —— 路径段栈示例（templates/utils/path.sc 的 path_stack）
#
# path_stack 移植自 c_prototype 的 C_pth_*（C_stk/proto 的路径应用）：把路径当作「段的栈」，
#   以 proto（FILO 纪律）为底座——push 下钻追加、up/ascend 上溯（等价 ".."）、build 按压入序
#   以 '/' 拼接成路径串。段用 proto.feed 存原始字节（不经 strlen，允许任意段内容、无长度上限）。
#
# 运行：scc templates/demo/path_stack_demo.sc
#   期望依次输出 5 组场景（绝对路径下钻/上溯、相对路径 "." 忽略、首段 '/' 自动判定绝对、
#   空栈边界 "/" 与 "."、build_to 写入用户缓冲并测长）。
#
# 用法要点：
#   var ps: path_stack
#   ps.init(true)                 # true=绝对路径栈（build 前缀 '/'），false=相对
#   ps.push("usr/local/bin")      # 逐段下钻（内部按 '/' 拆分；"." 忽略，".." 上溯不越根）
#   ps.up() / ps.ascend(n)        # 上溯一段 / n 段（back 为 sc 保留字，故名 ascend）
#   var s: char& = ps.build()     # 拼接成路径串（mem 分配，用完 recycle）
#   ps.build_to(&buf[0], size)    # 或直接写入用户缓冲；buffer 为 nil 时仅测长
#   ps.drop()                     # 释放底座 proto

inc ../utils/path.sc
inc mem.sc

# 打印一个路径栈：标签 + 段数 + 拼接结果。
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

    # C：首段以 '/' 起自动判定绝对；ascend 多段
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
