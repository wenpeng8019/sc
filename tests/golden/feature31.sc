# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def CAP: = 4

def dump: x
    printf("  %s = %d\n", `x`, x)

def tally: tag
    var tag\_n: i4 = 0
    tag\_n = (tag\_n + CAP)
    printf("  %s_n = %d\n", `tag`, tag\_n)

def logf: fmt, ...
    printf(fmt, __VA_ARGS__)

def gpair: pfx
    var pfx\_lo: i4 = 10
    var pfx\_hi: i4 = 20

mix gpair(cfg)

let cfg_lo:: i4

let cfg_hi:: i4

fnc main: i4
    var count: i4 = CAP
    printf("object macro: CAP=%d\n", CAP)
    printf("stringify:\n")
    mix dump(count)
    printf("paste:\n")
    mix tally(item)
    printf("variadic + claimed globals:\n")
    mix logf("  sum=%d range=[%d,%d]\n", count + cfg_lo, cfg_lo, cfg_hi)
    return 0
