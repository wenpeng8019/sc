# config —— .sa 配置解析模块（inc 引用，@ 导出；可独立 --test）
# 格式：`[段]` + `key: value`，`#` 行注释，空行忽略。见 PLAN.md §1.3。
#
# 存储：扁平键值表（键 = "段.key"，如 "llm.model"），定长槽位（够用原则）。
# api_key_env 语义（环境变量间接引用）由消费方（llm.sc）处理，解析器只存字面。

inc io.sc
inc os.sc
inc util.sc

@def sa_cfg: {
    keys[64][96]: char          # "段.key"
    vals[64][512]: char
    count: i4
    err_line: i4                # 首个坏行行号（0=无错）
}

# 行内截段：跳过前导空白。返回新下标。
fnc sa_cfg_skip_ws: i4, s: const char&, i: i4
    var j: i4 = i
    while s[j] == (32: char) || s[j] == (9: char)
        j = j + 1
    return j

# 去尾空白：返回截止下标（开区间）。
fnc sa_cfg_trim_end: i4, s: const char&, from: i4, to: i4
    var j: i4 = to
    while j > from && (s[j - 1] == (32: char) || s[j - 1] == (9: char) || s[j - 1] == (13: char))
        j = j - 1
    return j

# 解析一整块 .sa 文本进 cfg。返回 0 成功 / >0 首个坏行行号。
@fnc sa_cfg_parse: i4, cfg: sa_cfg&, text: const char&
    cfg->count = 0
    cfg->err_line = 0
    var sect[64]: char
    sect[0] = 0
    var i: i4 = 0
    var line: i4 = 0
    while text[i] != 0
        line = line + 1
        # 行首→行尾
        var b: i4 = i
        while text[i] != 0 && text[i] != (10: char)
            i = i + 1
        var e: i4 = i
        if text[i] != 0
            i = i + 1                       # 吃掉换行
        b = sa_cfg_skip_ws(text, b)
        e = sa_cfg_trim_end(text, b, e)
        if e <= b || text[b] == (35: char)  # 空行 / # 注释
            continue
        if text[b] == (91: char)            # '[' 段头
            var ce: i4 = b + 1
            while ce < e && text[ce] != (93: char)
                ce = ce + 1
            if ce >= e                      # 无 ']'
                cfg->err_line = line
                return line
            var k: i4 = 0
            var p: i4 = b + 1
            while p < ce && k < 63
                sect[k] = text[p]
                k = k + 1
                p = p + 1
            sect[k] = 0
            continue
        # key: value
        var col: i4 = b
        while col < e && text[col] != (58: char)   # ':'
            col = col + 1
        if col >= e                          # 无冒号
            cfg->err_line = line
            return line
        var ke: i4 = sa_cfg_trim_end(text, b, col)
        if ke <= b || cfg->count >= 64
            cfg->err_line = line
            return line
        # 组合 "段.key"
        var kk: i4 = 0
        var q: i4 = 0
        while sect[q] != 0 && kk < 90
            cfg->keys[cfg->count][kk] = sect[q]
            kk = kk + 1
            q = q + 1
        if kk > 0
            cfg->keys[cfg->count][kk] = (46: char)   # '.'
            kk = kk + 1
        q = b
        while q < ke && kk < 95
            cfg->keys[cfg->count][kk] = text[q]
            kk = kk + 1
            q = q + 1
        cfg->keys[cfg->count][kk] = 0
        # 值
        var vb: i4 = sa_cfg_skip_ws(text, col + 1)
        var vv: i4 = 0
        while vb < e && vv < 511
            cfg->vals[cfg->count][vv] = text[vb]
            vv = vv + 1
            vb = vb + 1
        cfg->vals[cfg->count][vv] = 0
        cfg->count = cfg->count + 1
    return 0

# 取值：key 形如 "llm.model"。命中返回值指针，未命中返回 dflt。
@fnc sa_cfg_get: const char&, cfg: sa_cfg&, key: const char&, dflt: const char&
    var n: i4 = 0
    while n < cfg->count
        if sa_streq((cfg->keys[n]: const char&), key)
            return (cfg->vals[n]: const char&)
        n = n + 1
    return dflt

# 载入 .sagent/config.sa。返回 0 成功；1 文件缺失；>1 坏行行号。
@fnc sa_cfg_load: i4, cfg: sa_cfg&
    var text: char& = sa_read_file(".sagent/config.sa")
    if text == nil
        return 1
    var r: i4 = sa_cfg_parse(cfg, text)
    ::free((text: &))
    return r

tst "sa 解析：段+键值+注释+空行"
    var cfg: sa_cfg
    var t: const char& = "# 注释\n[llm]\nmodel: gpt-4o-mini\nendpoint: https://x/v1\n\n[loop]\nbudget: 10\n"
    assert sa_cfg_parse(&cfg, t) == 0
    assert cfg.count == 3
    assert sa_streq(sa_cfg_get(&cfg, "llm.model", "?"), "gpt-4o-mini")
    assert sa_streq(sa_cfg_get(&cfg, "llm.endpoint", "?"), "https://x/v1")
    assert sa_streq(sa_cfg_get(&cfg, "loop.budget", "?"), "10")

tst "sa 解析：默认值与空白容忍"
    var cfg: sa_cfg
    var t: const char& = "[llm]\n  model  :   abc  \n"
    assert sa_cfg_parse(&cfg, t) == 0
    assert sa_streq(sa_cfg_get(&cfg, "llm.model", "?"), "abc")
    assert sa_streq(sa_cfg_get(&cfg, "llm.miss", "dflt"), "dflt")

tst "sa 解析：坏行报行号"
    var cfg: sa_cfg
    var t: const char& = "[llm]\nmodel gpt\n"
    assert sa_cfg_parse(&cfg, t) == 2
    assert cfg.err_line == 2

tst "sa 解析：无段裸键"
    var cfg: sa_cfg
    var t: const char& = "top: 1\n[s]\nk: v\n"
    assert sa_cfg_parse(&cfg, t) == 0
    assert sa_streq(sa_cfg_get(&cfg, "top", "?"), "1")
    assert sa_streq(sa_cfg_get(&cfg, "s.k", "?"), "v")
