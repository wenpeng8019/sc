# util —— sagent 基础工具模块（inc 引用，@ 导出；不得引用其它 src/ 模块）

inc io.sc
inc os.sc

# C 风格 strlen（sc FFI 惯例：不依赖 libc 头解析）。
@fnc sa_slen: u4, s: const char&
    var i: u4 = 0
    while s[i] != 0
        i = i + 1
    return i

# 字符串等值比较（数值比较惯例，print 坑规避）。
@fnc sa_streq: bool, a: const char&, b: const char&
    var i: u4 = 0
    while a[i] != 0 && b[i] != 0
        if a[i] != b[i]
            return false
        i = i + 1
    return a[i] == b[i]

# 写整文件（覆盖创建）。返回 0 成功 / <0 失败。
@fnc sa_write_file: i4, path: const char&, text: const char&
    var c: com& = file(path, true, 0, 1)
    if c == nil
        return -1
    var n: u4 = sa_slen(text)
    if n > 0
        var wr: i4 = c->write((text: &), &n)
        if wr < 0
            c->close()
            return -2
    c->close()
    return 0

# 读文件全文（malloc，调用方 ::free）。失败返回 nil。
@fnc sa_read_file: char&, path: const char&
    if !fs_is_file(path)
        return nil
    var sz: i8 = fs_size(path)
    if sz < 0
        return nil
    var buf: char& = (::malloc((sz + 1: u8)): char&)
    if buf == nil
        return nil
    var c: com& = file(path, true, 1, 0)
    if c == nil
        ::free((buf: &))
        return nil
    var n: u4 = (sz: u4)
    if n > 0
        var rd: i4 = c->read((buf: &), &n)
        if rd < 0
            c->close()
            ::free((buf: &))
            return nil
    buf[n] = 0
    c->close()
    return buf
