# 运行时守卫触发用例：--check=ref 悬挂引用（借用比目标活得久）
# 期望：内层栈对象 local 退域时仍被自动指针 p 借用（in>0），退域断言检出悬挂。
# 非致命（报告 stderr，不 abort）。比对 golden .trap（程序 stderr）。

@def node: { v: i4 }

@fnc main: i4
    var p: node@ = nil
    if true
        var local: node        # 内层栈对象
        local.v = 7
        p = &local             # p 借用 local（--check=ref 下 local.in++）
    return 0                   # local 退域时 in>0 → 悬挂报告
