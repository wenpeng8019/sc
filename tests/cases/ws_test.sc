# ws 单元测试：协议纯函数确定性向量（RFC 6455）。
#   覆盖 UTF-8 DFA 校验（§8.1）、close 状态码合法性（§7.4.1）、帧头构造（§5.2）。
#   被测组件 ws.sc 是 templates 通用 utils 件，经相对路径 inc。
# 运行：scc tests/cases/ws_test.sc --test

inc ../../templates/utils/ws.sc

tst "UTF-8 DFA 合法序列（§8.1）"
    # ASCII
    var a[2]: u1
    a[0] = 'h'
    a[1] = 'i'
    assert ws_utf8_check(WS_UTF8_ACCEPT, (&a[0]: u1&), 2) == WS_UTF8_ACCEPT, "ascii"
    # 2 字节 é = C3 A9
    var b2[2]: u1
    b2[0] = 0xC3
    b2[1] = 0xA9
    assert ws_utf8_check(WS_UTF8_ACCEPT, (&b2[0]: u1&), 2) == WS_UTF8_ACCEPT, "2-byte"
    # 3 字节 € = E2 82 AC
    var b3[3]: u1
    b3[0] = 0xE2
    b3[1] = 0x82
    b3[2] = 0xAC
    assert ws_utf8_check(WS_UTF8_ACCEPT, (&b3[0]: u1&), 3) == WS_UTF8_ACCEPT, "3-byte"
    # 4 字节 😀 = F0 9F 98 80
    var b4[4]: u1
    b4[0] = 0xF0
    b4[1] = 0x9F
    b4[2] = 0x98
    b4[3] = 0x80
    assert ws_utf8_check(WS_UTF8_ACCEPT, (&b4[0]: u1&), 4) == WS_UTF8_ACCEPT, "4-byte"

tst "UTF-8 DFA 非法/截断序列（§8.1）"
    # 裸 0xFF：非法
    var f[1]: u1
    f[0] = 0xFF
    assert ws_utf8_check(WS_UTF8_ACCEPT, (&f[0]: u1&), 1) == WS_UTF8_REJECT, "0xFF reject"
    # 非法 lead 0xC0
    var c0[1]: u1
    c0[0] = 0xC0
    assert ws_utf8_check(WS_UTF8_ACCEPT, (&c0[0]: u1&), 1) == WS_UTF8_REJECT, "0xC0 reject"
    # 截断的 3 字节序列 E2 82：中间态（既非 accept 也非 reject）
    var tr[2]: u1
    tr[0] = 0xE2
    tr[1] = 0x82
    var st: u4 = ws_utf8_check(WS_UTF8_ACCEPT, (&tr[0]: u1&), 2)
    assert st != WS_UTF8_ACCEPT, "trunc not accept"
    assert st != WS_UTF8_REJECT, "trunc not reject"

tst "UTF-8 DFA 跨片增量（§5.4+§8.1）"
    # € 分两次喂：E2 → 中间态；82 AC → accept（沿用上次 state）
    var p0[1]: u1
    p0[0] = 0xE2
    var s1: u4 = ws_utf8_check(WS_UTF8_ACCEPT, (&p0[0]: u1&), 1)
    assert s1 != WS_UTF8_ACCEPT, "after E2 intermediate"
    var p1[2]: u1
    p1[0] = 0x82
    p1[1] = 0xAC
    var s2: u4 = ws_utf8_check(s1, (&p1[0]: u1&), 2)
    assert s2 == WS_UTF8_ACCEPT, "cross-chunk accept"

tst "close 状态码合法性（§7.4.1）"
    assert ws_valid_close_code(1000) == 1, "1000 ok"
    assert ws_valid_close_code(1011) == 1, "1011 ok"
    assert ws_valid_close_code(1004) == 0, "1004 bad"
    assert ws_valid_close_code(1005) == 0, "1005 bad"
    assert ws_valid_close_code(1006) == 0, "1006 bad"
    assert ws_valid_close_code(1012) == 0, "1012 bad"
    assert ws_valid_close_code(2999) == 0, "2999 bad"
    assert ws_valid_close_code(3000) == 1, "3000 ok"
    assert ws_valid_close_code(4999) == 1, "4999 ok"
    assert ws_valid_close_code(5000) == 0, "5000 bad"

tst "帧头构造长度与字段（§5.2）"
    var hdr[14]: u1
    var mk[4]: u1
    mk[0] = 0x11
    mk[1] = 0x22
    mk[2] = 0x33
    mk[3] = 0x44
    # 短帧（<126）：2 字节头，无掩码
    var h1: i4 = ws_build_header((&hdr[0]: u1&), (WS_TEXT: u1), 5, 0, (&mk[0]: u1&))
    assert h1 == 2, "hlen small"
    assert hdr[0] == (0x80 | WS_TEXT), "fin|opcode"
    assert hdr[1] == 5, "len small"
    # 中帧（126..65535）：4 字节头，hdr[1]=126
    var h2: i4 = ws_build_header((&hdr[0]: u1&), (WS_BIN: u1), 200, 0, (&mk[0]: u1&))
    assert h2 == 4, "hlen medium"
    assert hdr[1] == 126, "len medium marker"
    assert hdr[2] == 0, "len hi"
    assert hdr[3] == 200, "len lo"
    # 大帧（>65535）：10 字节头，hdr[1]=127
    var h3: i4 = ws_build_header((&hdr[0]: u1&), (WS_BIN: u1), 70000, 0, (&mk[0]: u1&))
    assert h3 == 10, "hlen large"
    assert hdr[1] == 127, "len large marker"
    # 带掩码：头多 4 字节，置 MASK 位，写掩码键
    var h4: i4 = ws_build_header((&hdr[0]: u1&), (WS_TEXT: u1), 5, 1, (&mk[0]: u1&))
    assert h4 == 6, "hlen masked"
    assert (hdr[1] & 0x80) == 0x80, "mask bit set"
    assert hdr[2] == 0x11, "maskkey0"
    assert hdr[5] == 0x44, "maskkey3"
