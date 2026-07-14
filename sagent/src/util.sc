# util —— sagent 基础工具（最底层，被 sagent.sc 首个 add；不得引用其它 src/ 单元）

# C 风格 strlen（sc FFI 惯例：不依赖 libc 头解析）。
fnc sa_slen: u4, s: const char&
    var i: u4 = 0
    while s[i] != 0
        i = i + 1
    return i

# 字符串等值比较（数值比较惯例，print 坑规避）。
fnc sa_streq: bool, a: const char&, b: const char&
    var i: u4 = 0
    while a[i] != 0 && b[i] != 0
        if a[i] != b[i]
            return false
        i = i + 1
    return a[i] == b[i]
