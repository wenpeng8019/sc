# path —— 跨平台路径字符串工具（纯 sc）
#
# 定位：templates 通用 utils 组件，**不是语言基础模块**。把「路径字符串的纯词法处理」
#   （根长度 / 父目录 / 目录部分 / 全路径拼接）从 C 参考库（stdc 的 P_root/P_base/
#   P_dir/P_file）移植为纯 sc 实现，摘除对 stdc 的依赖。
#
# 语义与 stdc 的 POSIX 分支对齐（分隔符 '/'，绝对路径以 '/' 开头）：sc 源码平台无关，
#   不做 Windows 盘符 / UNC / 反斜杠分支（那属于下沉到 C 头的平台细节，见 syntax.md §9）。
#
# 依赖：
#   · os.sc    —— path_dir 需 fs_exists / fs_is_dir 判定「路径实体存在且不是目录（即文件）」，
#                 据此决定是否回退到父目录（与 stdc `stat()==0 && !S_ISDIR` 同口径）。
#   · sys.sc   —— path_file 在 base 为空时用 sys_work_dir 取当前工作目录（同 stdc P_work_dir）。
#   · proto.sc —— path_stack 段栈的底座（移植自 c_prototype C_pth_* 的 C_stk/proto 应用）。
#   · mem.sc   —— path_stack.build 结果缓冲的分配（chunk / recycle）。
#
# 用法：inc path.sc，然后：
#   var buf[256]: char
#   path_dir(&buf[0], 256, "/a/b/c.txt")       # → "/a/b"（若 c.txt 是文件）
#   path_file(&buf[0], 256, "/a/b", "../x.ini") # → "/a/x.ini"
#   var ps: path_stack; ps.init(true)          # 路径段栈（见文件末 path_stack）
#
# 与 stdc 的差异（移植时修正的正确性问题）：
#   · P_dir 对空路径 "" 会越界读 buffer[-1]（len==0 时 buffer[len-1] 越界）——本实现加空路径守卫。

inc os.sc     # fs_exists / fs_is_dir
inc sys.sc    # sys_work_dir
# @inc（而非 inc）：使 proto.h 随生成的 path 头一并导出，令消费单元看到 path_stack
#   内嵌的 sc_proto 字段类型（否则消费单元的 .c 报 unknown type name 'sc_proto'）。
@inc proto.sc # path_stack 段栈底座（FILO 记录序列）
inc mem.sc    # path_stack.build 结果缓冲的 chunk / recycle

# ---------------- path_root ----------------
# 根目录长度：POSIX 绝对路径（以 '/' 开头）返回 1，相对路径返回 0（空串亦 0）。
@fnc path_root: u4, path: const char&
    if path[0] == '/'
        return 1
    return 0

# ---------------- path_base ----------------
# 返回 path 的父目录（纯字符串，不检测实体是否存在）：
#   · path 是文件 → 其所在目录；path 是目录 → 上级目录。
# 结果去除末尾分隔符（保留根）；相对路径回退到空则返回 "."。
# 返回：成功返回根长度（root_len ≥ 0），buffer 太小返回 -1。
@fnc path_base: i4, buffer: char&, size: u4, path: const char&
    var len: u4 = (::strlen(path): u4)
    if len >= size
        return -1
    ::memcpy((buffer: &), (path: const &), len + 1)      # 含 NUL
    var root: u4 = path_root(buffer)
    # 去末尾分隔符（保留根）
    while len > root && buffer[len - 1] == '/'
        len = len - 1
    # 回退到上一个分隔符
    while len > root && buffer[len - 1] != '/'
        len = len - 1
    # 去末尾分隔符（保留根）
    while len > root && buffer[len - 1] == '/'
        len = len - 1
    if len == 0
        buffer[0] = '.'
        len = 1
    buffer[len] = 0
    return (root: i4)

# ---------------- path_dir ----------------
# 返回 path 的目录部分：
#   · 以分隔符结尾 → 视为目录，返回自身（去尾分隔符）。
#   · 否则检测实体：是目录 → 返回自身；是文件（存在且非目录）→ 返回所在目录。
#     不存在的路径按目录看待（与 stdc 一致）。
# 返回：成功返回根长度，buffer 太小返回 -1。
@fnc path_dir: i4, buffer: char&, size: u4, path: const char&
    var len: u4 = (::strlen(path): u4)
    if len >= size
        return -1
    ::memcpy((buffer: &), (path: const &), len + 1)
    var root: u4 = path_root(buffer)
    # 空路径守卫（stdc 原实现此处会越界读 buffer[-1]）→ 相对当前目录 "."
    if len == 0
        buffer[0] = '.'
        buffer[1] = 0
        return (root: i4)
    # 不以分隔符结尾：实体存在且不是目录（即文件）→ 回退到父目录
    if buffer[len - 1] != '/'
        if fs_exists(buffer) && !fs_is_dir(buffer)
            while len > root && buffer[len - 1] != '/'
                len = len - 1
    # 去末尾分隔符（保留根）
    while len > root && buffer[len - 1] == '/'
        len = len - 1
    if len == 0
        buffer[0] = '.'
        len = 1
    buffer[len] = 0
    return (root: i4)

# ---------------- path_file ----------------
# 由 base 与 path 构造全路径，写入 buffer：
#   · path 为绝对路径 → 直接采用 path。
#   · base 为空 → 用当前工作目录（sys_work_dir）。
#   · base 非空 → 取其目录部分（path_dir）为前缀。
#   · path 中的 "./" 忽略、"../" 回退上一层（不越过根）。
#   · 支持 file:// URI 前缀输入（POSIX：file:///x → /x），输出为普通文件系统路径。
# 返回：成功 true，buffer 太小或取 cwd 失败返回 false。
@fnc path_file: bool, buffer: char&, size: u4, base: const char&, path: const char&
    # 处理 path 的 file:// 前缀（POSIX：file:///x → /x）
    var pi: u4 = 0
    if ::strncmp(path, "file://", 7) == 0
        pi = 7
    # 处理 base 的 file:// 前缀
    var bi: u4 = 0
    if base != nil && ::strncmp(base, "file://", 7) == 0
        bi = 7

    # path 为绝对路径：直接采用
    if path[pi] == '/'
        var plen: u4 = (::strlen(&path[pi]): u4)
        if plen >= size
            return false
        ::memcpy((buffer: &), (&path[pi]: const &), plen + 1)
        return true

    # 取 base 目录到 buffer
    var root: i4 = 0
    if base == nil || base[bi] == 0
        # base 为空 → 当前工作目录
        if sys_work_dir(buffer, size) != 0
            return false
        root = (path_root(buffer): i4)
    else
        root = path_dir(buffer, size, &base[bi])
        if root < 0
            return false
    var len: u4 = (::strlen(buffer): u4)

    # 逐段拼接 path（处理 ./ 与 ../）
    var i: u4 = pi
    while path[i] != 0
        # 跳过分隔符
        while path[i] == '/'
            i = i + 1
        if path[i] == 0
            break
        # 段边界
        var seg: u4 = i
        while path[i] != 0 && path[i] != '/'
            i = i + 1
        var seglen: u4 = i - seg
        if seglen == 2 && path[seg] == '.' && path[seg + 1] == '.'
            # ".." 回退上一层（不越过根）
            while (len: i4) > root && buffer[len - 1] != '/'
                len = len - 1
            while (len: i4) > root && buffer[len - 1] == '/'
                len = len - 1
            buffer[len] = 0
        else if !(seglen == 1 && path[seg] == '.')
            # 普通段：拼接（前面有内容则先补分隔符）
            if len > 0
                if len + 1 + seglen >= size
                    return false
                buffer[len] = '/'
                len = len + 1
            else if seglen >= size
                return false
            ::memcpy((&buffer[len]: &), (&path[seg]: const &), seglen)
            len = len + seglen
            buffer[len] = 0
    return true

# ---------------- path_stack ----------------
# 路径段栈：移植自 C 参考库 c_prototype 的 C_pth_*（C_stk / 现 proto 的路径应用）。
#   机制——把路径当作「段的栈」：push 下钻（追加一段）、up/back 上溯（弹出末段，即 ".."）、
#   build 按压入序以 '/' 拼接成路径串。底座直接复用 proto（FILO 纪律：push 压顶、back 弹顶；
#   而 proto 的 build/each 恒按插入序遍历——正好等于路径从根到叶的自然顺序）。
#
# 相比 C_pth 的实例化差异：proto 未保留 TLS 隐式默认栈，故 path_stack 为显式对象（更清晰、
#   可并存多个）。段用 proto.feed 存原始字节（不经 strlen，允许任意段内容、无长度上限）。
#
# 用法：
#   var ps: path_stack
#   ps.init(true)                       # 绝对路径（build 前缀 '/'）
#   ps.push("usr/local")                # 逐段下钻（内部按 '/' 拆分，. 忽略、.. 回退）
#   ps.push("bin")
#   ps.push("../lib")                   # ".." 弹出 bin，再压 lib
#   var s: char& = ps.build()           # → "/usr/local/lib"（mem 分配，用完 recycle）
#   recycle(s)
#   ps.drop()
@def path_stack: {
    seg: proto          # 段 FILO 栈（feed 原始字节；push 压顶 / back 弹顶 / build 插入序拼接）
    abs: bool           # 绝对路径（build 前缀 '/'；由首段是否以 '/' 起判定，或 init 显式指定）

    # 初始化：is_abs 指定是否绝对路径栈。
    init: fnc: is_abs: bool
        this->seg.init(PROTO_FILO, 0, 4)
        this->abs = is_abs

    # 释放底座 proto。
    drop: fnc
        this->seg.drop()

    # 清空全部段（保留缓存复用），abs 标志不变。
    clear: fnc
        this->seg.clear()

    # 当前段数。
    depth: fnc: u8
        return this->seg.depth()

    # 是否无段。
    is_empty: fnc: bool
        return this->seg.is_empty()

    # 压入一段或多段（path 内含 '/' 则拆分逐段处理）：
    #   "." 忽略；".." 弹出末段（栈空则丢弃，不越根）；其余按原始字节压入。
    #   首段以 '/' 起且当前为空 → 自动标记为绝对路径。返回 false 表示底座分配失败。
    push: fnc: bool, path: const char&
        if path == nil
            return true
        if path[0] == '/' && this->seg.is_empty()
            this->abs = true
        var i: u4 = 0
        while path[i] != 0
            while path[i] == '/'
                i = i + 1
            if path[i] == 0
                break
            var s: u4 = i
            while path[i] != 0 && path[i] != '/'
                i = i + 1
            var slen: u4 = i - s
            if slen == 1 && path[s] == '.'
                continue                                 # "." 忽略
            if slen == 2 && path[s] == '.' && path[s + 1] == '.'
                if !this->seg.is_empty()                 # ".." 上溯（不越根）
                    this->seg.back(1)
                continue
            if !this->seg.feed(0, (&path[s]: const &), slen)
                return false
        return true

    # 上溯一段（等价 ".."）：栈空为空操作。
    up: fnc: bool
        return this->seg.back(1)

    # 上溯 n 段（back 为 sc 保留字，故名 ascend）。
    ascend: fnc: bool, n: i4
        return this->seg.back(n)

    # 拼接为路径串写入 buffer（NUL 结尾）：绝对路径前缀 '/'；空栈 → 绝对为 "/"、相对为 "."。
    #   buffer 为 nil 时仅测长。返回不含 NUL 的字节数；buffer 太小返回 -1。
    build_to: fnc: i4, buffer: char&, size: u4
        var body: i4 = this->seg.build_to("/", nil, 0)   # 测长（不含 NUL）
        if body < 0
            return -1
        var pre: u4 = 0
        if this->abs
            pre = 1
        var need: u4 = pre + (body: u4)
        if need == 0                                     # 空相对 → "."
            need = 1
        if buffer == nil
            return (need: i4)
        if need + 1 > size
            return -1
        var off: u4 = 0
        if this->abs
            buffer[0] = '/'
            off = 1
        if body == 0 && !this->abs
            buffer[0] = '.'
            buffer[1] = 0
            return 1
        this->seg.build_to("/", (&buffer[off]: char&), size - off)
        return (need: i4)

    # 拼接为路径串，由 mem 分配恰好容量的缓冲返回（NUL 结尾）；调用方用 recycle 释放；失败 nil。
    build: fnc: char&
        var need: i4 = this->build_to(nil, 0)
        if need < 0
            return nil
        var buf: char& = (chunk((need + 1): u8): char&)
        if buf == nil
            return nil
        this->build_to(buf, (need + 1: u4))
        return buf
}

