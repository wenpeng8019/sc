# img_jpg_codec —— JPEG 熵扫描流式编解码（纯 sc 实现）
#
# 定位：**JPEG 专属熵层，非通用 codec**（区别于 builtins/codec 的 Huffman/rANS/DEFLATE 等通用原子）。
#   原属 builtins/codec 簇 9，因其满身 JPEG 印记（0xFF 塞字节、DC/AC (run,size)、dezigzag、
#   ZRL/EOB、RSTn 重启、progressive 谱选择+逐次逼近）而迁出，随 img_jpg 就近维护。
#
# 组织：由 img_jpg.sc 经 `add img_jpg_codec.sc` 内联为子模块（本文件全部为内部 fnc，不导出）。
#
# 职责边界：只做位 I/O + Huffman + 逐块熵解码/编码 + 重启标记；MCU 几何 / IDCT / FDCT / 量化 /
#   上采样 / 颜色变换全归 img_jpg。块级即最小粒度，无独立非流式接口（整段解码 = 一次喂满、
#   need_more 永不触发；真正的整扫描一把梭需 MCU 几何，属 img 职责，故不提供）。
#
# 参考 stb_image.h / stb_image_write.h 的 JPEG 熵层，改造为流式 feed/drain + 块级回滚。
# 流式回滚不用 goto，改「wrapper 存快照 / core 早返 0 / wrapper 回滚」等价 stb 的 goto suspend。

inc mem.sc

# ─────────────────── 结构体 ───────────────────
# JPEG Huffman 解码表（size=各码长符号数展开；code 递增；fast 为 9bit 加速表）。
def cj_huff: {
    fast[512]:   u1
    code[256]:   u2
    values[256]: u1
    size[257]:   u1
    maxcode[18]: u4
    delta[17]:   i4
}

# 解码器状态（位读态 + 内部输入缓冲）。fast_ac 展平为 4×512。
def codec_jdec: {
    huff_dc[4]:    cj_huff
    huff_ac[4]:    cj_huff
    fast_ac[2048]: i2      # 4 表 × 512（行基址 = tbl*512）
    code_buffer:   u4
    code_bits:     i4
    marker:        u1      # 遇到的 marker XX（0xff=无）
    nomore:        i4      # 已见 marker：后续 grow 填 0
    need_more:     i4      # 喂入字节不足（可回滚）
    eob_run:       i4      # progressive AC 的 end-of-band 游程
    inbuf:         u1&
    incap:         u8
    inlen:         u8
    bytepos:       u8
}

# JPEG Huffman 编码表。
def cj_ehuff: {
    code[256]: u2
    len[256]:  u1
}

# 编码器状态（位写态 + 内部输出缓冲）。
def codec_jenc: {
    edc[4]: cj_ehuff
    eac[4]: cj_ehuff
    bitBuf: i4
    bitCnt: i4
    ob:     u1&
    obcap:  u8
    oblen:  u8
    obpos:  u8
    oom:    i4
}

# ─────────────────── 常量表 ───────────────────
# (1<<n)-1
var cj_bmask[17]: u4 = [0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535]
# bias[n] = (-1<<n)+1
var cj_jbias[16]: i4 = [0,-1,-3,-7,-15,-31,-63,-127,-255,-511,-1023,-2047,-4095,-8191,-16383,-32767]
# zigzag 位置 → 8x8 行主序位置（末尾 15 个 63 容错越界）
var cj_dezigzag[79]: u1 = [
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
    12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63,
    63,63,63,63,63,63,63,63,63,63,63,63,63,63,63
]

# ─────────────────── 字节工具（替代 memset/memcpy/memmove）───────────────────
fnc cj_bzero: p: u1&, n: u8
    var i: u8 = 0
    while i < n
        p[i] = 0
        i = i + 1
    return

fnc cj_bfill: p: u1&, val: u1, n: u8
    var i: u8 = 0
    while i < n
        p[i] = val
        i = i + 1
    return

# 正向字节拷贝（dst <= src 时对重叠安全，覆盖 memcpy 与本模块 memmove 用法）。
fnc cj_bcopy: dst: u1&, src: u1&, n: u8
    var i: u8 = 0
    while i < n
        dst[i] = src[i]
        i = i + 1
    return

# 清零 n 个 i2 系数。
fnc cj_zero16: data: i2&, n: i4
    var i: i4 = 0
    while i < n
        data[i] = 0
        i = i + 1
    return

# 32 位循环左移（cj_lrot 宏）。
fnc cj_lrot: u4, x: u4, y: i4
    return (x << y) | (x >> ((0 - y) & 31))

# ─────────────────── Huffman 建表 ───────────────────
# 建 JPEG Huffman 解码表。返回 0/-1。
fnc cj_build_huffman: i4, h: cj_huff&, count: i4&
    var k: i4 = 0
    var i: i4 = 0
    while i < 16
        var j: i4 = 0
        while j < count[i]
            h->size[k] = ((i + 1): u1)
            k = k + 1
            if k >= 257
                return -1
            j = j + 1
        i = i + 1
    h->size[k] = 0
    var code: u4 = 0
    k = 0
    var jj: i4 = 1
    while jj <= 16
        h->delta[jj] = k - (code: i4)
        if (h->size[k]: i4) == jj
            while (h->size[k]: i4) == jj
                h->code[k] = (code: u2)
                code = code + 1
                k = k + 1
            if code - 1 >= ((1: u4) << jj)
                return -1
        h->maxcode[jj] = code << (16 - jj)
        code = code << 1
        jj = jj + 1
    h->maxcode[17] = 0xffffffff
    cj_bfill((&h->fast[0]: u1&), 255, 512)
    i = 0
    while i < k
        var s: i4 = (h->size[i]: i4)
        if s <= 9
            var c: i4 = (h->code[i]: i4) << (9 - s)
            var m: i4 = 1 << (9 - s)
            var j2: i4 = 0
            while j2 < m
                h->fast[c + j2] = (i: u1)
                j2 = j2 + 1
        i = i + 1
    return 0

# AC 小值一次解出加速表（run/mag 合并进 fast_ac）。
fnc cj_build_fast_ac: fast_ac: i2&, h: cj_huff&
    var i: i4 = 0
    while i < 512
        var fast: i4 = (h->fast[i]: i4)
        fast_ac[i] = 0
        if fast < 255
            var rs: i4 = (h->values[fast]: i4)
            var runv: i4 = (rs >> 4) & 15
            var magbits: i4 = rs & 15
            var len: i4 = (h->size[fast]: i4)
            if magbits != 0 && len + magbits <= 9
                var k: i4 = ((i << len) & 511) >> (9 - magbits)
                var m: i4 = 1 << (magbits - 1)
                if k < m
                    k = k - (1 << magbits) + 1
                if k >= -128 && k <= 127
                    fast_ac[i] = (((k * 256) + (runv * 16) + (len + magbits)): i2)
        i = i + 1
    return

# ─────────────────── 解码：生命周期 + 交表 + 喂字节 ───────────────────
# 返回解码器状态结构字节数（供分配）。
fnc codec_jdec_size: u8
    return (sizeof(codec_jdec): u8)

# 初始化解码器（清零 + marker=none）；返回 0。
fnc codec_jdec_init: i4, sp: &
    var j: codec_jdec& = (sp: codec_jdec&)
    cj_bzero((sp: u1&), (sizeof(codec_jdec): u8))
    j->marker = 0xff
    return 0

# 释放内部输入缓冲。
fnc codec_jdec_free: sp: &
    var j: codec_jdec& = (sp: codec_jdec&)
    if j->inbuf != nil
        recycle((j->inbuf: &))
        j->inbuf = nil
        j->incap = 0
        j->inlen = 0
        j->bytepos = 0
    return

# 交 Huffman 表：tc 0=DC/1=AC；th 0..3；counts[16] 各码长符号数；values 符号表。返回 0/-1。
fnc codec_jdec_dht: i4, sp: &, tc: i4, th: i4, counts: u1&, values: u1&
    var j: codec_jdec& = (sp: codec_jdec&)
    if tc < 0 || tc > 1 || th < 0 || th > 3
        return -1
    var cc[16]: i4
    var n: i4 = 0
    var i: i4 = 0
    while i < 16
        cc[i] = (counts[i]: i4)
        n = n + cc[i]
        i = i + 1
    if n > 256
        return -1
    var h: cj_huff& = &j->huff_dc[th]
    if tc != 0
        h = &j->huff_ac[th]
    if cj_build_huffman(h, (&cc[0]: i4&)) < 0
        return -1
    i = 0
    while i < n
        h->values[i] = values[i]
        i = i + 1
    if tc == 1
        cj_build_fast_ac((&j->fast_ac[th * 512]: i2&), h)
    return 0

# 喂熵字节：append 进 inbuf，先压实已提交 [0,bytepos)。返回吸入字节数。
fnc codec_jdec_feed: i8, sp: &, in: &, inlen: u8
    var j: codec_jdec& = (sp: codec_jdec&)
    if j->bytepos > 0
        var rem: u8 = j->inlen - j->bytepos
        if rem != 0
            cj_bcopy((j->inbuf: u1&), (&j->inbuf[j->bytepos]: u1&), rem)
        j->inlen = rem
        j->bytepos = 0
    if inlen != 0
        if j->inlen + inlen > j->incap
            var nc: u8 = j->incap != 0 ? j->incap : 4096
            while nc < j->inlen + inlen
                nc = nc * 2
            var nb: & = refit((j->inbuf: &), nc)
            if nb == nil
                return -1
            j->inbuf = (nb: u1&)
            j->incap = nc
        cj_bcopy((&j->inbuf[j->inlen]: u1&), (in: u1&), inlen)
        j->inlen = j->inlen + inlen
    return (inlen: i8)

# ─────────────────── 解码：位 I/O ───────────────────
# 从 inbuf 拉字节进 code_buffer（0xFF 塞字节 destuff + marker 检测）。
fnc cj_grow: j: codec_jdec&
    do
        var b: u4 = 0
        if j->nomore != 0
            b = 0
        else
            if j->bytepos >= j->inlen
                j->need_more = 1
                return
            b = (j->inbuf[j->bytepos]: u4)
            j->bytepos = j->bytepos + 1
            if b == 0xff
                if j->bytepos >= j->inlen
                    j->need_more = 1
                    return
                var c: i4 = (j->inbuf[j->bytepos]: i4)
                j->bytepos = j->bytepos + 1
                while c == 0xff
                    if j->bytepos >= j->inlen
                        j->need_more = 1
                        return
                    c = (j->inbuf[j->bytepos]: i4)
                    j->bytepos = j->bytepos + 1
                if c != 0
                    j->marker = (c: u1)
                    j->nomore = 1
                    return
        j->code_buffer = j->code_buffer | (b << (24 - j->code_bits))
        j->code_bits = j->code_bits + 8
    while j->code_bits <= 24
    return

# 解一个 JPEG Huffman 值。need_more 由调用方在返回后检查。
fnc cj_huff_decode: i4, j: codec_jdec&, h: cj_huff&
    if j->code_bits < 16
        cj_grow(j)
        if j->need_more != 0
            return -1
    var c: i4 = ((j->code_buffer >> 23) & 511: i4)
    var k: i4 = (h->fast[c]: i4)
    if k < 255
        var s: i4 = (h->size[k]: i4)
        if s > j->code_bits
            return -1
        j->code_buffer = j->code_buffer << s
        j->code_bits = j->code_bits - s
        return (h->values[k]: i4)
    var temp: u4 = j->code_buffer >> 16
    k = 10
    while 1
        if temp < h->maxcode[k]
            break
        k = k + 1
    if k == 17
        j->code_bits = j->code_bits - 16
        return -1
    if k > j->code_bits
        return -1
    var c2: i4 = ((j->code_buffer >> (32 - k)) & cj_bmask[k]: i4) + h->delta[k]
    if c2 < 0 || c2 >= 256
        return -1
    j->code_bits = j->code_bits - k
    j->code_buffer = j->code_buffer << k
    return (h->values[c2]: i4)

# 收 n bit 并符号扩展（JPEG receive+extend）。
fnc cj_extend_receive: i4, j: codec_jdec&, n: i4
    if n == 0
        return 0
    if j->code_bits < n
        cj_grow(j)
        if j->need_more != 0
            return 0
    if j->code_bits < n
        return 0
    var sgn: i4 = (j->code_buffer >> 31: i4)
    var k: u4 = cj_lrot(j->code_buffer, n)
    j->code_buffer = k & (~cj_bmask[n])
    k = k & cj_bmask[n]
    j->code_bits = j->code_bits - n
    return (k: i4) + (cj_jbias[n] & (sgn - 1))

fnc cj_get_bits: i4, j: codec_jdec&, n: i4
    if n == 0
        return 0
    if j->code_bits < n
        cj_grow(j)
        if j->need_more != 0
            return 0
    if j->code_bits < n
        return 0
    var k: u4 = cj_lrot(j->code_buffer, n)
    j->code_buffer = k & (~cj_bmask[n])
    k = k & cj_bmask[n]
    j->code_bits = j->code_bits - n
    return (k: i4)

fnc cj_get_bit: i4, j: codec_jdec&
    if j->code_bits < 1
        cj_grow(j)
        if j->need_more != 0
            return 0
    if j->code_bits < 1
        return 0
    var k: u4 = j->code_buffer
    j->code_buffer = j->code_buffer << 1
    j->code_bits = j->code_bits - 1
    return ((k >> 31) & 1: i4)

# ─────────────────── 解码：baseline 块 ───────────────────
# core 返回 1 完成 / 0 缺字节（need_more，wrapper 回滚）/ -1 错。
fnc cj_block_core: i4, j: codec_jdec&, data: i2&, hdc: cj_huff&, hac: cj_huff&, fac: i2&, dc_pred: i4&, sv_dc: i4
    if j->code_bits < 16
        cj_grow(j)
        if j->need_more != 0
            return 0
    var t: i4 = cj_huff_decode(j, hdc)
    if j->need_more != 0
        return 0
    if t < 0 || t > 15
        return -1
    cj_zero16(data, 64)
    var diff: i4 = 0
    if t != 0
        diff = cj_extend_receive(j, t)
        if j->need_more != 0
            return 0
    var dc: i4 = sv_dc + diff
    dc_pred[0] = dc
    data[0] = (dc: i2)
    var k: i4 = 1
    do
        var c: i4 = 0
        var r: i4 = 0
        var s: i4 = 0
        var zig: i4 = 0
        if j->code_bits < 16
            cj_grow(j)
            if j->need_more != 0
                return 0
        c = ((j->code_buffer >> 23) & 511: i4)
        r = (fac[c]: i4)
        if r != 0
            k = k + ((r >> 4) & 15)
            s = r & 15
            if s > j->code_bits
                return -1
            j->code_buffer = j->code_buffer << s
            j->code_bits = j->code_bits - s
            zig = (cj_dezigzag[k]: i4)
            k = k + 1
            data[zig] = ((r >> 8): i2)
        else
            var rs: i4 = cj_huff_decode(j, hac)
            if j->need_more != 0
                return 0
            if rs < 0
                return -1
            s = rs & 15
            r = rs >> 4
            if s == 0
                if rs != 0xf0
                    break
                k = k + 16
            else
                k = k + r
                zig = (cj_dezigzag[k]: i4)
                k = k + 1
                var val: i4 = cj_extend_receive(j, s)
                if j->need_more != 0
                    return 0
                data[zig] = (val: i2)
    while k < 64
    return 1

# baseline：解一块到 data[64]（自然序，未去量化）。返回 1 完成 / 0 缺字节(已回滚) / -1 错。
fnc codec_jdec_block: i4, sp: &, data: i2&, dc_tbl: i4, ac_tbl: i4, dc_pred: i4&
    var j: codec_jdec& = (sp: codec_jdec&)
    var hdc: cj_huff& = &j->huff_dc[dc_tbl]
    var hac: cj_huff& = &j->huff_ac[ac_tbl]
    var fac: i2& = (&j->fast_ac[ac_tbl * 512]: i2&)
    var sv_cb: u4 = j->code_buffer
    var sv_bits: i4 = j->code_bits
    var sv_pos: u8 = j->bytepos
    var sv_mk: u1 = j->marker
    var sv_nm: i4 = j->nomore
    var sv_dc: i4 = dc_pred[0]
    j->need_more = 0
    var r: i4 = cj_block_core(j, data, hdc, hac, fac, dc_pred, sv_dc)
    if r == 0
        j->code_buffer = sv_cb
        j->code_bits = sv_bits
        j->bytepos = sv_pos
        j->marker = sv_mk
        j->nomore = sv_nm
        dc_pred[0] = sv_dc
        j->need_more = 0
    return r

# ─────────────────── 解码：progressive DC 块 ───────────────────
fnc cj_block_prog_dc_core: i4, j: codec_jdec&, data: i2&, hdc: cj_huff&, dc_pred: i4&, ah: i4, al: i4, sv_dc: i4
    if j->code_bits < 16
        cj_grow(j)
        if j->need_more != 0
            return 0
    if ah == 0
        cj_zero16(data, 64)
        var t: i4 = cj_huff_decode(j, hdc)
        if j->need_more != 0
            return 0
        if t < 0 || t > 15
            return -1
        var diff: i4 = 0
        if t != 0
            diff = cj_extend_receive(j, t)
            if j->need_more != 0
                return 0
        var dc: i4 = sv_dc + diff
        dc_pred[0] = dc
        data[0] = ((dc * (1 << al)): i2)
    else
        var b: i4 = cj_get_bit(j)
        if j->need_more != 0
            return 0
        if b != 0
            data[0] = ((data[0] + (1 << al)): i2)
    return 1

# progressive DC 块（ah=succ_high, al=succ_low）。返回 1/0/-1。
fnc codec_jdec_block_prog_dc: i4, sp: &, data: i2&, dc_tbl: i4, dc_pred: i4&, ah: i4, al: i4
    var j: codec_jdec& = (sp: codec_jdec&)
    var hdc: cj_huff& = &j->huff_dc[dc_tbl]
    var sv_cb: u4 = j->code_buffer
    var sv_bits: i4 = j->code_bits
    var sv_pos: u8 = j->bytepos
    var sv_mk: u1 = j->marker
    var sv_nm: i4 = j->nomore
    var sv_dc: i4 = dc_pred[0]
    j->need_more = 0
    var r: i4 = cj_block_prog_dc_core(j, data, hdc, dc_pred, ah, al, sv_dc)
    if r == 0
        j->code_buffer = sv_cb
        j->code_bits = sv_bits
        j->bytepos = sv_pos
        j->marker = sv_mk
        j->nomore = sv_nm
        dc_pred[0] = sv_dc
        j->need_more = 0
    return r

# ─────────────────── 解码：progressive AC 块 ───────────────────
fnc cj_block_prog_ac_core: i4, j: codec_jdec&, data: i2&, hac: cj_huff&, fac: i2&, ss: i4, se: i4, ah: i4, al: i4
    var k: i4 = 0
    if ah == 0
        var shift: i4 = al
        if j->eob_run != 0
            j->eob_run = j->eob_run - 1
            return 1
        k = ss
        do
            var c: i4 = 0
            var r: i4 = 0
            var s: i4 = 0
            var zig: i4 = 0
            if j->code_bits < 16
                cj_grow(j)
                if j->need_more != 0
                    return 0
            c = ((j->code_buffer >> 23) & 511: i4)
            r = (fac[c]: i4)
            if r != 0
                k = k + ((r >> 4) & 15)
                s = r & 15
                if s > j->code_bits
                    return -1
                j->code_buffer = j->code_buffer << s
                j->code_bits = j->code_bits - s
                zig = (cj_dezigzag[k]: i4)
                k = k + 1
                data[zig] = (((r >> 8) * (1 << shift)): i2)
            else
                var rs: i4 = cj_huff_decode(j, hac)
                if j->need_more != 0
                    return 0
                if rs < 0
                    return -1
                s = rs & 15
                r = rs >> 4
                if s == 0
                    if r < 15
                        j->eob_run = 1 << r
                        if r != 0
                            j->eob_run = j->eob_run + cj_get_bits(j, r)
                            if j->need_more != 0
                                return 0
                        j->eob_run = j->eob_run - 1
                        break
                    k = k + 16
                else
                    k = k + r
                    zig = (cj_dezigzag[k]: i4)
                    k = k + 1
                    var val: i4 = cj_extend_receive(j, s)
                    if j->need_more != 0
                        return 0
                    data[zig] = ((val * (1 << shift)): i2)
        while k <= se
    else
        var bit: i4 = 1 << al
        if j->eob_run != 0
            j->eob_run = j->eob_run - 1
            k = ss
            while k <= se
                var p: i2& = &data[cj_dezigzag[k]]
                if p[0] != 0
                    var b: i4 = cj_get_bit(j)
                    if j->need_more != 0
                        return 0
                    if b != 0
                        if (p[0] & bit) == 0
                            if p[0] > 0
                                p[0] = ((p[0] + bit): i2)
                            else
                                p[0] = ((p[0] - bit): i2)
                k = k + 1
        else
            k = ss
            do
                var rs: i4 = cj_huff_decode(j, hac)
                if j->need_more != 0
                    return 0
                if rs < 0
                    return -1
                var s: i4 = rs & 15
                var r: i4 = rs >> 4
                if s == 0
                    if r < 15
                        j->eob_run = (1 << r) - 1
                        if r != 0
                            j->eob_run = j->eob_run + cj_get_bits(j, r)
                            if j->need_more != 0
                                return 0
                        r = 64
                else
                    if s != 1
                        return -1
                    var b0: i4 = cj_get_bit(j)
                    if j->need_more != 0
                        return 0
                    if b0 != 0
                        s = bit
                    else
                        s = 0 - bit
                while k <= se
                    var p: i2& = &data[cj_dezigzag[k]]
                    k = k + 1
                    if p[0] != 0
                        var b: i4 = cj_get_bit(j)
                        if j->need_more != 0
                            return 0
                        if b != 0
                            if (p[0] & bit) == 0
                                if p[0] > 0
                                    p[0] = ((p[0] + bit): i2)
                                else
                                    p[0] = ((p[0] - bit): i2)
                    else
                        if r == 0
                            p[0] = (s: i2)
                            break
                        r = r - 1
            while k <= se
    return 1

# progressive AC 块（ss=spec_start, se=spec_end, ah, al；内部维护 eob_run）。返回 1/0/-1。
fnc codec_jdec_block_prog_ac: i4, sp: &, data: i2&, ac_tbl: i4, ss: i4, se: i4, ah: i4, al: i4
    var j: codec_jdec& = (sp: codec_jdec&)
    var hac: cj_huff& = &j->huff_ac[ac_tbl]
    var fac: i2& = (&j->fast_ac[ac_tbl * 512]: i2&)
    var sv_cb: u4 = j->code_buffer
    var sv_bits: i4 = j->code_bits
    var sv_pos: u8 = j->bytepos
    var sv_mk: u1 = j->marker
    var sv_nm: i4 = j->nomore
    var sv_eob: i4 = j->eob_run
    j->need_more = 0
    var r: i4 = cj_block_prog_ac_core(j, data, hac, fac, ss, se, ah, al)
    if r == 0
        j->code_buffer = sv_cb
        j->code_bits = sv_bits
        j->bytepos = sv_pos
        j->marker = sv_mk
        j->nomore = sv_nm
        j->eob_run = sv_eob
        j->need_more = 0
    return r

# ─────────────────── 解码：重启 / marker / pending ───────────────────
# 重启标记复位（RSTn）：清位缓冲 + eob_run + marker。dc_pred 由 img 清。
fnc codec_jdec_reset: sp: &
    var j: codec_jdec& = (sp: codec_jdec&)
    j->code_buffer = 0
    j->code_bits = 0
    j->marker = 0xff
    j->nomore = 0
    j->eob_run = 0
    j->need_more = 0
    return

# 返回 cj_grow 遇到的 marker XX（0xff=无）。
fnc codec_jdec_marker: i4, sp: &
    var j: codec_jdec& = (sp: codec_jdec&)
    return (j->marker: i4)

# 取回 marker 之后未消费的内部缓冲字节到 out[0..cap)，返回拷贝字节数并推进。
fnc codec_jdec_pending: i8, sp: &, out: u1&, cap: u8
    var j: codec_jdec& = (sp: codec_jdec&)
    var avail: u8 = j->inlen - j->bytepos
    var take: u8 = avail < cap ? avail : cap
    if take != 0
        cj_bcopy(out, (&j->inbuf[j->bytepos]: u1&), take)
    j->bytepos = j->bytepos + take
    return (take: i8)

# 未消费字节数（供 img 分配 pending 缓冲）。
fnc codec_jdec_pending_len: u8, sp: &
    var j: codec_jdec& = (sp: codec_jdec&)
    return j->inlen - j->bytepos

# ─────────────────── 编码：codec_jenc ───────────────────
# 返回编码器状态结构字节数（供分配）。
fnc codec_jenc_size: u8
    return (sizeof(codec_jenc): u8)

# 初始化编码器（清零）；返回 0。
fnc codec_jenc_init: i4, sp: &
    var z: codec_jenc& = (sp: codec_jenc&)
    cj_bzero((sp: u1&), (sizeof(codec_jenc): u8))
    return 0

# 释放内部输出缓冲。
fnc codec_jenc_free: sp: &
    var z: codec_jenc& = (sp: codec_jenc&)
    if z->ob != nil
        recycle((z->ob: &))
        z->ob = nil
        z->obcap = 0
        z->oblen = 0
        z->obpos = 0
    return

# 交标准编码 Huffman 表：tc 0=DC/1=AC；counts[16]/values。返回 0/-1。
fnc codec_jenc_dht: i4, sp: &, tc: i4, th: i4, counts: u1&, values: u1&
    var z: codec_jenc& = (sp: codec_jenc&)
    if tc < 0 || tc > 1 || th < 0 || th > 3
        return -1
    var e: cj_ehuff& = &z->edc[th]
    if tc != 0
        e = &z->eac[th]
    cj_bzero((&e->len[0]: u1&), 256)
    var k: i4 = 0
    var code: u4 = 0
    var i: i4 = 0
    while i < 16
        var jc: i4 = 0
        while jc < (counts[i]: i4)
            var sym: i4 = (values[k]: i4)
            e->code[sym] = (code: u2)
            e->len[sym] = ((i + 1): u1)
            code = code + 1
            k = k + 1
            if k > 256
                return -1
            jc = jc + 1
        code = code << 1
        i = i + 1
    return 0

# 缓冲扩容（追加 need 字节前）。返回 0/-1（-1 置 oom）。
fnc ejw_grow: i4, z: codec_jenc&, need: u8
    if z->oblen + need > z->obcap
        var nc: u8 = z->obcap != 0 ? z->obcap : 4096
        while nc < z->oblen + need
            nc = nc * 2
        var nb: & = refit((z->ob: &), nc)
        if nb == nil
            z->oom = 1
            return -1
        z->ob = (nb: u1&)
        z->obcap = nc
    return 0

# 位写 + 0xFF→0xFF00 塞字节。产出追加进 ob。
fnc cj_writebits: z: codec_jenc&, code: i4, len: i4
    z->bitCnt = z->bitCnt + len
    z->bitBuf = z->bitBuf | (code << (24 - z->bitCnt))
    while z->bitCnt >= 8
        if ejw_grow(z, 2) < 0
            return
        var c: i4 = (z->bitBuf >> 16) & 255
        z->ob[z->oblen] = (c: u1)
        z->oblen = z->oblen + 1
        if c == 255
            z->ob[z->oblen] = 0
            z->oblen = z->oblen + 1
        z->bitBuf = z->bitBuf << 8
        z->bitCnt = z->bitCnt - 8
    return

# 熵编码一块（du[64] zigzag 序、量化后、du[0]=DC；dc_pred 读改写）。返回 1 完成 / -1 内存错。
fnc codec_jenc_block: i4, sp: &, du: i4&, dc_tbl: i4, ac_tbl: i4, dc_pred: i4&
    var z: codec_jenc& = (sp: codec_jenc&)
    var hdc: cj_ehuff& = &z->edc[dc_tbl]
    var hac: cj_ehuff& = &z->eac[ac_tbl]
    z->oom = 0
    var diff: i4 = du[0] - dc_pred[0]
    dc_pred[0] = du[0]
    if diff == 0
        cj_writebits(z, (hdc->code[0]: i4), (hdc->len[0]: i4))
    else
        var t1: i4 = diff < 0 ? (0 - diff) : diff
        var v: i4 = diff < 0 ? diff - 1 : diff
        var blen: i4 = 1
        t1 = t1 >> 1
        while t1 != 0
            blen = blen + 1
            t1 = t1 >> 1
        cj_writebits(z, (hdc->code[blen]: i4), (hdc->len[blen]: i4))
        cj_writebits(z, v & ((1 << blen) - 1), blen)
    var end0pos: i4 = 63
    while end0pos > 0 && du[end0pos] == 0
        end0pos = end0pos - 1
    if end0pos == 0
        cj_writebits(z, (hac->code[0]: i4), (hac->len[0]: i4))
        return z->oom != 0 ? -1 : 1
    var i: i4 = 1
    while i <= end0pos
        var startpos: i4 = i
        while du[i] == 0 && i <= end0pos
            i = i + 1
        var nrzeroes: i4 = i - startpos
        if nrzeroes >= 16
            var lng: i4 = nrzeroes >> 4
            var m: i4 = 1
            while m <= lng
                cj_writebits(z, (hac->code[0xF0]: i4), (hac->len[0xF0]: i4))
                m = m + 1
            nrzeroes = nrzeroes & 15
        var t1: i4 = du[i] < 0 ? (0 - du[i]) : du[i]
        var v: i4 = du[i] < 0 ? du[i] - 1 : du[i]
        var blen: i4 = 1
        t1 = t1 >> 1
        while t1 != 0
            blen = blen + 1
            t1 = t1 >> 1
        cj_writebits(z, (hac->code[(nrzeroes << 4) + blen]: i4), (hac->len[(nrzeroes << 4) + blen]: i4))
        cj_writebits(z, v & ((1 << blen) - 1), blen)
        i = i + 1
    if end0pos != 63
        cj_writebits(z, (hac->code[0]: i4), (hac->len[0]: i4))
    return z->oom != 0 ? -1 : 1

# 抽产出 ob[obpos..oblen) 到 out[0..cap)，返回抽出字节数。
fnc codec_jenc_drain: i8, sp: &, out: u1&, cap: u8
    var z: codec_jenc& = (sp: codec_jenc&)
    var avail: u8 = z->oblen - z->obpos
    var take: u8 = avail < cap ? avail : cap
    if take != 0
        cj_bcopy(out, (&z->ob[z->obpos]: u1&), take)
    z->obpos = z->obpos + take
    if z->obpos >= z->oblen
        z->obpos = 0
        z->oblen = 0
    return (take: i8)

# 收尾：末尾 fillBits(0x7F,7) 位对齐（EOI 由 img 写）。
fnc codec_jenc_flush: i4, sp: &
    var z: codec_jenc& = (sp: codec_jenc&)
    z->oom = 0
    cj_writebits(z, 0x7F, 7)
    return z->oom != 0 ? -1 : 0
