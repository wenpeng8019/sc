# json —— 通用 JSON 组件（纯 sc 实现）
#
# 设计：解析结果是一张连续 json_node tape；默认模式把输入复制到连续 string，
# 不为每个值单独分配内存。节点通过 first/last/next 连接，字符串和数字保留
# 原文切片。这个布局对应 yyjson 的“连续 tape + 批量存储”思路，避免 DOM
# 常见的逐节点 malloc、指针追逐和重复字符串复制。
#
# 输入生命周期有三种模式：
#   1. copy（json_parse）：复制输入，doc 自己拥有 source；最安全，源数据可立即释放。
#   2. view（预留）：借用只读 NUL 结尾输入，不复制；调用方必须保证输入一直存活，
#      解析器不修改输入，适合已经由上层长期持有的响应缓冲区。
#   3. in-situ（json_parse_inplace）：借用可写 NUL 结尾缓冲区，不复制；缓冲区必须
#      一直存活，未来字符串反转义可直接回写其中。当前实现已完成零拷贝借用，仍保留
#      转义原文以保证 node 偏移稳定，因此不会破坏输入内容。
#
# 支持 RFC 8259：null/bool/number/string/array/object、嵌套结构、UTF-16
# \u 转义（含 surrogate pair）、严格数字格式、控制字符检查和完整序列化。

inc adt.sc

@def json_node: {
    kind: u1                 # 0=null 1=bool 2=number 3=string 4=array 5=object
    boolean: u1
    parent: i4
    first: i4
    last: i4
    next: i4
    count: u4
    off: u4                  # 字符串/数字原文起点
    len: u4                  # 字符串/数字原文长度
    key_off: u4              # object member key 原文内容（不含引号）
    key_len: u4
}

@def json_doc: {
    source: string&
    data: char&               # 当前输入；copy 指向 source，in-situ 指向外部缓冲区
    borrowed: bool            # data 是否为外部借用
    nodes: array
    root: i4
    pos: u4
    error: i4
}

@fnc json_init: doc: json_doc&
    ::memset(doc, 0, sizeof(::json_doc))
    doc->source = string()
    doc->data = doc->source->cstr()
    doc->borrowed = false
    doc->nodes.init(sizeof(::json_node))
    doc->root = -1
    doc->error = 0

@fnc json_drop: doc: json_doc&
    if doc == nil
        return
    if doc->source != nil
        doc->source->drop()
    doc->data = nil
    doc->nodes.drop()
    doc->root = -1

fnc json_ws: doc: json_doc&
    var s: const char& = doc->data
    while s[doc->pos] == (32: char) || s[doc->pos] == (9: char) || s[doc->pos] == (10: char) || s[doc->pos] == (13: char)
        doc->pos = doc->pos + 1

fnc json_hex: i4, c: char
    if c >= (48: char) && c <= (57: char)
        return (c: i4) - 48
    if c >= (97: char) && c <= (102: char)
        return (c: i4) - 87
    if c >= (65: char) && c <= (70: char)
        return (c: i4) - 55
    return -1

# 扫描字符串，返回内容起点；doc.pos 停在闭引号之后。
fnc json_scan_string: i4, doc: json_doc&
    var s: const char& = doc->data
    if s[doc->pos] != (34: char)
        return -1
    var b: i4 = doc->pos + 1
    var i: i4 = b
    while s[i] != 0
        var c: char = s[i]
        if (c: u1) < 32
            return -1
        if c == (34: char)
            doc->pos = i + 1
            return b
        if c == (92: char)
            i = i + 1
            c = s[i]
            if c == 0
                return -1
            if c == (117: char)
                var k: i4 = 0
                while k < 4
                    if json_hex(s[i + 1]) < 0
                        return -1
                    i = i + 1
                    k = k + 1
            else if c != (34: char) && c != (92: char) && c != (47: char) && c != (98: char) && c != (102: char) && c != (110: char) && c != (114: char) && c != (116: char)
                return -1
        i = i + 1
    return -1

fnc json_add_node: i4, doc: json_doc&, kind: u1, parent: i4
    var n: json_node
    ::memset(&n, 0, sizeof(::json_node))
    n.kind = kind
    n.parent = parent
    n.first = -1
    n.last = -1
    n.next = -1
    n.off = doc->pos
    var idx: i4 = (doc->nodes.len(): i4)
    if !doc->nodes.push(&n)
        return -1
    if parent >= 0
        var p: json_node& = (doc->nodes.at(parent): json_node&)
        if p->first < 0
            p->first = idx
        else
            var q: json_node& = (doc->nodes.at(p->last): json_node&)
            q->next = idx
        p->last = idx
        p->count = p->count + 1
    return idx

fnc json_literal: i4, doc: json_doc&, word: const char&, kind: u1, boolean: u1
    var s: const char& = doc->data
    var i: i4 = 0
    while word[i] != 0
        if s[doc->pos + i] != word[i]
            return -1
        i = i + 1
    doc->pos = doc->pos + i
    return i

fnc json_parse_value: i4, doc: json_doc&, parent: i4
    json_ws(doc)
    var s: const char& = doc->data
    var c: char = s[doc->pos]
    if c == 0
        return -1
    if c == (110: char)
        var z: i4 = json_add_node(doc, 0, parent)
        if z >= 0 && json_literal(doc, "null", 0, 0) < 0
            return -1
        if z >= 0
            var zp: json_node& = (doc->nodes.at(z): json_node&)
            zp->len = 4
        return z
    if c == (116: char) || c == (102: char)
        var bw: const char& = c == (116: char) ? "true" : "false"
        var bv: u1 = c == (116: char) ? 1 : 0
        var b: i4 = json_add_node(doc, 1, parent)
        if b >= 0
            var bl: i4 = json_literal(doc, bw, 1, bv)
            if bl < 0
                return -1
            var bp: json_node& = (doc->nodes.at(b): json_node&)
            bp->boolean = bv
            bp->len = bl
        return b
    if c == (34: char)
        var start: i4 = doc->pos
        var so: i4 = json_scan_string(doc)
        if so < 0
            return -1
        var n: i4 = json_add_node(doc, 3, parent)
        if n < 0
            return -1
        var p: json_node& = (doc->nodes.at(n): json_node&)
        p->off = so
        p->len = doc->pos - so - 1
        return n
    if c == (91: char)
        doc->pos = doc->pos + 1
        var a: i4 = json_add_node(doc, 4, parent)
        if a < 0
            return -1
        json_ws(doc)
        if s[doc->pos] == (93: char)
            doc->pos = doc->pos + 1
            return a
        while true
            if json_parse_value(doc, a) < 0
                return -1
            json_ws(doc)
            if s[doc->pos] == (93: char)
                doc->pos = doc->pos + 1
                return a
            if s[doc->pos] != (44: char)
                return -1
            doc->pos = doc->pos + 1
    if c == (123: char)
        doc->pos = doc->pos + 1
        var o: i4 = json_add_node(doc, 5, parent)
        if o < 0
            return -1
        json_ws(doc)
        if s[doc->pos] == (125: char)
            doc->pos = doc->pos + 1
            return o
        while true
            json_ws(doc)
            var kb: i4 = json_scan_string(doc)
            if kb < 0
                return -1
            var ke: i4 = doc->pos - 1
            json_ws(doc)
            if s[doc->pos] != (58: char)
                return -1
            doc->pos = doc->pos + 1
            var child: i4 = json_parse_value(doc, o)
            if child < 0
                return -1
            var cp: json_node& = (doc->nodes.at(child): json_node&)
            cp->key_off = kb
            cp->key_len = ke - kb
            json_ws(doc)
            if s[doc->pos] == (125: char)
                doc->pos = doc->pos + 1
                return o
            if s[doc->pos] != (44: char)
                return -1
            doc->pos = doc->pos + 1
    # number: strict JSON grammar, then retain its source slice.
    if c == (45: char) || (c >= (48: char) && c <= (57: char))
        var bpos: i4 = doc->pos
        if s[doc->pos] == (45: char)
            doc->pos = doc->pos + 1
        if s[doc->pos] == (48: char)
            doc->pos = doc->pos + 1
            if s[doc->pos] >= (48: char) && s[doc->pos] <= (57: char)
                return -1
        else
            if s[doc->pos] < (49: char) || s[doc->pos] > (57: char)
                return -1
            while s[doc->pos] >= (48: char) && s[doc->pos] <= (57: char)
                doc->pos = doc->pos + 1
        if s[doc->pos] == (46: char)
            doc->pos = doc->pos + 1
            if s[doc->pos] < (48: char) || s[doc->pos] > (57: char)
                return -1
            while s[doc->pos] >= (48: char) && s[doc->pos] <= (57: char)
                doc->pos = doc->pos + 1
        if s[doc->pos] == (101: char) || s[doc->pos] == (69: char)
            doc->pos = doc->pos + 1
            if s[doc->pos] == (43: char) || s[doc->pos] == (45: char)
                doc->pos = doc->pos + 1
            if s[doc->pos] < (48: char) || s[doc->pos] > (57: char)
                return -1
            while s[doc->pos] >= (48: char) && s[doc->pos] <= (57: char)
                doc->pos = doc->pos + 1
        var num: i4 = json_add_node(doc, 2, parent)
        if num < 0
            return -1
        var np: json_node& = (doc->nodes.at(num): json_node&)
        np->off = bpos
        np->len = doc->pos - bpos
        return num
    return -1

# 解析并校验完整 JSON。返回 0 成功，-1 失败。
fnc json_parse_buffer: i4, doc: json_doc&, text: const char&, borrow: bool
    if doc == nil
        return -1
    if doc->source == nil
        json_init(doc)
    if !borrow
        doc->source->assign(text)
        doc->data = doc->source->cstr()
    else
        doc->data = (text: char&)
    doc->borrowed = borrow
    doc->nodes.clear()
    # 一般 JSON 中结构符号约占输入的 1/2；预留 tape 容量，避免解析中反复扩容。
    var hint: u8 = doc->source->len() / 2 + 1
    doc->nodes.reserve(hint)
    doc->root = -1
    doc->pos = 0
    doc->error = 0
    var r: i4 = json_parse_value(doc, -1)
    if r < 0
        doc->error = doc->pos + 1
        return -1
    json_ws(doc)
    if doc->data[doc->pos] != 0
        doc->error = doc->pos + 1
        return -1
    doc->root = r
    return 0

# 默认 copy 模式：复制输入，doc 自己拥有数据。
@fnc json_parse: i4, doc: json_doc&, text: const char&
    return json_parse_buffer(doc, text, false)

# in-situ 模式：借用调用方的可写 NUL 结尾缓冲区，不复制输入。
# 当前解析阶段不改写缓冲区；调用方仍须保证 buffer 存活到 json_drop()。
@fnc json_parse_inplace: i4, doc: json_doc&, buffer: char&
    return json_parse_buffer(doc, buffer, true)

@fnc json_type: i4, doc: json_doc&, node: i4
    if doc == nil || node < 0 || node >= (doc->nodes.len(): i4)
        return -1
    var n: json_node& = (doc->nodes.at(node): json_node&)
    return n->kind

@fnc json_size: i8, doc: json_doc&, node: i4
    if json_type(doc, node) < 0
        return -1
    var n: json_node& = (doc->nodes.at(node): json_node&)
    return n->count

@fnc json_root: i4, doc: json_doc&
    return doc->root

@fnc json_error: i4, doc: json_doc&
    if doc == nil
        return -1
    return doc->error

@fnc json_at: i4, doc: json_doc&, node: i4, index: i4
    if json_type(doc, node) != 4 || index < 0
        return -1
    var n: json_node& = (doc->nodes.at(node): json_node&)
    var i: i4 = 0
    var x: i4 = n->first
    while x >= 0
        if i == index
            return x
        var p: json_node& = (doc->nodes.at(x): json_node&)
        x = p->next
        i = i + 1
    return -1

fnc json_key_equal: bool, doc: json_doc&, n: json_node&, key: const char&
    var s: const char& = doc->data
    var i: u4 = n->key_off
    var end: u4 = n->key_off + n->key_len
    var k: u4 = 0
    while i < end
        var c: char = s[i]
        if c == (92: char)
            i = i + 1
            c = s[i]
            if c == (117: char)
                # Object keys used by the public lookup API are normally ASCII;
                # decode \u escapes to one ASCII byte when possible.
                var cp: i4 = 0
                var h: i4 = 0
                while h < 4
                    cp = cp * 16 + json_hex(s[i + 1])
                    i = i + 1
                    h = h + 1
                if cp > 127 || key[k] != (cp: char)
                    return false
                k = k + 1
            else
                if key[k] != c
                    return false
                k = k + 1
        else
            if key[k] != c
                return false
            k = k + 1
        i = i + 1
    return key[k] == 0

@fnc json_get: i4, doc: json_doc&, object: i4, key: const char&
    if json_type(doc, object) != 5
        return -1
    var p: json_node& = (doc->nodes.at(object): json_node&)
    var x: i4 = p->first
    while x >= 0
        var n: json_node& = (doc->nodes.at(x): json_node&)
        if json_key_equal(doc, n, key)
            return x
        x = n->next
    return -1

@fnc json_string: i4, doc: json_doc&, node: i4, out: string&
    if json_type(doc, node) != 3
        return -1
    var n: json_node& = (doc->nodes.at(node): json_node&)
    out->clear()
    var s: const char& = doc->data
    var i: u4 = n->off
    var end: u4 = n->off + n->len
    while i < end
        var c: char = s[i]
        if c != (92: char)
            out->append_char(c)
            i = i + 1
            continue
        i = i + 1
        c = s[i]
        if c == (34: char) || c == (92: char) || c == (47: char)
            out->append_char(c)
        else if c == (98: char)
            out->append_char((8: char))
        else if c == (102: char)
            out->append_char((12: char))
        else if c == (110: char)
            out->append_char((10: char))
        else if c == (114: char)
            out->append_char((13: char))
        else if c == (116: char)
            out->append_char((9: char))
        else
            var cp: i4 = 0
            var h: i4 = 0
            while h < 4
                i = i + 1
                cp = cp * 16 + json_hex(s[i])
                h = h + 1
            # UTF-16 surrogate pair.
            if cp >= 55296 && cp <= 56319 && i + 6 < end && s[i + 1] == (92: char) && s[i + 2] == (117: char)
                var lo: i4 = 0
                var q: i4 = i + 3
                h = 0
                while h < 4
                    lo = lo * 16 + json_hex(s[q])
                    q = q + 1
                    h = h + 1
                if lo >= 56320 && lo <= 57343
                    cp = 65536 + (cp - 55296) * 1024 + lo - 56320
                    i = q - 1
            if cp < 128
                out->append_char((cp: char))
            else if cp < 2048
                out->append_char((192 + cp / 64: char))
                out->append_char((128 + cp % 64: char))
            else if cp < 65536
                out->append_char((224 + cp / 4096: char))
                out->append_char((128 + (cp / 64) % 64: char))
                out->append_char((128 + cp % 64: char))
            else
                out->append_char((240 + cp / 262144: char))
                out->append_char((128 + (cp / 4096) % 64: char))
                out->append_char((128 + (cp / 64) % 64: char))
                out->append_char((128 + cp % 64: char))
        i = i + 1
    return 0

@fnc json_number: f8, doc: json_doc&, node: i4
    if json_type(doc, node) != 2
        return 0
    var n: json_node& = (doc->nodes.at(node): json_node&)
    var s: const char& = doc->data
    return ::strtod(s + n->off, nil)

@fnc json_bool: bool, doc: json_doc&, node: i4
    if json_type(doc, node) != 1
        return false
    var n: json_node& = (doc->nodes.at(node): json_node&)
    return n->boolean != 0

# 追加 JSON 字符串（含引号），使用单次扫描；ASCII 控制字符按 \u00XX 输出。
@fnc json_write_string: bool, out: string&, s: const char&
    if !out->append_char((34: char))
        return false
    var i: u4 = 0
    while s[i] != 0
        var c: char = s[i]
        if c == (34: char)
            out->append("\\\"")
        else if c == (92: char)
            out->append("\\\\")
        else if c == (8: char)
            out->append("\\b")
        else if c == (12: char)
            out->append("\\f")
        else if c == (10: char)
            out->append("\\n")
        else if c == (13: char)
            out->append("\\r")
        else if c == (9: char)
            out->append("\\t")
        else if (c: u1) < 32
            out->printf("\\u%04x", c: i4)
        else
            out->append_char(c)
        i = i + 1
    return out->append_char((34: char))

fnc json_indent: bool, out: string&, depth: i4
    var i: i4 = 0
    while i < depth
        if !out->append("  ")
            return false
        i = i + 1
    return true

fnc json_write_node: bool, doc: json_doc&, node: i4, out: string&, pretty: bool, depth: i4
    var n: json_node& = (doc->nodes.at(node): json_node&)
    var s: const char& = doc->data
    if n->kind == 0
        return out->append("null")
    if n->kind == 1
        return out->append(n->boolean != 0 ? "true" : "false")
    if n->kind == 2
        return out->append_n(s + n->off, n->len)
    if n->kind == 3
        var tmp: string& = string()
        json_string(doc, node, tmp)
        var ok: bool = json_write_string(out, tmp->cstr())
        tmp->drop()
        return ok
    var open: char = n->kind == 4 ? (91: char) : (123: char)
    var close: char = n->kind == 4 ? (93: char) : (125: char)
    out->append_char(open)
    if n->count == 0
        return out->append_char(close)
    if pretty
        out->append_char((10: char))
    var x: i4 = n->first
    var i: i4 = 0
    while x >= 0
        if pretty
            json_indent(out, depth + 1)
        var child: json_node& = (doc->nodes.at(x): json_node&)
        if n->kind == 5
            var ks: string& = string()
            # Reuse the same decoder by using a temporary view node.
            var save_off: u4 = child->off
            var save_len: u4 = child->len
            var save_kind: u1 = child->kind
            child->kind = 3
            child->off = child->key_off
            child->len = child->key_len
            json_string(doc, x, ks)
            child->kind = save_kind
            child->off = save_off
            child->len = save_len
            json_write_string(out, ks->cstr())
            ks->drop()
            out->append(pretty ? ": " : ":")
        if !json_write_node(doc, x, out, pretty, depth + 1)
            return false
        i = i + 1
        x = child->next
        if x >= 0
            out->append_char((44: char))
        if pretty
            out->append_char((10: char))
    if pretty
        json_indent(out, depth)
    return out->append_char(close)

@fnc json_write: bool, doc: json_doc&, node: i4, out: string&, pretty: bool
    if json_type(doc, node) < 0
        return false
    out->clear()
    return json_write_node(doc, node, out, pretty, 0)

# 保留轻量构造 API，便于请求体组装和旧调用方迁移。
@fnc json_escape: out: string&, s: const char&
    var tmp: string& = string()
    json_write_string(tmp, s)
    if tmp->len() >= 2
        out->append_n(tmp->cstr() + 1, tmp->len() - 2)
    tmp->drop()

@fnc json_str: out: string&, s: const char&
    json_write_string(out, s)

# 兼容旧的“按键取字符串”调用；新代码应使用 json_parse/json_get/json_string。
@fnc json_get_str: i4, t: const char&, key: const char&, out: string&
    var d: json_doc
    json_init(&d)
    var rc: i4 = json_parse(&d, t)
    if rc == 0
        var n: i4 = json_get(&d, d.root, key)
        if n >= 0
            rc = json_string(&d, n, out)
        else
            rc = -1
    else
        rc = -1
    json_drop(&d)
    return rc

tst "json 完整解析：对象、数组、数字、布尔、null"
    var d: json_doc
    json_init(&d)
    assert json_parse(&d, "{\"a\":1.25,\"ok\":true,\"x\":null,\"v\":[\"a\",2]}") == 0
    assert json_type(&d, d.root) == 5
    assert json_size(&d, d.root) == 4
    var a: i4 = json_get(&d, d.root, "a")
    assert json_number(&d, a) > 1.2
    assert json_bool(&d, json_get(&d, d.root, "ok"))
    assert json_size(&d, json_get(&d, d.root, "v")) == 2
    json_drop(&d)

tst "json 严格数字和 unicode"
    var d: json_doc
    json_init(&d)
    assert json_parse(&d, "{\"s\":\"A\\u0041\\uD83D\\uDE00\",\"n\":-1.5e+2}") == 0
    var s: string& = string()
    assert json_string(&d, json_get(&d, d.root, "s"), s) == 0
    assert s->equals("AA😀")
    assert json_number(&d, json_get(&d, d.root, "n")) < -149
    s->drop()
    json_drop(&d)

tst "json 序列化往返"
    var d: json_doc
    json_init(&d)
    var out: string& = string()
    assert json_parse(&d, "{\"message\":\"a\\n b\",\"items\":[1,false]}") == 0
    assert json_write(&d, d.root, out, false)
    assert out->equals("{\"message\":\"a\\n b\",\"items\":[1,false]}")
    out->drop()
    json_drop(&d)

tst "json in-situ：零拷贝借用可写缓冲区"
    var d: json_doc
    json_init(&d)
    var buf[64]: char
    ::strcpy(buf, "{\"name\":\"sc\",\"n\":7}")
    assert json_parse_inplace(&d, &buf[0]) == 0
    assert d.borrowed
    var s: string& = string()
    assert json_string(&d, json_get(&d, d.root, "name"), s) == 0
    assert s->equals("sc")
    s->drop()
    json_drop(&d)
