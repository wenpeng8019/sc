# 运行时守卫触发用例：--check=ptr 空指针解引用（成员访问 p->v）
# 期望：运行期拦截并 abort（致命），stderr 打印「空指针解引用」带源码定位。
# 比对 golden .trap（程序 stderr）。

@def node: { v: i4 }

@fnc main: i4
    var p: node& = nil
    ::printf("%d\n", p->v)        # nil->v：--check=ptr 注入 nil 校验，命中即 abort
    return 0
