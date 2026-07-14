# json —— JSON 最小库模块（inc 引用，@ 导出；可独立 --test）。见 PLAN.md §1.4。
# 构造面：转义追加（组装 chat completions 请求体用）。
# 解析面：扫描式取值器（键定位 + 字符串值反转义），不做完整 DOM。
# 扫描器正确跳过字符串内容（含转义），值内出现键名不会误匹配。

inc adt.sc

# 追加转义后的 JSON 字符串内容（不含首尾引号）。
@fnc json_escape: out: string&, s: const char&
    var i: u4 = 0
    while s[i] != 0
        var c: char = s[i]
        if c == (34: char)                    # '"'
            out->append("\\\"")
        else if c == (92: char)               # '\'
            out->append("\\\\")
        else if c == (10: char)               # \n
            out->append("\\n")
        else if c == (13: char)               # \r
            out->append("\\r")
        else if c == (9: char)                # \t
            out->append("\\t")
        else if (c: u1) < 32
            out->printf("\\u%04x", (c: i4))
        else
            out->append_char(c)
        i = i + 1

# 追加一个完整 JSON 字符串（含引号）。
@fnc json_str: out: string&, s: const char&
    out->append_char((34: char))
    json_escape(out, s)
    out->append_char((34: char))

# 跳过一个字符串 token：i 指向开引号，返回闭引号后下标；未闭合返回 -1。
fnc json_skip_str: i4, t: const char&, i: i4
    var j: i4 = i + 1
    while t[j] != 0
        if t[j] == (92: char)                 # 转义：跳过下一字符
            if t[j + 1] == 0
                return -1
            j = j + 2
            continue
        if t[j] == (34: char)
            return j + 1
        j = j + 1
    return -1

# 定位键：返回 "key": 之后值起始下标（跳过空白）；未找到返回 -1。
# 扫描时跳过所有字符串 token，仅当字符串后紧跟 ':' 才视为键并比对。
fnc json_find_key: i4, t: const char&, key: const char&
    var klen: u4 = (::strlen(key): u4)
    var i: i4 = 0
    while t[i] != 0
        if t[i] != (34: char)
            i = i + 1
            continue
        # 字符串 token：记录内容区间
        var b: i4 = i + 1
        var e: i4 = json_skip_str(t, i)
        if e < 0
            return -1
        i = e
        # 后随 ':' 才是键
        var j: i4 = i
        while t[j] == (32: char) || t[j] == (9: char) || t[j] == (10: char) || t[j] == (13: char)
            j = j + 1
        if t[j] != (58: char)                 # ':'
            continue
        # 键名比对（区间 [b, e-1) 与 key 逐字节；键内转义不支持——LLM API 键均为裸 ASCII）
        if (e - 1 - b: u4) != klen
            continue
        var k: u4 = 0
        var hit: bool = true
        while k < klen
            if t[b + (k: i4)] != key[k]
                hit = false
                break
            k = k + 1
        if !hit
            continue
        # 命中：返回值起始
        j = j + 1
        while t[j] == (32: char) || t[j] == (9: char) || t[j] == (10: char) || t[j] == (13: char)
            j = j + 1
        return j
    return -1

# 反转义字符串值到 out：i 指向开引号。返回 0 成功 / -1 格式错。
fnc json_unescape: i4, t: const char&, i: i4, out: string&
    if t[i] != (34: char)
        return -1
    var j: i4 = i + 1
    while t[j] != 0 && t[j] != (34: char)
        if t[j] != (92: char)
            out->append_char(t[j])
            j = j + 1
            continue
        j = j + 1                             # 转义符
        var c: char = t[j]
        if c == (110: char)                   # n
            out->append_char((10: char))
        else if c == (114: char)              # r
            out->append_char((13: char))
        else if c == (116: char)              # t
            out->append_char((9: char))
        else if c == (117: char)              # uXXXX → UTF-8
            var cp: i4 = 0
            var k: i4 = 0
            while k < 4
                j = j + 1
                var h: char = t[j]
                var d: i4 = -1
                if h >= (48: char) && h <= (57: char)
                    d = (h: i4) - 48
                else if h >= (97: char) && h <= (102: char)
                    d = (h: i4) - 87
                else if h >= (65: char) && h <= (70: char)
                    d = (h: i4) - 55
                if d < 0
                    return -1
                cp = cp * 16 + d
                k = k + 1
            if cp < 128
                out->append_char((cp: char))
            else if cp < 2048
                out->append_char((192 + cp / 64: char))
                out->append_char((128 + cp % 64: char))
            else
                out->append_char((224 + cp / 4096: char))
                out->append_char((128 + (cp / 64) % 64: char))
                out->append_char((128 + cp % 64: char))
        else                                  # " \ / 及其它：字面
            out->append_char(c)
        j = j + 1
    if t[j] != (34: char)
        return -1
    return 0

# 取键的字符串值（首个命中）。返回 0 成功 / -1 未找到或非字符串。
@fnc json_get_str: i4, t: const char&, key: const char&, out: string&
    var v: i4 = json_find_key(t, key)
    if v < 0
        return -1
    return json_unescape(t, v, out)

tst "json 转义：控制字符与引号反斜杠"
    var s: string& = string()
    json_str(s, "a\"b\\c\nd\te")
    assert s->equals("\"a\\\"b\\\\c\\nd\\te\"")
    s->drop()

tst "json 取值：chat completions 响应样例"
    var resp: const char& = "{\"id\":\"cc-1\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"你好！\\n有什么可以帮你？\"},\"finish_reason\":\"stop\"}],\"usage\":{\"total_tokens\":20}}"
    var out: string& = string()
    assert json_get_str(resp, "content", out) == 0
    assert out->equals("你好！\n有什么可以帮你？")
    out->drop()

tst "json 取值：值内键名不误匹配"
    var t: const char& = "{\"note\":\"content: fake\",\"content\":\"real\"}"
    var out: string& = string()
    assert json_get_str(t, "content", out) == 0
    assert out->equals("real")
    out->drop()

tst "json 取值：error.message 与 \\u 转义"
    var t: const char& = "{\"error\":{\"message\":\"bad \\u0041 key\",\"type\":\"auth\"}}"
    var out: string& = string()
    assert json_get_str(t, "message", out) == 0
    assert out->equals("bad A key")
    out->drop()

tst "json 取值：未找到返回 -1"
    var out: string& = string()
    assert json_get_str("{\"a\":1}", "missing", out) == -1
    out->drop()
