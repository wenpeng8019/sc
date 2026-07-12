# img_jpg —— JPEG 格式私有实现（由 img.sc 经 `add img_jpg.sc` 内联为子模块）
#
# 依赖：img 结构（img.sc）+ io.sc（com I/O）+ mem.sc（chunk/refit/recycle）+ img_jpg_codec（熵层）。
# 本文件是 img.sc 的内联子单元：只含 JPEG 私有类型与辅助函数；公开接口 jpg_read/jpg_write/jpg_shape
#   定义在 img.sc。单独编译不完整（缺 img 结构）。
#
# 职责分层（对齐设计）：熵编解码（位 I/O + Huffman + 逐块）归 img_jpg_codec（native）；本文件负责
#   容器 marker 解析、量化/Huffman 表提取、分量/MCU 几何、IDCT/FDCT、量化/反量化、上采样、颜色变换。
# 全功能解码（对齐 stb_image.h）：baseline(SOF0/1) + progressive(SOF2)；灰度/YCbCr/RGB/CMYK/YCCK；
#   H/V 采样任意；重启间隔 RSTn；APP0(JFIF)/APP14(Adobe) 颜色变换判定。
# 全功能编码（对齐 stb_image_write.h）：baseline；quality 可调；quality<=90 时 4:2:0 色度下采样。

inc io.sc
inc mem.sc
add img_jpg_codec.sc

# ============================ 常量表 ============================

# zigzag → 自然(行主序) 位置：DQT 段按 zigzag 存，解出后据此还原自然序。
var jpg_dezig[64]: u1 = [
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
    12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
]

# 自然(行主序) → zigzag 位置：编码量化后据此把系数落位到 du[]（zigzag 序，du[0]=DC）。
var jpg_zig[64]: i4 = [
    0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,
    3,8,12,17,25,30,41,43,9,11,18,24,31,40,44,53,
    10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,
    21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63
]

# 标准 Huffman 表（编码用；解码用 DHT 段现读）。counts[16] + values。
var jpg_dc_lum_cnt[16]: u1 = [0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0]
var jpg_dc_lum_val[12]: u1 = [0,1,2,3,4,5,6,7,8,9,10,11]
var jpg_dc_chr_cnt[16]: u1 = [0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0]
var jpg_dc_chr_val[12]: u1 = [0,1,2,3,4,5,6,7,8,9,10,11]
var jpg_ac_lum_cnt[16]: u1 = [0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d]
var jpg_ac_lum_val[162]: u1 = [
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
]
var jpg_ac_chr_cnt[16]: u1 = [0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77]
var jpg_ac_chr_val[162]: u1 = [
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
]

# 标准量化基表（自然序，quality=50 基准）。
var jpg_yqt[64]: i4 = [
    16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,
    14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,
    49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99
]
var jpg_uvqt[64]: i4 = [
    17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,
    24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
]
# AAN FDCT 缩放基（×2.828427125 得实际 aasf）。
var jpg_aasf_base[8]: f4 = [
    1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
    1.0f, 0.785694958f, 0.541196100f, 0.275899379f
]

# ---- 整数 IDCT 定点常量（stb (int)(x*4096+0.5)；惰性一次初始化）----
var jpg_ic_ready: i4 = 0
var jpg_ic_a: i4 = 0        # f2f(0.5411961)
var jpg_ic_b: i4 = 0        # f2f(-1.847759065)
var jpg_ic_c: i4 = 0        # f2f(0.765366865)
var jpg_ic_d: i4 = 0        # f2f(1.175875602)
var jpg_ic_e: i4 = 0        # f2f(0.298631336)
var jpg_ic_f: i4 = 0        # f2f(2.053119869)
var jpg_ic_g: i4 = 0        # f2f(3.072711026)
var jpg_ic_h: i4 = 0        # f2f(1.501321110)
var jpg_ic_i: i4 = 0        # f2f(-0.899976223)
var jpg_ic_j: i4 = 0        # f2f(-2.562915447)
var jpg_ic_k: i4 = 0        # f2f(-1.961570560)
var jpg_ic_l: i4 = 0        # f2f(-0.390180644)

fnc jpg_idct_init:
    if jpg_ic_ready != 0
        return
    jpg_ic_a = (0.5411961 * 4096.0 + 0.5: i4)
    jpg_ic_b = (-1.847759065 * 4096.0 + 0.5: i4)
    jpg_ic_c = (0.765366865 * 4096.0 + 0.5: i4)
    jpg_ic_d = (1.175875602 * 4096.0 + 0.5: i4)
    jpg_ic_e = (0.298631336 * 4096.0 + 0.5: i4)
    jpg_ic_f = (2.053119869 * 4096.0 + 0.5: i4)
    jpg_ic_g = (3.072711026 * 4096.0 + 0.5: i4)
    jpg_ic_h = (1.501321110 * 4096.0 + 0.5: i4)
    jpg_ic_i = (-0.899976223 * 4096.0 + 0.5: i4)
    jpg_ic_j = (-2.562915447 * 4096.0 + 0.5: i4)
    jpg_ic_k = (-1.961570560 * 4096.0 + 0.5: i4)
    jpg_ic_l = (-0.390180644 * 4096.0 + 0.5: i4)
    jpg_ic_ready = 1
    return

# ============================ 结构 ============================

# 分量：几何 + 采样 + 数据平面 + progressive 系数 + 上采样状态。
def jpg_comp: {
    id:      i4        # 分量标识
    h:       i4        # 水平采样因子
    v:       i4        # 垂直采样因子
    tq:      i4        # 量化表选择
    hd:      i4        # DC Huffman 表选择（SOS）
    ha:      i4        # AC Huffman 表选择（SOS）
    dc_pred: i4        # DC 预测值
    x:       i4        # 分量像素宽（有效）
    y:       i4        # 分量像素高（有效）
    w2:      i4        # 数据平面行跨（补到 MCU）
    h2:      i4        # 数据平面高
    data:    u1&       # 分量样本平面（w2*h2）
    coeff:   i2&       # progressive：系数平面（coeff_w*coeff_h*64，自然序）
    coeff_w: i4        # 块宽（补齐）
    coeff_h: i4        # 块高
    # 上采样状态
    hs:      i4
    vs:      i4
    ystep:   i4
    ypos:    i4
    wlores:  i4
    line0:   u1&
    line1:   u1&
    linebuf: u1&
}

# 解码器上下文。
def jpg_dec: {
    img_w:   i4
    img_h:   i4
    img_n:   i4
    comp[4]: jpg_comp
    dequant[256]: u2   # 4 表 × 64（自然序）
    h_max:   i4
    v_max:   i4
    mcu_w:   i4
    mcu_h:   i4
    mcu_x:   i4
    mcu_y:   i4
    scan_n:  i4
    order[4]: i4
    spec_start: i4
    spec_end:   i4
    succ_high:  i4
    succ_low:   i4
    progressive: i4
    restart_interval: i4
    rgb:     i4
    jfif:    i4
    app14:   i4        # -1 未见 / 0 CMYK / 1 YCbCr / 2 YCCK
    held:    i4        # 预读的 marker（-1=无）
    eof_inj: i4        # 已注入合成 EOI
    jd:      &         # codec_jdec 状态
}

# 流式字节读取器（覆盖 com；容器解析用）。
def jpg_br: {
    c:    com&
    buf:  u1&
    cap:  u8
    pos:  u8
    len:  u8
    eof:  i4
}

# ============================ 读取器 ============================

fnc jpg_br_init: i4, br: jpg_br&, c: com&
    br->c = c
    br->cap = 8192
    br->buf = (chunk(br->cap): u1&)
    if br->buf == nil
        return -1
    br->pos = 0
    br->len = 0
    br->eof = 0
    return 0

fnc jpg_br_free: br: jpg_br&
    if br->buf != nil
        recycle((br->buf: &))
        br->buf = nil
    return

# 确保至少 1 字节可读；返回可读字节数（0=eof）。
fnc jpg_br_fill: u8, br: jpg_br&
    if br->pos < br->len
        return br->len - br->pos
    if br->eof != 0
        return 0
    br->pos = 0
    br->len = 0
    var want: u4 = (br->cap: u4)
    var r: i4 = br->c->read((&br->buf[0]: &), &want)
    if r < 0 || want == 0
        br->eof = 1
        return 0
    br->len = (want: u8)
    return br->len

fnc jpg_br_get8: i4, br: jpg_br&
    if jpg_br_fill(br) == 0
        return -1
    var b: i4 = (br->buf[br->pos]: i4)
    br->pos = br->pos + 1
    return b

fnc jpg_br_get16: i4, br: jpg_br&
    var hi: i4 = jpg_br_get8(br)
    if hi < 0
        return -1
    var lo: i4 = jpg_br_get8(br)
    if lo < 0
        return -1
    return (hi << 8) | lo

# 精确读 n 字节到 dst；0 成功 / -1 eof。
fnc jpg_br_read: i4, br: jpg_br&, dst: u1&, n: u8
    var got: u8 = 0
    while got < n
        var avail: u8 = jpg_br_fill(br)
        if avail == 0
            return -1
        var take: u8 = n - got
        if take > avail
            take = avail
        var i: u8 = 0
        while i < take
            dst[got + i] = br->buf[br->pos + i]
            i = i + 1
        br->pos = br->pos + take
        got = got + take
    return 0

fnc jpg_br_skip: i4, br: jpg_br&, n: u8
    var left: u8 = n
    while left > 0
        var avail: u8 = jpg_br_fill(br)
        if avail == 0
            return -1
        var take: u8 = left
        if take > avail
            take = avail
        br->pos = br->pos + take
        left = left - take
    return 0

# ============================ 精确写 ============================

fnc jpg_wr: i4, c: com&, buf: &, n: u8
    var p: u1& = (buf: u1&)
    var put: u8 = 0
    while put < n
        var rem: u8 = n - put
        var want: u4 = 0
        if rem > 0x7FFFFFFF
            want = 0x7FFFFFFF
        else
            want = (rem: u4)
        var r: i4 = c->write((&p[put]: &), &want)
        if r < 0
            return -1
        if want == 0
            return -1
        put = put + (want: u8)
    return 0

# ============================ marker 解析 ============================

# 读下一个 marker 码（跳过填充 0xFF）；含预读 held。返回 marker 或 -1。
fnc jpg_read_marker: i4, dec: jpg_dec&, br: jpg_br&
    if dec->held >= 0
        var h: i4 = dec->held
        dec->held = -1
        return h
    var x: i4 = jpg_br_get8(br)
    if x < 0
        return -1
    while x != 0xFF
        x = jpg_br_get8(br)
        if x < 0
            return -1
    while x == 0xFF
        x = jpg_br_get8(br)
        if x < 0
            return -1
    return x

# SOF：帧头（几何 + 分量 + 分配平面）。
fnc jpg_process_sof: i4, dec: jpg_dec&, br: jpg_br&
    var seglen: i4 = jpg_br_get16(br)
    var prec: i4 = jpg_br_get8(br)
    if prec != 8
        return -1
    dec->img_h = jpg_br_get16(br)
    dec->img_w = jpg_br_get16(br)
    if dec->img_h <= 0 || dec->img_w <= 0
        return -1
    var nc: i4 = jpg_br_get8(br)
    if nc != 1 && nc != 3 && nc != 4
        return -1
    dec->img_n = nc
    dec->rgb = 0
    var hmax: i4 = 1
    var vmax: i4 = 1
    var i: i4 = 0
    while i < nc
        var cid: i4 = jpg_br_get8(br)
        var hv: i4 = jpg_br_get8(br)
        var tq: i4 = jpg_br_get8(br)
        var cp: jpg_comp& = &dec->comp[i]
        cp->id = cid
        cp->h = (hv >> 4) & 15
        cp->v = hv & 15
        cp->tq = tq
        if cp->h <= 0 || cp->v <= 0 || tq > 3
            return -1
        # RGB 直存判定（分量标识为 'R''G''B'）
        if nc == 3
            if (i == 0 && cid == 82) || (i == 1 && cid == 71) || (i == 2 && cid == 66)
                dec->rgb = dec->rgb + 1
        if cp->h > hmax
            hmax = cp->h
        if cp->v > vmax
            vmax = cp->v
        i = i + 1
    dec->h_max = hmax
    dec->v_max = vmax
    dec->mcu_w = hmax * 8
    dec->mcu_h = vmax * 8
    dec->mcu_x = (dec->img_w + dec->mcu_w - 1) / dec->mcu_w
    dec->mcu_y = (dec->img_h + dec->mcu_h - 1) / dec->mcu_h
    # 每分量尺寸 + 分配平面
    i = 0
    while i < nc
        var cp: jpg_comp& = &dec->comp[i]
        cp->x = (dec->img_w * cp->h + hmax - 1) / hmax
        cp->y = (dec->img_h * cp->v + vmax - 1) / vmax
        cp->w2 = dec->mcu_x * cp->h * 8
        cp->h2 = dec->mcu_y * cp->v * 8
        cp->data = (chunk((cp->w2 * cp->h2: u8)): u1&)
        if cp->data == nil
            return -1
        cp->coeff = nil
        if dec->progressive != 0
            cp->coeff_w = cp->w2 / 8
            cp->coeff_h = cp->h2 / 8
            var cb: u8 = (cp->coeff_w: u8) * (cp->coeff_h: u8) * 64 * 2
            cp->coeff = (chunk0(cb): i2&)
            if cp->coeff == nil
                return -1
        i = i + 1
    return 0

# DQT：量化表（zigzag 存 → 自然序）。
fnc jpg_process_dqt: i4, dec: jpg_dec&, br: jpg_br&
    var seglen: i4 = jpg_br_get16(br)
    var l: i4 = seglen - 2
    while l > 0
        var q: i4 = jpg_br_get8(br)
        var p: i4 = q >> 4
        var t: i4 = q & 15
        if t > 3
            return -1
        var i: i4 = 0
        while i < 64
            var v: i4
            if p != 0
                v = jpg_br_get16(br)
            else
                v = jpg_br_get8(br)
            dec->dequant[t * 64 + (jpg_dezig[i]: i4)] = (v: u2)
            i = i + 1
        if p != 0
            l = l - 129
        else
            l = l - 65
    return 0

# DHT：Huffman 表 → 交 codec。
fnc jpg_process_dht: i4, dec: jpg_dec&, br: jpg_br&
    var seglen: i4 = jpg_br_get16(br)
    var l: i4 = seglen - 2
    while l > 0
        var tq: i4 = jpg_br_get8(br)
        var tc: i4 = tq >> 4
        var th: i4 = tq & 15
        if tc > 1 || th > 3
            return -1
        var counts[16]: u1
        var total: i4 = 0
        var i: i4 = 0
        while i < 16
            counts[i] = (jpg_br_get8(br): u1)
            total = total + (counts[i]: i4)
            i = i + 1
        if total > 256
            return -1
        var values[256]: u1
        if jpg_br_read(br, (&values[0]: u1&), (total: u8)) < 0
            return -1
        if codec_jdec_dht(dec->jd, tc, th, (&counts[0]: u1&), (&values[0]: u1&)) < 0
            return -1
        l = l - 17 - total
    return 0

# DRI：重启间隔。
fnc jpg_process_dri: i4, dec: jpg_dec&, br: jpg_br&
    var seglen: i4 = jpg_br_get16(br)
    dec->restart_interval = jpg_br_get16(br)
    return 0

# APPn/COM 等：解析 JFIF/Adobe，其余跳过。
fnc jpg_process_app: i4, dec: jpg_dec&, br: jpg_br&, m: i4
    var seglen: i4 = jpg_br_get16(br)
    var l: i4 = seglen - 2
    if m == 0xE0 && l >= 5
        # JFIF 标识
        var t0: i4 = jpg_br_get8(br)
        var t1: i4 = jpg_br_get8(br)
        var t2: i4 = jpg_br_get8(br)
        var t3: i4 = jpg_br_get8(br)
        var t4: i4 = jpg_br_get8(br)
        l = l - 5
        if t0 == 74 && t1 == 70 && t2 == 73 && t3 == 70 && t4 == 0
            dec->jfif = 1
    else if m == 0xEE && l >= 12
        # Adobe 标识 → 颜色变换
        var ok: i4 = 1
        var tag[6]: u1
        tag[0] = 65
        tag[1] = 100
        tag[2] = 111
        tag[3] = 98
        tag[4] = 101
        tag[5] = 0
        var i: i4 = 0
        while i < 6
            if jpg_br_get8(br) != (tag[i]: i4)
                ok = 0
            i = i + 1
        l = l - 6
        if ok != 0
            var vv: i4 = jpg_br_get8(br)
            var f0: i4 = jpg_br_get16(br)
            var f1: i4 = jpg_br_get16(br)
            dec->app14 = jpg_br_get8(br)
            l = l - 6
    if l > 0
        if jpg_br_skip(br, (l: u8)) < 0
            return -1
    return 0

# 未知带长 marker：跳过。
fnc jpg_skip_segment: i4, br: jpg_br&
    var seglen: i4 = jpg_br_get16(br)
    if seglen < 2
        return -1
    if jpg_br_skip(br, ((seglen - 2): u8)) < 0
        return -1
    return 0

# SOS：扫描头。
fnc jpg_process_sos: i4, dec: jpg_dec&, br: jpg_br&
    var seglen: i4 = jpg_br_get16(br)
    var ns: i4 = jpg_br_get8(br)
    if ns < 1 || ns > 4
        return -1
    dec->scan_n = ns
    var i: i4 = 0
    while i < ns
        var cid: i4 = jpg_br_get8(br)
        var tt: i4 = jpg_br_get8(br)
        var which: i4 = -1
        var k: i4 = 0
        while k < dec->img_n
            if dec->comp[k].id == cid
                which = k
            k = k + 1
        if which < 0
            return -1
        dec->comp[which].hd = (tt >> 4) & 15
        dec->comp[which].ha = tt & 15
        dec->order[i] = which
        i = i + 1
    dec->spec_start = jpg_br_get8(br)
    dec->spec_end = jpg_br_get8(br)
    var ahal: i4 = jpg_br_get8(br)
    dec->succ_high = (ahal >> 4) & 15
    dec->succ_low = ahal & 15
    if dec->progressive == 0
        dec->spec_start = 0
        dec->spec_end = 63
        dec->succ_high = 0
        dec->succ_low = 0
    return 0

# ============================ 扫描熵字节预读 ============================

# 从 br 读原始扫描熵字节（含 0xFF00 塞与 RSTn），直到终止 marker（0xFF 跟 非00/非RST）。
# 追加合成 EOI(0xFF 0xD9) 使 codec 在尾部见 marker 而补零。out_buf 由调用方 recycle。
# out_len 为含合成 EOI 的总长；out_marker 为真实终止 marker 码；br 停在其后。
fnc jpg_read_scan: i4, br: jpg_br&, out_buf: &&, out_len: u8&, out_marker: i4&
    var cap: u8 = 65536
    var buf: u1& = (chunk(cap): u1&)
    if buf == nil
        return -1
    var n: u8 = 0
    var fin: i4 = 0
    while fin == 0
        var b: i4 = jpg_br_get8(br)
        if b < 0
            out_marker[0] = 0xD9
            fin = 1
        else if b == 0xFF
            var cc: i4 = jpg_br_get8(br)
            while cc == 0xFF
                cc = jpg_br_get8(br)
            if cc < 0
                out_marker[0] = 0xD9
                fin = 1
            else if cc == 0x00
                if n + 2 > cap
                    cap = cap * 2
                    buf = (refit((buf: &), cap): u1&)
                    if buf == nil
                        return -1
                buf[n] = 0xFF
                buf[n + 1] = 0x00
                n = n + 2
            else if cc >= 0xD0 && cc <= 0xD7
                if n + 2 > cap
                    cap = cap * 2
                    buf = (refit((buf: &), cap): u1&)
                    if buf == nil
                        return -1
                buf[n] = 0xFF
                buf[n + 1] = (cc: u1)
                n = n + 2
            else
                out_marker[0] = cc
                fin = 1
        else
            if n + 1 > cap
                cap = cap * 2
                buf = (refit((buf: &), cap): u1&)
                if buf == nil
                    return -1
            buf[n] = (b: u1)
            n = n + 1
    # 追加合成 EOI
    if n + 2 > cap
        cap = cap + 2
        buf = (refit((buf: &), cap): u1&)
        if buf == nil
            return -1
    buf[n] = 0xFF
    buf[n + 1] = 0xD9
    n = n + 2
    out_buf[0] = (buf: &)
    out_len[0] = n
    return 0

# ============================ 整数 IDCT ============================

fnc jpg_clamp: u1, x: i4
    if x < 0
        return 0
    if x > 255
        return 255
    return (x: u1)

# stb 整数 IDCT：data 为反量化后自然序 short[64]；out 写 u1（行跨 stride）。
fnc jpg_idct_block: out: u1&, stride: i4, data: i2&
    jpg_idct_init()
    var val[64]: i4
    var i: i4 = 0
    # 列
    while i < 8
        var s0: i4 = (data[i]: i4)
        var s1: i4 = (data[i + 8]: i4)
        var s2: i4 = (data[i + 16]: i4)
        var s3: i4 = (data[i + 24]: i4)
        var s4: i4 = (data[i + 32]: i4)
        var s5: i4 = (data[i + 40]: i4)
        var s6: i4 = (data[i + 48]: i4)
        var s7: i4 = (data[i + 56]: i4)
        if s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 0 && s6 == 0 && s7 == 0
            var dc: i4 = s0 * 4
            val[i] = dc
            val[i + 8] = dc
            val[i + 16] = dc
            val[i + 24] = dc
            val[i + 32] = dc
            val[i + 40] = dc
            val[i + 48] = dc
            val[i + 56] = dc
        else
            var p2: i4 = s2
            var p3: i4 = s6
            var p1: i4 = (p2 + p3) * jpg_ic_a
            var t2: i4 = p1 + p3 * jpg_ic_b
            var t3: i4 = p1 + p2 * jpg_ic_c
            p2 = s0
            p3 = s4
            var t0: i4 = (p2 + p3) * 4096
            var t1: i4 = (p2 - p3) * 4096
            var x0: i4 = t0 + t3
            var x3: i4 = t0 - t3
            var x1: i4 = t1 + t2
            var x2: i4 = t1 - t2
            var q0: i4 = s7
            var q1: i4 = s5
            var q2: i4 = s3
            var q3: i4 = s1
            p3 = q0 + q2
            var p4: i4 = q1 + q3
            p1 = q0 + q3
            p2 = q1 + q2
            var p5: i4 = (p3 + p4) * jpg_ic_d
            q0 = q0 * jpg_ic_e
            q1 = q1 * jpg_ic_f
            q2 = q2 * jpg_ic_g
            q3 = q3 * jpg_ic_h
            p1 = p5 + p1 * jpg_ic_i
            p2 = p5 + p2 * jpg_ic_j
            p3 = p3 * jpg_ic_k
            p4 = p4 * jpg_ic_l
            q3 = q3 + p1 + p4
            q2 = q2 + p2 + p3
            q1 = q1 + p2 + p4
            q0 = q0 + p1 + p3
            x0 = x0 + 512
            x1 = x1 + 512
            x2 = x2 + 512
            x3 = x3 + 512
            val[i] = (x0 + q3) >> 10
            val[i + 56] = (x0 - q3) >> 10
            val[i + 8] = (x1 + q2) >> 10
            val[i + 48] = (x1 - q2) >> 10
            val[i + 16] = (x2 + q1) >> 10
            val[i + 40] = (x2 - q1) >> 10
            val[i + 24] = (x3 + q0) >> 10
            val[i + 32] = (x3 - q0) >> 10
        i = i + 1
    # 行
    i = 0
    while i < 8
        var base: i4 = i * 8
        var s0: i4 = val[base]
        var s1: i4 = val[base + 1]
        var s2: i4 = val[base + 2]
        var s3: i4 = val[base + 3]
        var s4: i4 = val[base + 4]
        var s5: i4 = val[base + 5]
        var s6: i4 = val[base + 6]
        var s7: i4 = val[base + 7]
        var p2: i4 = s2
        var p3: i4 = s6
        var p1: i4 = (p2 + p3) * jpg_ic_a
        var t2: i4 = p1 + p3 * jpg_ic_b
        var t3: i4 = p1 + p2 * jpg_ic_c
        p2 = s0
        p3 = s4
        var t0: i4 = (p2 + p3) * 4096
        var t1: i4 = (p2 - p3) * 4096
        var x0: i4 = t0 + t3
        var x3: i4 = t0 - t3
        var x1: i4 = t1 + t2
        var x2: i4 = t1 - t2
        var q0: i4 = s7
        var q1: i4 = s5
        var q2: i4 = s3
        var q3: i4 = s1
        p3 = q0 + q2
        var p4: i4 = q1 + q3
        p1 = q0 + q3
        p2 = q1 + q2
        var p5: i4 = (p3 + p4) * jpg_ic_d
        q0 = q0 * jpg_ic_e
        q1 = q1 * jpg_ic_f
        q2 = q2 * jpg_ic_g
        q3 = q3 * jpg_ic_h
        p1 = p5 + p1 * jpg_ic_i
        p2 = p5 + p2 * jpg_ic_j
        p3 = p3 * jpg_ic_k
        p4 = p4 * jpg_ic_l
        q3 = q3 + p1 + p4
        q2 = q2 + p2 + p3
        q1 = q1 + p2 + p4
        q0 = q0 + p1 + p3
        x0 = x0 + 65536 + (128 << 17)
        x1 = x1 + 65536 + (128 << 17)
        x2 = x2 + 65536 + (128 << 17)
        x3 = x3 + 65536 + (128 << 17)
        var o: u1& = (&out[i * stride]: u1&)
        o[0] = jpg_clamp((x0 + q3) >> 17)
        o[7] = jpg_clamp((x0 - q3) >> 17)
        o[1] = jpg_clamp((x1 + q2) >> 17)
        o[6] = jpg_clamp((x1 - q2) >> 17)
        o[2] = jpg_clamp((x2 + q1) >> 17)
        o[5] = jpg_clamp((x2 - q1) >> 17)
        o[3] = jpg_clamp((x3 + q0) >> 17)
        o[4] = jpg_clamp((x3 - q0) >> 17)
        i = i + 1
    return

# 反量化 data[64]（自然序）→ 临时 dq → IDCT 到分量平面 (ox,oy)。
fnc jpg_deq_idct: dec: jpg_dec&, cp: jpg_comp&, data: i2&, ox: i4, oy: i4
    var dq[64]: i2
    var t: i4 = cp->tq
    var i: i4 = 0
    while i < 64
        dq[i] = (((data[i]: i4) * (dec->dequant[t * 64 + i]: i4)): i2)
        i = i + 1
    jpg_idct_block((&cp->data[oy * cp->w2 + ox]: u1&), cp->w2, (&dq[0]: i2&))
    return

# ============================ 喂 codec 的块解码 ============================

# baseline：解一块（含反量化 IDCT 已在外做）。返回 1 完成 / -1 错。整段已喂满故不 need_more。
fnc jpg_block_base: i4, dec: jpg_dec&, data: i2&, dc_tbl: i4, ac_tbl: i4, dcp: i4&
    var r: i4 = codec_jdec_block(dec->jd, data, dc_tbl, ac_tbl, dcp)
    if r == 0
        return -1
    return r

# ============================ baseline 扫描解码 ============================

fnc jpg_decode_baseline: i4, dec: jpg_dec&
    var data[64]: i2
    var todo: i4 = dec->restart_interval
    if todo == 0
        todo = 0x7FFFFFFF
    if dec->scan_n == 1
        var ci: i4 = dec->order[0]
        var cp: jpg_comp& = &dec->comp[ci]
        var wbl: i4 = (cp->x + 7) / 8
        var hbl: i4 = (cp->y + 7) / 8
        var by: i4 = 0
        while by < hbl
            var bx: i4 = 0
            while bx < wbl
                if jpg_block_base(dec, (&data[0]: i2&), cp->hd, cp->ha, &cp->dc_pred) < 0
                    return -1
                jpg_deq_idct(dec, cp, (&data[0]: i2&), bx * 8, by * 8)
                todo = todo - 1
                if todo <= 0
                    codec_jdec_reset(dec->jd)
                    cp->dc_pred = 0
                    todo = dec->restart_interval
                    if todo == 0
                        todo = 0x7FFFFFFF
                bx = bx + 1
            by = by + 1
    else
        var my: i4 = 0
        while my < dec->mcu_y
            var mx: i4 = 0
            while mx < dec->mcu_x
                var s: i4 = 0
                while s < dec->scan_n
                    var ci: i4 = dec->order[s]
                    var cp: jpg_comp& = &dec->comp[ci]
                    var vy: i4 = 0
                    while vy < cp->v
                        var vx: i4 = 0
                        while vx < cp->h
                            var bx2: i4 = mx * cp->h + vx
                            var by2: i4 = my * cp->v + vy
                            if jpg_block_base(dec, (&data[0]: i2&), cp->hd, cp->ha, &cp->dc_pred) < 0
                                return -1
                            jpg_deq_idct(dec, cp, (&data[0]: i2&), bx2 * 8, by2 * 8)
                            vx = vx + 1
                        vy = vy + 1
                    s = s + 1
                todo = todo - 1
                if todo <= 0
                    codec_jdec_reset(dec->jd)
                    var z: i4 = 0
                    while z < dec->img_n
                        dec->comp[z].dc_pred = 0
                        z = z + 1
                    todo = dec->restart_interval
                    if todo == 0
                        todo = 0x7FFFFFFF
                mx = mx + 1
            my = my + 1
    return 0

# ============================ progressive 扫描解码 ============================

fnc jpg_decode_prog: i4, dec: jpg_dec&
    var todo: i4 = dec->restart_interval
    if todo == 0
        todo = 0x7FFFFFFF
    if dec->spec_start == 0
        # DC 扫描（可交错）
        if dec->scan_n == 1
            var ci: i4 = dec->order[0]
            var cp: jpg_comp& = &dec->comp[ci]
            var wbl: i4 = (cp->x + 7) / 8
            var hbl: i4 = (cp->y + 7) / 8
            var by: i4 = 0
            while by < hbl
                var bx: i4 = 0
                while bx < wbl
                    var blk: i2& = (&cp->coeff[64 * (by * cp->coeff_w + bx)]: i2&)
                    if codec_jdec_block_prog_dc(dec->jd, blk, cp->hd, &cp->dc_pred, dec->succ_high, dec->succ_low) != 1
                        return -1
                    todo = todo - 1
                    if todo <= 0
                        codec_jdec_reset(dec->jd)
                        cp->dc_pred = 0
                        todo = dec->restart_interval
                        if todo == 0
                            todo = 0x7FFFFFFF
                    bx = bx + 1
                by = by + 1
        else
            var my: i4 = 0
            while my < dec->mcu_y
                var mx: i4 = 0
                while mx < dec->mcu_x
                    var s: i4 = 0
                    while s < dec->scan_n
                        var ci: i4 = dec->order[s]
                        var cp: jpg_comp& = &dec->comp[ci]
                        var vy: i4 = 0
                        while vy < cp->v
                            var vx: i4 = 0
                            while vx < cp->h
                                var bx2: i4 = mx * cp->h + vx
                                var by2: i4 = my * cp->v + vy
                                var blk: i2& = (&cp->coeff[64 * (by2 * cp->coeff_w + bx2)]: i2&)
                                if codec_jdec_block_prog_dc(dec->jd, blk, cp->hd, &cp->dc_pred, dec->succ_high, dec->succ_low) != 1
                                    return -1
                                vx = vx + 1
                            vy = vy + 1
                        s = s + 1
                    todo = todo - 1
                    if todo <= 0
                        codec_jdec_reset(dec->jd)
                        var z: i4 = 0
                        while z < dec->img_n
                            dec->comp[z].dc_pred = 0
                            z = z + 1
                        todo = dec->restart_interval
                        if todo == 0
                            todo = 0x7FFFFFFF
                    mx = mx + 1
                my = my + 1
    else
        # AC 扫描（必单分量非交错）
        var ci: i4 = dec->order[0]
        var cp: jpg_comp& = &dec->comp[ci]
        var wbl: i4 = (cp->x + 7) / 8
        var hbl: i4 = (cp->y + 7) / 8
        var by: i4 = 0
        while by < hbl
            var bx: i4 = 0
            while bx < wbl
                var blk: i2& = (&cp->coeff[64 * (by * cp->coeff_w + bx)]: i2&)
                if codec_jdec_block_prog_ac(dec->jd, blk, cp->ha, dec->spec_start, dec->spec_end, dec->succ_high, dec->succ_low) != 1
                    return -1
                todo = todo - 1
                if todo <= 0
                    codec_jdec_reset(dec->jd)
                    todo = dec->restart_interval
                    if todo == 0
                        todo = 0x7FFFFFFF
                bx = bx + 1
            by = by + 1
    return 0

# progressive 收尾：全分量逐块反量化 + IDCT。
fnc jpg_finish_prog: dec: jpg_dec&
    var n: i4 = 0
    while n < dec->img_n
        var cp: jpg_comp& = &dec->comp[n]
        var wbl: i4 = (cp->x + 7) / 8
        var hbl: i4 = (cp->y + 7) / 8
        var by: i4 = 0
        while by < hbl
            var bx: i4 = 0
            while bx < wbl
                var blk: i2& = (&cp->coeff[64 * (by * cp->coeff_w + bx)]: i2&)
                jpg_deq_idct(dec, cp, blk, bx * 8, by * 8)
                bx = bx + 1
            by = by + 1
        n = n + 1
    return

# 一趟扫描：预读熵字节 → 喂 codec → 解码。
fnc jpg_decode_scan: i4, dec: jpg_dec&, br: jpg_br&
    var sbuf: & = nil
    var slen: u8 = 0
    var mk: i4 = 0
    if jpg_read_scan(br, &sbuf, &slen, &mk) < 0
        return -1
    dec->held = mk
    codec_jdec_reset(dec->jd)
    var i: i4 = 0
    while i < dec->img_n
        dec->comp[i].dc_pred = 0
        i = i + 1
    codec_jdec_feed(dec->jd, sbuf, slen)
    recycle((sbuf: &))
    if dec->progressive == 0
        return jpg_decode_baseline(dec)
    return jpg_decode_prog(dec)

# ============================ 容器主循环 ============================

# 仅解析到首个 SOF：填 img_w/img_h/img_n（不分配平面），供 jpg_shape 用。
fnc jpg_scan_header: i4, dec: jpg_dec&, br: jpg_br&
    var m0: i4 = jpg_read_marker(dec, br)
    if m0 != 0xD8
        return -1
    var m: i4 = jpg_read_marker(dec, br)
    while m != 0xD9
        if m < 0
            return -1
        if m == 0xC0 || m == 0xC1 || m == 0xC2
            var seglen: i4 = jpg_br_get16(br)
            var prec: i4 = jpg_br_get8(br)
            if prec != 8
                return -1
            dec->img_h = jpg_br_get16(br)
            dec->img_w = jpg_br_get16(br)
            var nc: i4 = jpg_br_get8(br)
            if nc != 1 && nc != 3 && nc != 4
                return -1
            dec->img_n = nc
            return 0
        else if m == 0xDA
            return -1
        else if m == 0x01 || (m >= 0xD0 && m <= 0xD7)
            m = jpg_read_marker(dec, br)
            continue
        else
            if jpg_skip_segment(br) < 0
                return -1
        m = jpg_read_marker(dec, br)
    return -1

fnc jpg_decode: i4, dec: jpg_dec&, br: jpg_br&
    var m0: i4 = jpg_read_marker(dec, br)
    if m0 != 0xD8
        return -1
    var m: i4 = jpg_read_marker(dec, br)
    while m != 0xD9
        if m < 0
            return -1
        if m == 0xC0 || m == 0xC1
            dec->progressive = 0
            if jpg_process_sof(dec, br) < 0
                return -1
        else if m == 0xC2
            dec->progressive = 1
            if jpg_process_sof(dec, br) < 0
                return -1
        else if m == 0xC4
            if jpg_process_dht(dec, br) < 0
                return -1
        else if m == 0xDB
            if jpg_process_dqt(dec, br) < 0
                return -1
        else if m == 0xDD
            if jpg_process_dri(dec, br) < 0
                return -1
        else if m == 0xDA
            if jpg_process_sos(dec, br) < 0
                return -1
            if jpg_decode_scan(dec, br) < 0
                return -1
        else if (m >= 0xE0 && m <= 0xEF) || m == 0xFE
            if jpg_process_app(dec, br, m) < 0
                return -1
        else if m == 0x01 || (m >= 0xD0 && m <= 0xD7)
            # TEM / 杂散 RST：无长度，忽略
            m = jpg_read_marker(dec, br)
            continue
        else
            if jpg_skip_segment(br) < 0
                return -1
        m = jpg_read_marker(dec, br)
    if dec->progressive != 0
        jpg_finish_prog(dec)
    return 0

# ============================ 上采样 ============================

fnc jpg_rs_v2: u1&, out: u1&, near: u1&, far: u1&, w: i4
    var i: i4 = 0
    while i < w
        out[i] = (((3 * (near[i]: i4) + (far[i]: i4) + 2) >> 2): u1)
        i = i + 1
    return out

fnc jpg_rs_h2: u1&, out: u1&, near: u1&, far: u1&, w: i4
    if w == 1
        out[0] = near[0]
        out[1] = near[0]
        return out
    out[0] = near[0]
    out[1] = ((((near[0]: i4) * 3 + (near[1]: i4) + 2) >> 2): u1)
    var i: i4 = 1
    while i < w - 1
        var n: i4 = 3 * (near[i]: i4) + 2
        out[i * 2] = (((n + (near[i - 1]: i4)) >> 2): u1)
        out[i * 2 + 1] = (((n + (near[i + 1]: i4)) >> 2): u1)
        i = i + 1
    out[(w - 1) * 2] = ((((near[w - 2]: i4) * 3 + (near[w - 1]: i4) + 2) >> 2): u1)
    out[(w - 1) * 2 + 1] = near[w - 1]
    return out

fnc jpg_rs_hv2: u1&, out: u1&, near: u1&, far: u1&, w: i4
    if w == 1
        var v: u1 = (((3 * (near[0]: i4) + (far[0]: i4) + 2) >> 2): u1)
        out[0] = v
        out[1] = v
        return out
    var t1: i4 = 3 * (near[0]: i4) + (far[0]: i4)
    out[0] = (((t1 + 2) >> 2): u1)
    var i: i4 = 1
    while i < w
        var t0: i4 = t1
        t1 = 3 * (near[i]: i4) + (far[i]: i4)
        out[i * 2 - 1] = (((3 * t0 + t1 + 8) >> 4): u1)
        out[i * 2] = (((3 * t1 + t0 + 8) >> 4): u1)
        i = i + 1
    out[w * 2 - 1] = (((t1 + 2) >> 2): u1)
    return out

fnc jpg_rs_generic: u1&, out: u1&, near: u1&, w: i4, hs: i4
    var i: i4 = 0
    while i < w
        var j: i4 = 0
        while j < hs
            out[i * hs + j] = near[i]
            j = j + 1
        i = i + 1
    return out

# ============================ 颜色变换辅助 ============================

fnc jpg_compute_y: u1, r: i4, g: i4, b: i4
    return (((r * 77 + g * 150 + b * 29) >> 8): u1)

fnc jpg_blinn: u1, x: i4, y: i4
    var t: i4 = x * y + 128
    return (((t + (t >> 8)) >> 8): u1)

# YCbCr → RGB 一行（定点；out 步进 step）。
fnc jpg_ycbcr_row: out: u1&, y: u1&, pcb: u1&, pcr: u1&, count: i4, step: i4
    var i: i4 = 0
    var o: i4 = 0
    while i < count
        var yf: i4 = ((y[i]: i4) << 20) + (1 << 19)
        var cr: i4 = (pcr[i]: i4) - 128
        var cb: i4 = (pcb[i]: i4) - 128
        var r: i4 = yf + cr * 1470464
        var g: i4 = yf + cr * (-748800) + (((cb * (-360960)) >> 16) << 16)
        var b: i4 = yf + cb * 1858048
        r = r >> 20
        g = g >> 20
        b = b >> 20
        out[o] = jpg_clamp(r)
        out[o + 1] = jpg_clamp(g)
        out[o + 2] = jpg_clamp(b)
        if step == 4
            out[o + 3] = 255
        o = o + step
        i = i + 1
    return

# ============================ 组装输出（上采样 + 颜色）============================

fnc jpg_assemble: i4, dec: jpg_dec&, req_comp: i4, flip: i4, out_pixels: &&, out_n: i4&
    var iw: i4 = dec->img_w
    var ih: i4 = dec->img_h
    var n: i4 = req_comp
    if n == 0
        if dec->img_n >= 3
            n = 3
        else
            n = 1
    var is_rgb: i4 = 0
    if dec->img_n == 3 && (dec->rgb == 3 || (dec->app14 == 0 && dec->jfif == 0))
        is_rgb = 1
    var decode_n: i4 = dec->img_n
    if dec->img_n == 3 && n < 3 && is_rgb == 0
        decode_n = 1
    # 上采样初始化
    var k: i4 = 0
    while k < decode_n
        var cp: jpg_comp& = &dec->comp[k]
        cp->linebuf = (chunk((iw + 3: u8)): u1&)
        if cp->linebuf == nil
            return -1
        cp->hs = dec->h_max / cp->h
        cp->vs = dec->v_max / cp->v
        cp->ystep = cp->vs >> 1
        cp->wlores = (iw + cp->hs - 1) / cp->hs
        cp->ypos = 0
        cp->line0 = cp->data
        cp->line1 = cp->data
        k = k + 1
    var outbuf: u1& = (chunk((n * iw * ih: u8)): u1&)
    if outbuf == nil
        return -1
    var coutput[4]: u1&
    var j: i4 = 0
    while j < ih
        var srow: i4 = j
        if flip != 0
            srow = ih - 1 - j
        var out: u1& = (&outbuf[n * iw * srow]: u1&)
        var k2: i4 = 0
        while k2 < decode_n
            var cp: jpg_comp& = &dec->comp[k2]
            var ybot: i4 = 0
            if cp->ystep >= (cp->vs >> 1)
                ybot = 1
            var near: u1& = cp->line0
            var far: u1& = cp->line1
            if ybot != 0
                near = cp->line1
                far = cp->line0
            if cp->hs == 1 && cp->vs == 1
                coutput[k2] = cp->line0
            else if cp->hs == 1 && cp->vs == 2
                coutput[k2] = jpg_rs_v2(cp->linebuf, near, far, cp->wlores)
            else if cp->hs == 2 && cp->vs == 1
                coutput[k2] = jpg_rs_h2(cp->linebuf, near, far, cp->wlores)
            else if cp->hs == 2 && cp->vs == 2
                coutput[k2] = jpg_rs_hv2(cp->linebuf, near, far, cp->wlores)
            else
                coutput[k2] = jpg_rs_generic(cp->linebuf, near, cp->wlores, cp->hs)
            cp->ystep = cp->ystep + 1
            if cp->ystep >= cp->vs
                cp->ystep = 0
                cp->line0 = cp->line1
                cp->ypos = cp->ypos + 1
                if cp->ypos < cp->y
                    cp->line1 = (&cp->line1[cp->w2]: u1&)
            k2 = k2 + 1
        # 颜色变换
        jpg_color_row(dec, out, (&coutput[0]: &), n, is_rgb, iw)
        j = j + 1
    out_pixels[0] = (outbuf: &)
    out_n[0] = n
    return 0

# 单行颜色变换：coarr 为 coutput[4]（u1& 数组）指针。
fnc jpg_color_row: dec: jpg_dec&, out: u1&, coarr: &, n: i4, is_rgb: i4, iw: i4
    var co: u1&& = (coarr: u1&&)      # co[0..3] 为各分量已上采样行
    var y0: u1& = (co[0]: u1&)
    var i: i4 = 0
    if n >= 3
        if dec->img_n == 3
            if is_rgb != 0
                var c1: u1& = (co[1]: u1&)
                var c2: u1& = (co[2]: u1&)
                var o: i4 = 0
                while i < iw
                    out[o] = y0[i]
                    out[o + 1] = c1[i]
                    out[o + 2] = c2[i]
                    if n == 4
                        out[o + 3] = 255
                    o = o + n
                    i = i + 1
            else
                jpg_ycbcr_row(out, y0, (co[1]: u1&), (co[2]: u1&), iw, n)
        else if dec->img_n == 4
            var c0: u1& = (co[0]: u1&)
            var c1: u1& = (co[1]: u1&)
            var c2: u1& = (co[2]: u1&)
            var c3: u1& = (co[3]: u1&)
            if dec->app14 == 2
                jpg_ycbcr_row(out, y0, c1, c2, iw, n)
                var o: i4 = 0
                while i < iw
                    var mm: i4 = (c3[i]: i4)
                    out[o] = jpg_blinn(255 - (out[o]: i4), mm)
                    out[o + 1] = jpg_blinn(255 - (out[o + 1]: i4), mm)
                    out[o + 2] = jpg_blinn(255 - (out[o + 2]: i4), mm)
                    o = o + n
                    i = i + 1
            else if dec->app14 == 0
                var o: i4 = 0
                while i < iw
                    var mm: i4 = (c3[i]: i4)
                    out[o] = jpg_blinn((c0[i]: i4), mm)
                    out[o + 1] = jpg_blinn((c1[i]: i4), mm)
                    out[o + 2] = jpg_blinn((c2[i]: i4), mm)
                    if n == 4
                        out[o + 3] = 255
                    o = o + n
                    i = i + 1
            else
                jpg_ycbcr_row(out, y0, c1, c2, iw, n)
        else
            var o: i4 = 0
            while i < iw
                out[o] = y0[i]
                out[o + 1] = y0[i]
                out[o + 2] = y0[i]
                if n == 4
                    out[o + 3] = 255
                o = o + n
                i = i + 1
    else
        if is_rgb != 0
            var c1: u1& = (co[1]: u1&)
            var c2: u1& = (co[2]: u1&)
            if n == 1
                while i < iw
                    out[i] = jpg_compute_y((y0[i]: i4), (c1[i]: i4), (c2[i]: i4))
                    i = i + 1
            else
                var o: i4 = 0
                while i < iw
                    out[o] = jpg_compute_y((y0[i]: i4), (c1[i]: i4), (c2[i]: i4))
                    out[o + 1] = 255
                    o = o + 2
                    i = i + 1
        else if dec->img_n == 4 && dec->app14 == 0
            var c0: u1& = (co[0]: u1&)
            var c1: u1& = (co[1]: u1&)
            var c2: u1& = (co[2]: u1&)
            var c3: u1& = (co[3]: u1&)
            var o: i4 = 0
            while i < iw
                var mm: i4 = (c3[i]: i4)
                var rr: i4 = (jpg_blinn((c0[i]: i4), mm): i4)
                var gg: i4 = (jpg_blinn((c1[i]: i4), mm): i4)
                var bb: i4 = (jpg_blinn((c2[i]: i4), mm): i4)
                out[o] = jpg_compute_y(rr, gg, bb)
                if n == 2
                    out[o + 1] = 255
                o = o + n
                i = i + 1
        else if dec->img_n == 4 && dec->app14 == 2
            var c0: u1& = (co[0]: u1&)
            var c3: u1& = (co[3]: u1&)
            var o: i4 = 0
            while i < iw
                out[o] = jpg_blinn(255 - (c0[i]: i4), (c3[i]: i4))
                if n == 2
                    out[o + 1] = 255
                o = o + n
                i = i + 1
        else
            if n == 1
                while i < iw
                    out[i] = y0[i]
                    i = i + 1
            else
                var o: i4 = 0
                while i < iw
                    out[o] = y0[i]
                    out[o + 1] = 255
                    o = o + 2
                    i = i + 1
    return

# ============================ 生命周期 ============================

fnc jpg_dec_setup: i4, dec: jpg_dec&
    dec->img_w = 0
    dec->img_h = 0
    dec->img_n = 0
    dec->h_max = 1
    dec->v_max = 1
    dec->scan_n = 0
    dec->spec_start = 0
    dec->spec_end = 0
    dec->succ_high = 0
    dec->succ_low = 0
    dec->progressive = 0
    dec->restart_interval = 0
    dec->rgb = 0
    dec->jfif = 0
    dec->app14 = -1
    dec->held = -1
    dec->eof_inj = 0
    var i: i4 = 0
    while i < 4
        dec->comp[i].data = nil
        dec->comp[i].coeff = nil
        dec->comp[i].linebuf = nil
        i = i + 1
    dec->jd = chunk(codec_jdec_size())
    if dec->jd == nil
        return -1
    codec_jdec_init(dec->jd)
    return 0

fnc jpg_dec_cleanup: dec: jpg_dec&
    var i: i4 = 0
    while i < 4
        if dec->comp[i].data != nil
            recycle((dec->comp[i].data: &))
            dec->comp[i].data = nil
        if dec->comp[i].coeff != nil
            recycle((dec->comp[i].coeff: &))
            dec->comp[i].coeff = nil
        if dec->comp[i].linebuf != nil
            recycle((dec->comp[i].linebuf: &))
            dec->comp[i].linebuf = nil
        i = i + 1
    if dec->jd != nil
        codec_jdec_free(dec->jd)
        recycle((dec->jd: &))
        dec->jd = nil
    return

# ============================ 编码：FDCT + 量化 + 驱动 codec ============================

# 浮点 AAN FDCT（stb 8 点，就地）。作用于 blk 的 8 个元素（步进 stp）。
fnc jpg_fdct8: blk: f4&, o0: i4, stp: i4
    var d0: f4 = blk[o0]
    var d1: f4 = blk[o0 + stp]
    var d2: f4 = blk[o0 + stp * 2]
    var d3: f4 = blk[o0 + stp * 3]
    var d4: f4 = blk[o0 + stp * 4]
    var d5: f4 = blk[o0 + stp * 5]
    var d6: f4 = blk[o0 + stp * 6]
    var d7: f4 = blk[o0 + stp * 7]
    var tmp0: f4 = d0 + d7
    var tmp7: f4 = d0 - d7
    var tmp1: f4 = d1 + d6
    var tmp6: f4 = d1 - d6
    var tmp2: f4 = d2 + d5
    var tmp5: f4 = d2 - d5
    var tmp3: f4 = d3 + d4
    var tmp4: f4 = d3 - d4
    var tmp10: f4 = tmp0 + tmp3
    var tmp13: f4 = tmp0 - tmp3
    var tmp11: f4 = tmp1 + tmp2
    var tmp12: f4 = tmp1 - tmp2
    var r0: f4 = tmp10 + tmp11
    var r4: f4 = tmp10 - tmp11
    var z1: f4 = (tmp12 + tmp13) * 0.707106781f
    var r2: f4 = tmp13 + z1
    var r6: f4 = tmp13 - z1
    tmp10 = tmp4 + tmp5
    tmp11 = tmp5 + tmp6
    tmp12 = tmp6 + tmp7
    var z5: f4 = (tmp10 - tmp12) * 0.382683433f
    var z2: f4 = tmp10 * 0.541196100f + z5
    var z4: f4 = tmp12 * 1.306562965f + z5
    var z3: f4 = tmp11 * 0.707106781f
    var z11: f4 = tmp7 + z3
    var z13: f4 = tmp7 - z3
    blk[o0 + stp * 5] = z13 + z2
    blk[o0 + stp * 3] = z13 - z2
    blk[o0 + stp] = z11 + z4
    blk[o0 + stp * 7] = z11 - z4
    blk[o0] = r0
    blk[o0 + stp * 2] = r2
    blk[o0 + stp * 4] = r4
    blk[o0 + stp * 6] = r6
    return

# 处理一个 8x8 块（CDU 步进 stride）：FDCT + 量化 + zigzag → du[64]（zigzag 序）→ codec 熵编码 → drain 到 com。
fnc jpg_enc_du: i4, enc: &, cdu: f4&, du_stride: i4, fdtbl: f4&, dc_tbl: i4, ac_tbl: i4, dcp: i4&, c: com&, drainbuf: u1&, dcap: u8
    # DCT 行
    var r: i4 = 0
    while r < 8
        jpg_fdct8(cdu, r * du_stride, 1)
        r = r + 1
    # DCT 列
    var cc: i4 = 0
    while cc < 8
        jpg_fdct8(cdu, cc, du_stride)
        cc = cc + 1
    # 量化 + zigzag
    var du[64]: i4
    var y: i4 = 0
    var jn: i4 = 0
    while y < 8
        var x: i4 = 0
        while x < 8
            var v: f4 = cdu[y * du_stride + x] * fdtbl[jn]
            var q: i4
            if v < 0.0f
                q = ((v - 0.5f): i4)
            else
                q = ((v + 0.5f): i4)
            du[jpg_zig[jn]] = q
            x = x + 1
            jn = jn + 1
        y = y + 1
    if codec_jenc_block(enc, (&du[0]: i4&), dc_tbl, ac_tbl, dcp) < 0
        return -1
    # drain
    while 1
        var got: i8 = codec_jenc_drain(enc, drainbuf, dcap)
        if got <= 0
            break
        if jpg_wr(c, (drainbuf: &), (got: u8)) < 0
            return -1
    return 0

# 写标准 Huffman 表段辅助（DHT 单表）：写 Tc/Th、counts[16]、values。
fnc jpg_write_hdr: i4, c: com&, buf: u1&, n: u8
    return jpg_wr(c, (buf: &), n)

fnc jpg_encode: i4, c: com&, info: img&, quality: i4, flip: i4
    var comp: i4 = info->channels
    if comp < 1 || comp > 4
        return -1
    var w: i4 = info->width
    var h: i4 = info->height
    if w <= 0 || h <= 0
        return -1
    var q: i4 = quality
    if q <= 0
        q = 90
    var subsample: i4 = 0
    if q <= 90
        subsample = 1
    if q > 100
        q = 100
    if q < 1
        q = 1
    if q < 50
        q = 5000 / q
    else
        q = 200 - q * 2
    # 量化表（zigzag 序，钳 1..255）
    var ytab[64]: u1
    var uvtab[64]: u1
    var i: i4 = 0
    while i < 64
        var yti: i4 = (jpg_yqt[i] * q + 50) / 100
        if yti < 1
            yti = 1
        if yti > 255
            yti = 255
        ytab[jpg_zig[i]] = (yti: u1)
        var uti: i4 = (jpg_uvqt[i] * q + 50) / 100
        if uti < 1
            uti = 1
        if uti > 255
            uti = 255
        uvtab[jpg_zig[i]] = (uti: u1)
        i = i + 1
    # fdtbl（行主序）= 1/(table[zigzag] * aasf[row] * aasf[col])
    var fy[64]: f4
    var fuv[64]: f4
    var aasf[8]: f4
    i = 0
    while i < 8
        aasf[i] = jpg_aasf_base[i] * 2.828427125f
        i = i + 1
    var row: i4 = 0
    var kk: i4 = 0
    while row < 8
        var col: i4 = 0
        while col < 8
            fy[kk] = 1.0f / ((ytab[jpg_zig[kk]]: f4) * aasf[row] * aasf[col])
            fuv[kk] = 1.0f / ((uvtab[jpg_zig[kk]]: f4) * aasf[row] * aasf[col])
            col = col + 1
            kk = kk + 1
        row = row + 1
    # 初始化编码器 + 交标准表
    var enc: & = chunk(codec_jenc_size())
    if enc == nil
        return -1
    codec_jenc_init(enc)
    codec_jenc_dht(enc, 0, 0, (&jpg_dc_lum_cnt[0]: u1&), (&jpg_dc_lum_val[0]: u1&))
    codec_jenc_dht(enc, 1, 0, (&jpg_ac_lum_cnt[0]: u1&), (&jpg_ac_lum_val[0]: u1&))
    codec_jenc_dht(enc, 0, 1, (&jpg_dc_chr_cnt[0]: u1&), (&jpg_dc_chr_val[0]: u1&))
    codec_jenc_dht(enc, 1, 1, (&jpg_ac_chr_cnt[0]: u1&), (&jpg_ac_chr_val[0]: u1&))
    # ---- 写文件头 ----
    var head0[25]: u1
    head0[0] = 0xFF
    head0[1] = 0xD8
    head0[2] = 0xFF
    head0[3] = 0xE0
    head0[4] = 0
    head0[5] = 0x10
    head0[6] = 74
    head0[7] = 70
    head0[8] = 73
    head0[9] = 70
    head0[10] = 0
    head0[11] = 1
    head0[12] = 1
    head0[13] = 0
    head0[14] = 0
    head0[15] = 1
    head0[16] = 0
    head0[17] = 1
    head0[18] = 0
    head0[19] = 0
    head0[20] = 0xFF
    head0[21] = 0xDB
    head0[22] = 0
    head0[23] = 0x84
    head0[24] = 0
    if jpg_wr(c, (&head0[0]: &), 25) < 0
        return -1
    if jpg_wr(c, (&ytab[0]: &), 64) < 0
        return -1
    var one[1]: u1
    one[0] = 1
    if jpg_wr(c, (&one[0]: &), 1) < 0
        return -1
    if jpg_wr(c, (&uvtab[0]: &), 64) < 0
        return -1
    var head1[24]: u1
    head1[0] = 0xFF
    head1[1] = 0xC0
    head1[2] = 0
    head1[3] = 0x11
    head1[4] = 8
    head1[5] = ((h >> 8) & 255: u1)
    head1[6] = (h & 255: u1)
    head1[7] = ((w >> 8) & 255: u1)
    head1[8] = (w & 255: u1)
    head1[9] = 3
    head1[10] = 1
    if subsample != 0
        head1[11] = 0x22
    else
        head1[11] = 0x11
    head1[12] = 0
    head1[13] = 2
    head1[14] = 0x11
    head1[15] = 1
    head1[16] = 3
    head1[17] = 0x11
    head1[18] = 1
    head1[19] = 0xFF
    head1[20] = 0xC4
    head1[21] = 0x01
    head1[22] = 0xA2
    head1[23] = 0
    if jpg_wr(c, (&head1[0]: &), 24) < 0
        return -1
    # DHT 表体：DC 亮度
    if jpg_wr(c, (&jpg_dc_lum_cnt[0]: &), 16) < 0
        return -1
    if jpg_wr(c, (&jpg_dc_lum_val[0]: &), 12) < 0
        return -1
    var b10[1]: u1
    b10[0] = 0x10
    if jpg_wr(c, (&b10[0]: &), 1) < 0
        return -1
    if jpg_wr(c, (&jpg_ac_lum_cnt[0]: &), 16) < 0
        return -1
    if jpg_wr(c, (&jpg_ac_lum_val[0]: &), 162) < 0
        return -1
    var b1[1]: u1
    b1[0] = 1
    if jpg_wr(c, (&b1[0]: &), 1) < 0
        return -1
    if jpg_wr(c, (&jpg_dc_chr_cnt[0]: &), 16) < 0
        return -1
    if jpg_wr(c, (&jpg_dc_chr_val[0]: &), 12) < 0
        return -1
    var b11[1]: u1
    b11[0] = 0x11
    if jpg_wr(c, (&b11[0]: &), 1) < 0
        return -1
    if jpg_wr(c, (&jpg_ac_chr_cnt[0]: &), 16) < 0
        return -1
    if jpg_wr(c, (&jpg_ac_chr_val[0]: &), 162) < 0
        return -1
    var head2[14]: u1
    head2[0] = 0xFF
    head2[1] = 0xDA
    head2[2] = 0
    head2[3] = 0x0C
    head2[4] = 3
    head2[5] = 1
    head2[6] = 0
    head2[7] = 2
    head2[8] = 0x11
    head2[9] = 3
    head2[10] = 0x11
    head2[11] = 0
    head2[12] = 0x3F
    head2[13] = 0
    if jpg_wr(c, (&head2[0]: &), 14) < 0
        return -1
    # ---- 编码 MCU ----
    var pix: u1& = info->pixels
    var ofsg: i4 = 0
    var ofsb: i4 = 0
    if comp > 2
        ofsg = 1
        ofsb = 2
    var dcy: i4 = 0
    var dcu: i4 = 0
    var dcv: i4 = 0
    var dcap: u8 = 4096
    var drainbuf: u1& = (chunk(dcap): u1&)
    if drainbuf == nil
        recycle((enc: &))
        return -1
    var rc: i4 = 0
    if subsample != 0
        var yb[256]: f4
        var ub[256]: f4
        var vb[256]: f4
        var suba[64]: f4
        var subb[64]: f4
        var yy: i4 = 0
        while yy < h && rc == 0
            var xx: i4 = 0
            while xx < w && rc == 0
                # 采集 16x16
                var rr: i4 = 0
                var pos: i4 = 0
                while rr < 16
                    var crow: i4 = yy + rr
                    if crow >= h
                        crow = h - 1
                    var srow: i4 = crow
                    if flip != 0
                        srow = h - 1 - crow
                    var basep: i4 = srow * w * comp
                    var col: i4 = 0
                    while col < 16
                        var ccol: i4 = xx + col
                        if ccol >= w
                            ccol = w - 1
                        var p: i4 = basep + ccol * comp
                        var vr: f4 = (pix[p]: f4)
                        var vg: f4 = (pix[p + ofsg]: f4)
                        var vb2: f4 = (pix[p + ofsb]: f4)
                        yb[pos] = 0.299f * vr + 0.587f * vg + 0.114f * vb2 - 128.0f
                        ub[pos] = (-0.16874f) * vr - 0.33126f * vg + 0.5f * vb2
                        vb[pos] = 0.5f * vr - 0.41869f * vg - 0.08131f * vb2
                        pos = pos + 1
                        col = col + 1
                    rr = rr + 1
                # 4 个 Y 子块
                if jpg_enc_du(enc, (&yb[0]: f4&), 16, (&fy[0]: f4&), 0, 0, &dcy, c, drainbuf, dcap) < 0
                    rc = -1
                if rc == 0 && jpg_enc_du(enc, (&yb[8]: f4&), 16, (&fy[0]: f4&), 0, 0, &dcy, c, drainbuf, dcap) < 0
                    rc = -1
                if rc == 0 && jpg_enc_du(enc, (&yb[128]: f4&), 16, (&fy[0]: f4&), 0, 0, &dcy, c, drainbuf, dcap) < 0
                    rc = -1
                if rc == 0 && jpg_enc_du(enc, (&yb[136]: f4&), 16, (&fy[0]: f4&), 0, 0, &dcy, c, drainbuf, dcap) < 0
                    rc = -1
                # 下采样 U,V 到 8x8
                var yq: i4 = 0
                var sp: i4 = 0
                while yq < 8
                    var xq: i4 = 0
                    while xq < 8
                        var jj: i4 = yq * 32 + xq * 2
                        suba[sp] = (ub[jj] + ub[jj + 1] + ub[jj + 16] + ub[jj + 17]) * 0.25f
                        subb[sp] = (vb[jj] + vb[jj + 1] + vb[jj + 16] + vb[jj + 17]) * 0.25f
                        sp = sp + 1
                        xq = xq + 1
                    yq = yq + 1
                if rc == 0 && jpg_enc_du(enc, (&suba[0]: f4&), 8, (&fuv[0]: f4&), 1, 1, &dcu, c, drainbuf, dcap) < 0
                    rc = -1
                if rc == 0 && jpg_enc_du(enc, (&subb[0]: f4&), 8, (&fuv[0]: f4&), 1, 1, &dcv, c, drainbuf, dcap) < 0
                    rc = -1
                xx = xx + 16
            yy = yy + 16
    else
        var yb[64]: f4
        var ub[64]: f4
        var vb[64]: f4
        var yy: i4 = 0
        while yy < h && rc == 0
            var xx: i4 = 0
            while xx < w && rc == 0
                var rr: i4 = 0
                var pos: i4 = 0
                while rr < 8
                    var crow: i4 = yy + rr
                    if crow >= h
                        crow = h - 1
                    var srow: i4 = crow
                    if flip != 0
                        srow = h - 1 - crow
                    var basep: i4 = srow * w * comp
                    var col: i4 = 0
                    while col < 8
                        var ccol: i4 = xx + col
                        if ccol >= w
                            ccol = w - 1
                        var p: i4 = basep + ccol * comp
                        var vr: f4 = (pix[p]: f4)
                        var vg: f4 = (pix[p + ofsg]: f4)
                        var vb2: f4 = (pix[p + ofsb]: f4)
                        yb[pos] = 0.299f * vr + 0.587f * vg + 0.114f * vb2 - 128.0f
                        ub[pos] = (-0.16874f) * vr - 0.33126f * vg + 0.5f * vb2
                        vb[pos] = 0.5f * vr - 0.41869f * vg - 0.08131f * vb2
                        pos = pos + 1
                        col = col + 1
                    rr = rr + 1
                if jpg_enc_du(enc, (&yb[0]: f4&), 8, (&fy[0]: f4&), 0, 0, &dcy, c, drainbuf, dcap) < 0
                    rc = -1
                if rc == 0 && jpg_enc_du(enc, (&ub[0]: f4&), 8, (&fuv[0]: f4&), 1, 1, &dcu, c, drainbuf, dcap) < 0
                    rc = -1
                if rc == 0 && jpg_enc_du(enc, (&vb[0]: f4&), 8, (&fuv[0]: f4&), 1, 1, &dcv, c, drainbuf, dcap) < 0
                    rc = -1
                xx = xx + 8
            yy = yy + 8
    if rc == 0
        codec_jenc_flush(enc)
        while 1
            var got: i8 = codec_jenc_drain(enc, drainbuf, dcap)
            if got <= 0
                break
            if jpg_wr(c, (drainbuf: &), (got: u8)) < 0
                rc = -1
                break
    recycle((drainbuf: &))
    codec_jenc_free(enc)
    recycle((enc: &))
    if rc != 0
        return -1
    # EOI
    var eoi[2]: u1
    eoi[0] = 0xFF
    eoi[1] = 0xD9
    if jpg_wr(c, (&eoi[0]: &), 2) < 0
        return -1
    return 0
