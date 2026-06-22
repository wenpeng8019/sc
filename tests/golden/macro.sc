# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def TAG: = 7

def decl_pair: pfx
    var pfx\_lo: i4 = 0
    var pfx\_hi: i4 = 1

def show: x
    printf("%s = %d\n", `x`, x)

def sumprint: fmt, ...
    printf(fmt, __VA_ARGS__)

mix decl_pair(g)

fnc main: i4
    var count: i4 = TAG
    mix show(count)
    mix sumprint("sum=%d\n", count)
    return count
