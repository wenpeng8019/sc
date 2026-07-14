# keys —— 用户级 api-key 存取模块（inc 引用，@ 导出）
# 文件 ~/.sagent/keys（0600，行式 ENV=VALUE；VALUE 为 "!" 表示"不再提示"）。
# 项目 config.sa 只存 api_key_env 环境变量名（密钥不入项目库）；本文件是
# env 缺失时的用户级兜底与交互输入的落点。

inc os.sc
inc adt.sc
inc io.sc
inc mem.sc

# keys 文件路径（$HOME/.sagent/keys）。
fnc keys_path: out: string&
    var home: const char& = (::getenv("HOME"): const char&)
    out->clear()
    out->printf("%s/.sagent/keys", home != nil ? home : ".")

# 查 ENV 条目。命中返回 0 并填 val；未命中 -1。
@fnc keys_get: i4, env_name: const char&, val: string&
    var p: string& = string()
    keys_path(p)
    var rc: com@1 = file(p->cstr(), true, 1, 0)
    var t: char& = nil
    if rc != nil
        var rs: com[0]
        rs = rc
        rc >> rs
        t = (rs.take(): char&)
    p->drop()
    if t == nil
        return -1
    var nlen: u4 = (::strlen(env_name): u4)
    var i: i4 = 0
    var r: i4 = -1
    while t[i] != 0
        var b: i4 = i
        while t[i] != 0 && t[i] != (10: char)
            i = i + 1
        # 匹配 "ENV="
        if (i - b: u4) > nlen && t[b + (nlen: i4)] == (61: char)
            var k: u4 = 0
            var hit: bool = true
            while k < nlen
                if t[b + (k: i4)] != env_name[k]
                    hit = false
                    break
                k = k + 1
            if hit
                val->clear()
                val->append_n((t + b + (nlen: i4) + 1: const char&), (i - b - (nlen: i4) - 1: u8))
                r = 0
                break
        if t[i] != 0
            i = i + 1
    recycle((t: &))
    return r

# 写/替换 ENV 条目（0600 权限，目录自动创建）。返回 0 成功。
@fnc keys_put: i4, env_name: const char&, val: const char&
    var p: string& = string()
    keys_path(p)
    # 目录 + 0600 占位
    var dir: string& = string()
    var home: const char& = (::getenv("HOME"): const char&)
    dir->printf("%s/.sagent", home != nil ? home : ".")
    fs_mkdirs(dir->cstr())
    dir->drop()
    # 读旧内容，滤掉同名行
    var s: string& = string()
    var rc: com@1 = file(p->cstr(), true, 1, 0)
    var t: char& = nil
    if rc != nil
        var rs: com[0]
        rs = rc
        rc >> rs
        t = (rs.take(): char&)
    var nlen: u4 = (::strlen(env_name): u4)
    if t != nil
        var i: i4 = 0
        while t[i] != 0
            var b: i4 = i
            while t[i] != 0 && t[i] != (10: char)
                i = i + 1
            var skip: bool = false
            if (i - b: u4) > nlen && t[b + (nlen: i4)] == (61: char)
                var k: u4 = 0
                skip = true
                while k < nlen
                    if t[b + (k: i4)] != env_name[k]
                        skip = false
                        break
                    k = k + 1
            if !skip && i > b
                s->append_n((t + b: const char&), (i - b: u8))
                s->append("\n")
            if t[i] != 0
                i = i + 1
        recycle((t: &))
    s->printf("%s=%s\n", env_name, val)
    # 先建 0600 空文件再写（防窗口期）
    var cmdl: string& = string()
    cmdl->printf("umask 077; : > '%s'", p->cstr())
    ::system(cmdl->cstr())
    cmdl->drop()
    var wc: com@1 = file(p->cstr(), true, 0, 1)
    var r: i4 = -1
    if wc != nil
        wc << s
        r = 0
    s->drop()
    p->drop()
    return r

# 交互读取一行（提示写 stderr；echo_off=true 时关回显，用于密钥）。
# 成功返回 0 并填 out（去尾换行）；EOF/失败 -1。
@fnc sa_prompt_line: i4, prompt: const char&, echo_off: bool, out: string&
    ::fprintf(::stderr, "%s", prompt)
    if echo_off
        ::system("stty -echo 2>/dev/null")
    var buf[512]: char
    var got: & = ::fgets((buf: &), 512, ::stdin)
    if echo_off
        ::system("stty echo 2>/dev/null")
        ::fprintf(::stderr, "\n")
    if got == nil
        return -1
    var n: u4 = (::strlen((buf: const char&)): u4)
    while n > 0 && (buf[n-1] == (10: char) || buf[n-1] == (13: char))
        buf[n-1] = 0
        n = n - 1
    out->clear()
    out->append((buf: const char&))
    return 0
