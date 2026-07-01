# img_png —— PNG 格式私有实现（由 img.sc 经 `add img_png.sc` 内联为子模块）
#
# 依赖：img 结构（定义于 img.sc）+ io.sc（com I/O）+ mem.sc（chunk/refit/recycle）+ codec.sc（zlib/crc32）。
# 本文件是 img.sc 的内联子单元：只含 PNG 私有类型与辅助函数；公开接口 png_read/png_write/png_shape
#   定义在 img.sc。单独编译不完整（缺 img 结构）。
#
# ================================ PNG ================================
# 规格：https://www.w3.org/TR/PNG/ —— 大端；8 字节签名 + 一系列 chunk（len+type+data+crc）。
# 全功能解码（对齐 stb_image.h）：
#   - color type 0/2/3/4/6（灰度/真彩/调色板/灰度+A/真彩+A）；bit depth 1/2/4/8/16。
#   - Adam7 隔行；tRNS 透明（调色板逐项 alpha / 非调色板 color-key）；5 种行过滤器 None/Sub/Up/Average/Paeth。
#   - IDAT 收齐整块 codec_zlib_decode（iphone CgBI 私有块用 raw inflate）；PLTE 调色板展开。
#   - req_comp（0=按文件原生通道，1..4=强制）经 convert_format 转换；depth 8/16 分别处理。
# 全功能编码（对齐 stb_image_write.h）：8 位，channels 1/2/3/4（灰/灰A/RGB/RGBA），逐行自适应选最优
#   过滤器（试 5 种取绝对值熵最小），codec_zlib_encode 压缩，codec_crc32 计算 chunk CRC。

inc io.sc
inc mem.sc
inc codec.sc

# 解码上下文：宽/高 + 源分量数（去过滤所用）+ 输出缓冲
def png_img: {
    w:     i4
    h:     i4
    img_n: i4        # 源分量数（灰=1/灰A=2/真彩=3/真彩A=4；调色板=1 索引）
    out:   u1&       # 解码输出缓冲（png_create 分配）
}

# 解码结果元信息
def png_meta: {
    depth:     i4    # 位深 1/2/4/8/16（输出统一 8 或 16）
    color:     i4    # PNG color type
    interlace: i4
    out_n:     i4    # out 缓冲实际通道数（req_comp 转换前）
    src_n:     i4    # 上报的源通道数
}

# -------- com 精确读/写（循环至满 n 字节；0=成功 / <0=错或意外 EOF）--------
fnc png_rd: i4, c: com&, buf: &, n: u8
    var p: u1& = (buf: u1&)
    var got: u8 = 0
    while got < n
        var rem: u8 = n - got
        var want: u4 = 0
        if rem > 0x7FFFFFFF
            want = 0x7FFFFFFF
        else
            want = (rem: u4)
        var r: i4 = c->read((&p[got]: &), &want)
        if r < 0
            return -1
        if want == 0
            return -1
        got = got + (want: u8)
    return 0

fnc png_wr: i4, c: com&, buf: &, n: u8
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

# 字节缓冲 memcpy（正向；不重叠）
fnc png_bcopy: dst: u1&, src: u1&, n: u8
    var i: u8 = 0
    while i < n
        dst[i] = src[i]
        i = i + 1
    return

# 大端 4 字节读（从字节缓冲）
fnc png_u32be: u4, b: u1&, o: u8
    return ((b[o]: u4) << 24) | ((b[o + 1]: u4) << 16) | ((b[o + 2]: u4) << 8) | (b[o + 3]: u4)

# 大端 4 字节写到 com
fnc png_wput32: i4, c: com&, v: u4
    var b[4]: u1
    b[0] = ((v >> 24) & 255: u1)
    b[1] = ((v >> 16) & 255: u1)
    b[2] = ((v >> 8) & 255: u1)
    b[3] = (v & 255: u1)
    return png_wr(c, (&b[0]: &), 4)

# -------- Paeth 预测器 --------
# 解码式（stb 等价无分支形式）
fnc png_paeth: i4, a: i4, b: i4, c: i4
    var thresh: i4 = c * 3 - (a + b)
    var lo: i4 = a < b ? a : b
    var hi: i4 = a < b ? b : a
    var t0: i4 = hi <= thresh ? lo : c
    var t1: i4 = thresh <= lo ? hi : t0
    return t1

# 编码式（PNG 规范原式，返回选中的预测值，已截断到字节）
fnc png_paeth_w: i4, a: i4, b: i4, c: i4
    var p: i4 = a + b - c
    var pa: i4 = p - a
    if pa < 0
        pa = 0 - pa
    var pb: i4 = p - b
    if pb < 0
        pb = 0 - pb
    var pc: i4 = p - c
    if pc < 0
        pc = 0 - pc
    if pa <= pb && pa <= pc
        return a & 255
    if pb <= pc
        return b & 255
    return c & 255

# 追加全 255 alpha 通道（dst 可等于 src；img_n 为 1 或 3；逆序处理以允许原地）
fnc png_alpha_expand8: dst: u1&, src: u1&, x: u4, img_n: i4
    var i: i8 = (x: i8) - 1
    if img_n == 1
        while i >= 0
            dst[i * 2 + 1] = 255
            dst[i * 2 + 0] = src[i]
            i = i - 1
    else
        while i >= 0
            dst[i * 4 + 3] = 255
            dst[i * 4 + 2] = src[i * 3 + 2]
            dst[i * 4 + 1] = src[i * 3 + 1]
            dst[i * 4 + 0] = src[i * 3 + 0]
            i = i - 1
    return

# 解压后 filter 数据的精确总字节数（= codec_zlib_decode 所需 cap；隔行按 7 个 pass 求和）
fnc png_raw_len: u8, x: u4, y: u4, img_n: i4, depth: i4, interlaced: i4
    if interlaced == 0
        var iwb: u8 = (((img_n: u8) * (x: u8) * (depth: u8)) + 7) >> 3
        return (iwb + 1) * (y: u8)
    var xorig[7]: i4
    var yorig[7]: i4
    var xspc[7]: i4
    var yspc[7]: i4
    xorig[0] = 0
    xorig[1] = 4
    xorig[2] = 0
    xorig[3] = 2
    xorig[4] = 0
    xorig[5] = 1
    xorig[6] = 0
    yorig[0] = 0
    yorig[1] = 0
    yorig[2] = 4
    yorig[3] = 0
    yorig[4] = 2
    yorig[5] = 0
    yorig[6] = 1
    xspc[0] = 8
    xspc[1] = 8
    xspc[2] = 4
    xspc[3] = 4
    xspc[4] = 2
    xspc[5] = 2
    xspc[6] = 1
    yspc[0] = 8
    yspc[1] = 8
    yspc[2] = 8
    yspc[3] = 4
    yspc[4] = 4
    yspc[5] = 2
    yspc[6] = 2
    var total: u8 = 0
    var p: i4 = 0
    while p < 7
        var xx: i4 = ((x: i4) - xorig[p] + xspc[p] - 1) / xspc[p]
        var yy: i4 = ((y: i4) - yorig[p] + yspc[p] - 1) / yspc[p]
        if xx > 0 && yy > 0
            var iwb: u8 = (((img_n: u8) * (xx: u8) * (depth: u8)) + 7) >> 3
            total = total + (iwb + 1) * (yy: u8)
        p = p + 1
    return total

# -------- 去过滤 + 展开一个（子）图像 raw -> a->out（对齐 stbi__create_png_image_raw）--------
fnc png_create_raw: i4, a: png_img&, raw: u1&, raw_len: u8, out_n: i4, x: u4, y: u4, depth: i4, color: i4
    a->out = nil
    var bytes: i4 = depth == 16 ? 2 : 1
    var img_n: i4 = a->img_n
    var stride: u8 = (x: u8) * (out_n: u8) * (bytes: u8)
    var output_bytes: i4 = out_n * bytes
    var filter_bytes: i4 = img_n * bytes
    var width: u4 = x
    var img_width_bytes: u8 = (((img_n: u8) * (x: u8) * (depth: u8)) + 7) >> 3
    var img_len: u8 = (img_width_bytes + 1) * (y: u8)
    if raw_len < img_len
        return -1
    var outbuf: u1& = (chunk((x: u8) * (y: u8) * (output_bytes: u8)): u1&)
    if outbuf == nil
        return -1
    a->out = outbuf
    var filter_buf: u1& = (chunk(img_width_bytes * 2): u1&)
    if filter_buf == nil
        recycle((outbuf: &))
        a->out = nil
        return -1
    if depth < 8
        filter_bytes = 1
        width = (img_width_bytes: u4)
    var fb: u8 = (filter_bytes: u8)
    var rp: u8 = 0
    var j: u4 = 0
    while j < y
        var curi: u8 = ((j & 1): u8) * img_width_bytes
        var prii: u8 = ((1 - (j & 1)): u8) * img_width_bytes
        var cur: u1& = (&filter_buf[curi]: u1&)
        var prior: u1& = (&filter_buf[prii]: u1&)
        var dest: u1& = (&outbuf[stride * (j: u8)]: u1&)
        var nk: u8 = (width: u8) * fb
        var filter: i4 = (raw[rp]: i4)
        rp = rp + 1
        if filter > 4
            recycle((filter_buf: &))
            recycle((outbuf: &))
            a->out = nil
            return -1
        if j == 0
            if filter == 2
                filter = 0
            else if filter == 3
                filter = 5
            else if filter == 4
                filter = 1
        # 去过滤
        if filter == 0
            png_bcopy(cur, (&raw[rp]: u1&), nk)
        else if filter == 1
            var k: u8 = 0
            while k < fb
                cur[k] = raw[rp + k]
                k = k + 1
            while k < nk
                cur[k] = (((raw[rp + k]: i4) + (cur[k - fb]: i4)) & 255: u1)
                k = k + 1
        else if filter == 2
            var k: u8 = 0
            while k < nk
                cur[k] = (((raw[rp + k]: i4) + (prior[k]: i4)) & 255: u1)
                k = k + 1
        else if filter == 3
            var k: u8 = 0
            while k < fb
                cur[k] = (((raw[rp + k]: i4) + ((prior[k]: i4) >> 1)) & 255: u1)
                k = k + 1
            while k < nk
                cur[k] = (((raw[rp + k]: i4) + (((prior[k]: i4) + (cur[k - fb]: i4)) >> 1)) & 255: u1)
                k = k + 1
        else if filter == 4
            var k: u8 = 0
            while k < fb
                cur[k] = (((raw[rp + k]: i4) + (prior[k]: i4)) & 255: u1)
                k = k + 1
            while k < nk
                cur[k] = (((raw[rp + k]: i4) + png_paeth((cur[k - fb]: i4), (prior[k]: i4), (prior[k - fb]: i4))) & 255: u1)
                k = k + 1
        else if filter == 5
            var k: u8 = 0
            while k < fb
                cur[k] = raw[rp + k]
                k = k + 1
            while k < nk
                cur[k] = (((raw[rp + k]: i4) + ((cur[k - fb]: i4) >> 1)) & 255: u1)
                k = k + 1
        rp = rp + nk
        # 展开到 dest（并按需补 alpha）
        if depth < 8
            var scale: i4 = 1
            if color == 0
                if depth == 1
                    scale = 255
                else if depth == 2
                    scale = 85
                else
                    scale = 17
            var nsmp: u8 = (x: u8) * (img_n: u8)
            var ini: u8 = 0
            var outi: u8 = 0
            var inb: i4 = 0
            var i: u8 = 0
            if depth == 4
                while i < nsmp
                    if (i & 1) == 0
                        inb = (cur[ini]: i4)
                        ini = ini + 1
                    dest[outi] = ((scale * (inb >> 4)) & 255: u1)
                    inb = (inb << 4) & 255
                    outi = outi + 1
                    i = i + 1
            else if depth == 2
                while i < nsmp
                    if (i & 3) == 0
                        inb = (cur[ini]: i4)
                        ini = ini + 1
                    dest[outi] = ((scale * (inb >> 6)) & 255: u1)
                    inb = (inb << 2) & 255
                    outi = outi + 1
                    i = i + 1
            else
                while i < nsmp
                    if (i & 7) == 0
                        inb = (cur[ini]: i4)
                        ini = ini + 1
                    dest[outi] = ((scale * (inb >> 7)) & 255: u1)
                    inb = (inb << 1) & 255
                    outi = outi + 1
                    i = i + 1
            if img_n != out_n
                png_alpha_expand8(dest, dest, x, img_n)
        else if depth == 8
            if img_n == out_n
                png_bcopy(dest, cur, (x: u8) * (img_n: u8))
            else
                png_alpha_expand8(dest, cur, x, img_n)
        else
            # depth == 16：big-endian -> 本机 u2
            var d16: u2& = (dest: u2&)
            var nsmp: u8 = (x: u8) * (img_n: u8)
            if img_n == out_n
                var i: u8 = 0
                var ci: u8 = 0
                while i < nsmp
                    d16[i] = (((cur[ci]: u4) << 8) | (cur[ci + 1]: u4): u2)
                    ci = ci + 2
                    i = i + 1
            else if img_n == 1
                var i: u8 = 0
                var ci: u8 = 0
                var di: u8 = 0
                while i < (x: u8)
                    d16[di] = (((cur[ci]: u4) << 8) | (cur[ci + 1]: u4): u2)
                    d16[di + 1] = 0xFFFF
                    ci = ci + 2
                    di = di + 2
                    i = i + 1
            else
                var i: u8 = 0
                var ci: u8 = 0
                var di: u8 = 0
                while i < (x: u8)
                    d16[di] = (((cur[ci]: u4) << 8) | (cur[ci + 1]: u4): u2)
                    d16[di + 1] = (((cur[ci + 2]: u4) << 8) | (cur[ci + 3]: u4): u2)
                    d16[di + 2] = (((cur[ci + 4]: u4) << 8) | (cur[ci + 5]: u4): u2)
                    d16[di + 3] = 0xFFFF
                    ci = ci + 6
                    di = di + 4
                    i = i + 1
        j = j + 1
    recycle((filter_buf: &))
    return 0

# -------- 组装（含 Adam7 反隔行）（对齐 stbi__create_png_image）--------
fnc png_create: i4, a: png_img&, image_data: u1&, data_len: u8, out_n: i4, depth: i4, color: i4, interlaced: i4
    var bytes: i4 = depth == 16 ? 2 : 1
    var out_bytes: i4 = out_n * bytes
    if interlaced == 0
        return png_create_raw(a, image_data, data_len, out_n, (a->w: u4), (a->h: u4), depth, color)
    var fout: u1& = (chunk((a->w: u8) * (a->h: u8) * (out_bytes: u8)): u1&)
    if fout == nil
        return -1
    var xorig[7]: i4
    var yorig[7]: i4
    var xspc[7]: i4
    var yspc[7]: i4
    xorig[0] = 0
    xorig[1] = 4
    xorig[2] = 0
    xorig[3] = 2
    xorig[4] = 0
    xorig[5] = 1
    xorig[6] = 0
    yorig[0] = 0
    yorig[1] = 0
    yorig[2] = 4
    yorig[3] = 0
    yorig[4] = 2
    yorig[5] = 0
    yorig[6] = 1
    xspc[0] = 8
    xspc[1] = 8
    xspc[2] = 4
    xspc[3] = 4
    xspc[4] = 2
    xspc[5] = 2
    xspc[6] = 1
    yspc[0] = 8
    yspc[1] = 8
    yspc[2] = 8
    yspc[3] = 4
    yspc[4] = 4
    yspc[5] = 2
    yspc[6] = 2
    var data_off: u8 = 0
    var p: i4 = 0
    while p < 7
        var xx: i4 = ((a->w: i4) - xorig[p] + xspc[p] - 1) / xspc[p]
        var yy: i4 = ((a->h: i4) - yorig[p] + yspc[p] - 1) / yspc[p]
        if xx > 0 && yy > 0
            var img_len: u8 = (((((a->img_n: u8) * (xx: u8) * (depth: u8)) + 7) >> 3) + 1) * (yy: u8)
            if png_create_raw(a, (&image_data[data_off]: u1&), data_len - data_off, out_n, (xx: u4), (yy: u4), depth, color) < 0
                recycle((fout: &))
                return -1
            var jj: i4 = 0
            while jj < yy
                var ii: i4 = 0
                while ii < xx
                    var out_y: i4 = jj * yspc[p] + yorig[p]
                    var out_x: i4 = ii * xspc[p] + xorig[p]
                    var dsti: u8 = ((out_y: u8) * (a->w: u8) + (out_x: u8)) * (out_bytes: u8)
                    var srci: u8 = (((jj: u8) * (xx: u8)) + (ii: u8)) * (out_bytes: u8)
                    png_bcopy((&fout[dsti]: u1&), (&a->out[srci]: u1&), (out_bytes: u8))
                    ii = ii + 1
                jj = jj + 1
            recycle((a->out: &))
            a->out = nil
            data_off = data_off + img_len
        p = p + 1
    a->out = fout
    return 0

# -------- color-key 透明（8 位）（对齐 stbi__compute_transparency）--------
fnc png_trans8: i4, a: png_img&, t0: i4, t1: i4, t2: i4, out_n: i4
    var pc: u8 = (a->w: u8) * (a->h: u8)
    var p: u1& = a->out
    var i: u8 = 0
    var o: u8 = 0
    if out_n == 2
        while i < pc
            if (p[o]: i4) == t0
                p[o + 1] = 0
            else
                p[o + 1] = 255
            o = o + 2
            i = i + 1
    else
        while i < pc
            if (p[o]: i4) == t0 && (p[o + 1]: i4) == t1 && (p[o + 2]: i4) == t2
                p[o + 3] = 0
            o = o + 4
            i = i + 1
    return 0

# -------- color-key 透明（16 位）（对齐 stbi__compute_transparency16）--------
fnc png_trans16: i4, a: png_img&, t0: i4, t1: i4, t2: i4, out_n: i4
    var pc: u8 = (a->w: u8) * (a->h: u8)
    var p: u2& = (a->out: u2&)
    var i: u8 = 0
    var o: u8 = 0
    if out_n == 2
        while i < pc
            if (p[o]: i4) == t0
                p[o + 1] = 0
            else
                p[o + 1] = 0xFFFF
            o = o + 2
            i = i + 1
    else
        while i < pc
            if (p[o]: i4) == t0 && (p[o + 1]: i4) == t1 && (p[o + 2]: i4) == t2
                p[o + 3] = 0
            o = o + 4
            i = i + 1
    return 0

# -------- 调色板展开（索引 -> RGB/RGBA）（对齐 stbi__expand_png_palette）--------
fnc png_expand_pal: i4, a: png_img&, pal: u1&, pn: i4
    var pc: u8 = (a->w: u8) * (a->h: u8)
    var np: u1& = (chunk(pc * (pn: u8)): u1&)
    if np == nil
        return -1
    var orig: u1& = a->out
    var i: u8 = 0
    var o: u8 = 0
    if pn == 3
        while i < pc
            var n: u8 = (orig[i]: u8) * 4
            np[o] = pal[n]
            np[o + 1] = pal[n + 1]
            np[o + 2] = pal[n + 2]
            o = o + 3
            i = i + 1
    else
        while i < pc
            var n: u8 = (orig[i]: u8) * 4
            np[o] = pal[n]
            np[o + 1] = pal[n + 1]
            np[o + 2] = pal[n + 2]
            np[o + 3] = pal[n + 3]
            o = o + 4
            i = i + 1
    recycle((a->out: &))
    a->out = np
    return 0

# -------- 通道数转换（8 位）（对齐 stbi__convert_format）；成功返回新缓冲并回收 data，失败 nil --------
fnc png_conv8: u1&, data: u1&, img_n: i4, req: i4, x: u4, y: u4
    if req == img_n
        return data
    var good: u1& = (chunk((req: u8) * (x: u8) * (y: u8)): u1&)
    if good == nil
        recycle((data: &))
        return nil
    var combo: i4 = img_n * 8 + req
    var j: u4 = 0
    while j < y
        var si: u8 = (j: u8) * (x: u8) * (img_n: u8)
        var di: u8 = (j: u8) * (x: u8) * (req: u8)
        var i: u4 = 0
        while i < x
            var s: u1& = (&data[si]: u1&)
            var d: u1& = (&good[di]: u1&)
            if combo == 10
                d[0] = s[0]
                d[1] = 255
            else if combo == 11
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
            else if combo == 12
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
                d[3] = 255
            else if combo == 17
                d[0] = s[0]
            else if combo == 19
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
            else if combo == 20
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
                d[3] = s[1]
            else if combo == 28
                d[0] = s[0]
                d[1] = s[1]
                d[2] = s[2]
                d[3] = 255
            else if combo == 25
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u1)
            else if combo == 26
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u1)
                d[1] = 255
            else if combo == 33
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u1)
            else if combo == 34
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u1)
                d[1] = s[3]
            else if combo == 35
                d[0] = s[0]
                d[1] = s[1]
                d[2] = s[2]
            si = si + (img_n: u8)
            di = di + (req: u8)
            i = i + 1
        j = j + 1
    recycle((data: &))
    return good

# -------- 通道数转换（16 位）（对齐 stbi__convert_format16）--------
fnc png_conv16: u1&, data: u1&, img_n: i4, req: i4, x: u4, y: u4
    if req == img_n
        return data
    var good: u1& = (chunk((req: u8) * (x: u8) * (y: u8) * 2): u1&)
    if good == nil
        recycle((data: &))
        return nil
    var src: u2& = (data: u2&)
    var dst: u2& = (good: u2&)
    var combo: i4 = img_n * 8 + req
    var j: u4 = 0
    while j < y
        var si: u8 = (j: u8) * (x: u8) * (img_n: u8)
        var di: u8 = (j: u8) * (x: u8) * (req: u8)
        var i: u4 = 0
        while i < x
            var s: u2& = (&src[si]: u2&)
            var d: u2& = (&dst[di]: u2&)
            if combo == 10
                d[0] = s[0]
                d[1] = 0xFFFF
            else if combo == 11
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
            else if combo == 12
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
                d[3] = 0xFFFF
            else if combo == 17
                d[0] = s[0]
            else if combo == 19
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
            else if combo == 20
                d[0] = s[0]
                d[1] = s[0]
                d[2] = s[0]
                d[3] = s[1]
            else if combo == 28
                d[0] = s[0]
                d[1] = s[1]
                d[2] = s[2]
                d[3] = 0xFFFF
            else if combo == 25
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u2)
            else if combo == 26
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u2)
                d[1] = 0xFFFF
            else if combo == 33
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u2)
            else if combo == 34
                d[0] = (((s[0]: i4) * 77 + (s[1]: i4) * 150 + (s[2]: i4) * 29) >> 8: u2)
                d[1] = s[3]
            else if combo == 35
                d[0] = s[0]
                d[1] = s[1]
                d[2] = s[2]
            si = si + (img_n: u8)
            di = di + (req: u8)
            i = i + 1
        j = j + 1
    recycle((data: &))
    return good

# -------- 垂直翻转行序（写/读朝向归一）--------
fnc png_flip_rows: buf: u1&, w: u4, h: u4, bpp: u4
    if h < 2
        return
    var rowb: u8 = (w: u8) * (bpp: u8)
    var tmp: u1& = (chunk(rowb): u1&)
    if tmp == nil
        return
    var top: u4 = 0
    var bot: u4 = h - 1
    while top < bot
        var ti: u8 = (top: u8) * rowb
        var bi: u8 = (bot: u8) * rowb
        png_bcopy(tmp, (&buf[ti]: u1&), rowb)
        png_bcopy((&buf[ti]: u1&), (&buf[bi]: u1&), rowb)
        png_bcopy((&buf[bi]: u1&), tmp, rowb)
        top = top + 1
        bot = bot - 1
    recycle((tmp: &))
    return

# -------- 解析 PNG（scan：0=完整解码 / 1=仅头信息）--------
# 成功返回 0（填 a->out[仅 load] 与 meta），失败 <0。
# 流式解码错误收尾：释放 feed 缓冲 / 原始缓冲 / 流式 inflate 状态，统一返回 -1。
fnc png_pdec_free: i4, idata: u1&, expanded: u1&, zst: &
    recycle((idata: &))
    recycle((expanded: &))
    if zst != nil
        codec_zdec_free(zst)
        recycle((zst: &))
    return -1

fnc png_parse: i4, c: com&, a: png_img&, meta: png_meta&, scan: i4, req_comp: i4
    a->out = nil
    a->w = 0
    a->h = 0
    a->img_n = 0
    var sig[8]: u1
    if png_rd(c, (&sig[0]: &), 8) < 0
        return -1
    if sig[0] != 0x89 || sig[1] != 0x50 || sig[2] != 0x4E || sig[3] != 0x47
        return -1
    if sig[4] != 0x0D || sig[5] != 0x0A || sig[6] != 0x1A || sig[7] != 0x0A
        return -1
    var pal[1024]: u1
    var pal_len: i4 = 0
    var pal_img_n: i4 = 0
    var has_trans: i4 = 0
    var tc0: i4 = 0
    var tc1: i4 = 0
    var tc2: i4 = 0
    var depth: i4 = 0
    var color: i4 = 0
    var interlace: i4 = 0
    var is_iphone: i4 = 0
    var first: i4 = 1
    var idata: u1& = nil         # IDAT 分块喂入缓冲（首个 IDAT 时分配，复用）
    var expanded: u1& = nil      # 去过滤前完整原始扫描线缓冲（首个 IDAT 时按 rawlen 分配）
    var ecap: u8 = 0
    var dpos: u8 = 0             # 已流式解出字节数
    var zst: & = nil            # 流式 inflate 状态（首个 IDAT 分配/初始化）
    var idat_seen: i4 = 0
    var eof: i4 = 0
    while eof == 0
        var ch[8]: u1
        if png_rd(c, (&ch[0]: &), 8) < 0
            return png_pdec_free(idata, expanded, zst)
        var clen: u4 = png_u32be((&ch[0]: u1&), 0)
        if ch[4] == 0x43 && ch[5] == 0x67 && ch[6] == 0x42 && ch[7] == 0x49
            # CgBI（iphone）：私有块，标记后跳过
            is_iphone = 1
            if c->seek((clen: i8) + 4, 1) < 0
                return png_pdec_free(idata, expanded, zst)
        else if ch[4] == 0x49 && ch[5] == 0x48 && ch[6] == 0x44 && ch[7] == 0x52
            # IHDR
            if first == 0
                return png_pdec_free(idata, expanded, zst)
            first = 0
            if clen != 13
                return png_pdec_free(idata, expanded, zst)
            var ih[13]: u1
            if png_rd(c, (&ih[0]: &), 13) < 0
                return png_pdec_free(idata, expanded, zst)
            a->w = (png_u32be((&ih[0]: u1&), 0): i4)
            a->h = (png_u32be((&ih[0]: u1&), 4): i4)
            depth = (ih[8]: i4)
            if depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16
                return png_pdec_free(idata, expanded, zst)
            color = (ih[9]: i4)
            if color > 6
                return png_pdec_free(idata, expanded, zst)
            if color == 3 && depth == 16
                return png_pdec_free(idata, expanded, zst)
            if color == 3
                pal_img_n = 3
            else if (color & 1) != 0
                return png_pdec_free(idata, expanded, zst)
            if ih[10] != 0
                return png_pdec_free(idata, expanded, zst)
            if ih[11] != 0
                return png_pdec_free(idata, expanded, zst)
            interlace = (ih[12]: i4)
            if interlace > 1
                return png_pdec_free(idata, expanded, zst)
            if a->w == 0 || a->h == 0
                return png_pdec_free(idata, expanded, zst)
            if pal_img_n == 0
                a->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0)
            else
                a->img_n = 1
            if c->seek(4, 1) < 0
                return png_pdec_free(idata, expanded, zst)
        else if ch[4] == 0x50 && ch[5] == 0x4C && ch[6] == 0x54 && ch[7] == 0x45
            # PLTE
            if first != 0
                return png_pdec_free(idata, expanded, zst)
            if clen > 768
                return png_pdec_free(idata, expanded, zst)
            pal_len = (clen: i4) / 3
            if (pal_len: u4) * 3 != clen
                return png_pdec_free(idata, expanded, zst)
            var i: i4 = 0
            while i < pal_len
                var e[3]: u1
                if png_rd(c, (&e[0]: &), 3) < 0
                    return png_pdec_free(idata, expanded, zst)
                pal[i * 4 + 0] = e[0]
                pal[i * 4 + 1] = e[1]
                pal[i * 4 + 2] = e[2]
                pal[i * 4 + 3] = 255
                i = i + 1
            if c->seek(4, 1) < 0
                return png_pdec_free(idata, expanded, zst)
        else if ch[4] == 0x74 && ch[5] == 0x52 && ch[6] == 0x4E && ch[7] == 0x53
            # tRNS
            if first != 0
                return png_pdec_free(idata, expanded, zst)
            if idat_seen != 0
                return png_pdec_free(idata, expanded, zst)
            if pal_img_n != 0
                if pal_len == 0
                    return png_pdec_free(idata, expanded, zst)
                if (clen: i4) > pal_len
                    return png_pdec_free(idata, expanded, zst)
                pal_img_n = 4
                var i: i4 = 0
                while i < (clen: i4)
                    var b1[1]: u1
                    if png_rd(c, (&b1[0]: &), 1) < 0
                        return png_pdec_free(idata, expanded, zst)
                    pal[i * 4 + 3] = b1[0]
                    i = i + 1
            else
                if (a->img_n & 1) == 0
                    return png_pdec_free(idata, expanded, zst)
                if clen != (a->img_n: u4) * 2
                    return png_pdec_free(idata, expanded, zst)
                has_trans = 1
                var td[6]: u1
                if png_rd(c, (&td[0]: &), (clen: u8)) < 0
                    return png_pdec_free(idata, expanded, zst)
                if depth == 16
                    var k: i4 = 0
                    while k < a->img_n && k < 3
                        var v: i4 = ((td[k * 2]: i4) << 8) | (td[k * 2 + 1]: i4)
                        if k == 0
                            tc0 = v
                        else if k == 1
                            tc1 = v
                        else
                            tc2 = v
                        k = k + 1
                else
                    var sc: i4 = 1
                    if depth == 1
                        sc = 255
                    else if depth == 2
                        sc = 85
                    else if depth == 4
                        sc = 17
                    var k: i4 = 0
                    while k < a->img_n && k < 3
                        var v: i4 = (((td[k * 2]: i4) << 8) | (td[k * 2 + 1]: i4)) & 255
                        v = (v * sc) & 255
                        if k == 0
                            tc0 = v
                        else if k == 1
                            tc1 = v
                        else
                            tc2 = v
                        k = k + 1
            if c->seek(4, 1) < 0
                return png_pdec_free(idata, expanded, zst)
        else if ch[4] == 0x49 && ch[5] == 0x44 && ch[6] == 0x41 && ch[7] == 0x54
            # IDAT：流式喂入 codec_zdec，产出直接落入 expanded（不再累积压缩流）
            if first != 0
                return png_pdec_free(idata, expanded, zst)
            if pal_img_n != 0 && pal_len == 0
                return png_pdec_free(idata, expanded, zst)
            if scan == 1
                eof = 1
            else
                if idat_seen == 0
                    # 首个 IDAT：按 rawlen 分配 expanded、feed 缓冲，初始化流式 inflate
                    idat_seen = 1
                    var rawlen: u8 = png_raw_len((a->w: u4), (a->h: u4), a->img_n, depth, interlace)
                    ecap = rawlen + 64
                    expanded = (chunk(ecap): u1&)
                    if expanded == nil
                        return png_pdec_free(idata, expanded, zst)
                    idata = (chunk(16384): u1&)
                    if idata == nil
                        return png_pdec_free(idata, expanded, zst)
                    var zsz: u8 = codec_zdec_size()
                    zst = chunk(zsz)
                    if zst == nil
                        return png_pdec_free(idata, expanded, zst)
                    codec_zdec_init(zst, is_iphone != 0 ? 0 : 1)
                # 分块读取本 IDAT 并喂入解码
                var rem: u8 = (clen: u8)
                while rem > 0
                    var take: u8 = rem
                    if take > 16384
                        take = 16384
                    if png_rd(c, (&idata[0]: &), take) < 0
                        return png_pdec_free(idata, expanded, zst)
                    var consumed: u8 = 0
                    var got: i8 = codec_zdec_feed(zst, (&idata[0]: &), take, &consumed, (&expanded[dpos]: u1&), ecap - dpos)
                    if got < 0
                        return png_pdec_free(idata, expanded, zst)
                    dpos = dpos + (got: u8)
                    rem = rem - take
                if c->seek(4, 1) < 0
                    return png_pdec_free(idata, expanded, zst)
        else if ch[4] == 0x49 && ch[5] == 0x45 && ch[6] == 0x4E && ch[7] == 0x44
            # IEND
            eof = 1
        else
            # 未知块：关键块（大写首字母）报错，辅助块跳过
            if first != 0
                return png_pdec_free(idata, expanded, zst)
            if (ch[4] & 0x20) == 0
                return png_pdec_free(idata, expanded, zst)
            if c->seek((clen: i8) + 4, 1) < 0
                return png_pdec_free(idata, expanded, zst)
    # 头信息扫描：填通道数即返回
    if scan == 1
        meta->depth = depth
        meta->color = color
        meta->interlace = interlace
        var chn: i4 = 0
        if pal_img_n != 0
            chn = pal_img_n
        else if has_trans != 0
            chn = a->img_n + 1
        else
            chn = a->img_n
        meta->out_n = chn
        meta->src_n = chn
        return 0
    # 完整解码：expanded 已由流式 inflate 逐块填满（dpos 字节）
    if idat_seen == 0
        return png_pdec_free(idata, expanded, zst)
    recycle((idata: &))
    idata = nil
    if zst != nil
        codec_zdec_free(zst)
        recycle((zst: &))
        zst = nil
    var out_n: i4 = a->img_n
    if (req_comp == a->img_n + 1 && req_comp != 3 && pal_img_n == 0) || has_trans != 0
        out_n = a->img_n + 1
    if png_create(a, expanded, dpos, out_n, depth, color, interlace) < 0
        recycle((expanded: &))
        return -1
    recycle((expanded: &))
    if has_trans != 0
        if depth == 16
            if png_trans16(a, tc0, tc1, tc2, out_n) < 0
                return -1
        else
            if png_trans8(a, tc0, tc1, tc2, out_n) < 0
                return -1
    var src_n: i4 = a->img_n
    if pal_img_n != 0
        var pn: i4 = pal_img_n
        if req_comp >= 3
            pn = req_comp
        if png_expand_pal(a, (&pal[0]: u1&), pn) < 0
            return -1
        out_n = pn
        src_n = pal_img_n
    else if has_trans != 0
        src_n = a->img_n + 1
    meta->depth = depth
    meta->color = color
    meta->interlace = interlace
    meta->out_n = out_n
    meta->src_n = src_n
    return 0

# -------- 编码一行（选定过滤器 type，写入 line）（对齐 stbiw__encode_png_line）--------
fnc png_enc_line: pix: u1&, stride: u8, width: i4, height: i4, yy: i4, n: i4, ft: i4, line: u1&, flip: i4
    var type: i4 = ft
    if yy == 0
        if ft == 2
            type = 0
        else if ft == 3
            type = 5
        else if ft == 4
            type = 6
    var zoff: u8 = 0
    if flip != 0
        zoff = stride * ((height - 1 - yy): u8)
    else
        zoff = stride * (yy: u8)
    var z: u1& = (&pix[zoff]: u1&)
    var ss: i8 = (stride: i8)
    if flip != 0
        ss = 0 - (stride: i8)
    var wn: i8 = (width: i8) * (n: i8)
    var ni: i8 = (n: i8)
    if type == 0
        png_bcopy(line, z, (wn: u8))
        return
    var i: i8 = 0
    while i < ni
        if type == 1
            line[i] = z[i]
        else if type == 2
            line[i] = (((z[i]: i4) - (z[i - ss]: i4)) & 255: u1)
        else if type == 3
            line[i] = (((z[i]: i4) - ((z[i - ss]: i4) >> 1)) & 255: u1)
        else if type == 4
            line[i] = (((z[i]: i4) - png_paeth_w(0, (z[i - ss]: i4), 0)) & 255: u1)
        else
            line[i] = z[i]
        i = i + 1
    while i < wn
        if type == 1
            line[i] = (((z[i]: i4) - (z[i - ni]: i4)) & 255: u1)
        else if type == 2
            line[i] = (((z[i]: i4) - (z[i - ss]: i4)) & 255: u1)
        else if type == 3
            line[i] = (((z[i]: i4) - (((z[i - ni]: i4) + (z[i - ss]: i4)) >> 1)) & 255: u1)
        else if type == 4
            line[i] = (((z[i]: i4) - png_paeth_w((z[i - ni]: i4), (z[i - ss]: i4), (z[i - ss - ni]: i4))) & 255: u1)
        else if type == 5
            line[i] = (((z[i]: i4) - ((z[i - ni]: i4) >> 1)) & 255: u1)
        else
            line[i] = (((z[i]: i4) - png_paeth_w((z[i - ni]: i4), 0, 0)) & 255: u1)
        i = i + 1
    return

# 行熵估计（绝对值之和，越小越好）
fnc png_line_est: i8, line: u1&, n: u8
    var est: i8 = 0
    var i: u8 = 0
    while i < n
        var sv: i4 = (line[i]: i4)
        if sv >= 128
            sv = sv - 256
        if sv < 0
            sv = 0 - sv
        est = est + (sv: i8)
        i = i + 1
    return est

# 写一个 chunk（len + type + data + crc）
fnc png_wchunk: i4, c: com&, typ: u1&, data: u1&, dlen: u8
    if png_wput32(c, (dlen: u4)) < 0
        return -1
    if png_wr(c, (typ: &), 4) < 0
        return -1
    if dlen > 0
        if png_wr(c, (data: &), dlen) < 0
            return -1
    var crc: u4 = codec_crc32_update(0, (typ: &), 4)
    if dlen > 0
        crc = codec_crc32_update(crc, (data: &), dlen)
    if png_wput32(c, crc) < 0
        return -1
    return 0

# 流式编码抽取：把编码器可产出的压缩字节抽入 cstage，满一批（csize）即作为一个 IDAT chunk 写出。
#   finishing==0：先喂 feedbuf[0..feedlen) 再抽干本轮产出；finishing!=0：调 finish 抽至末块/尾产出完毕。
#   cstage 未满即返回（剩余不足一批的压缩字节留待下次或收尾后统一写出）。cfill 为 in/out 已积字节数。
fnc png_zdrain: i4, c: com&, zst: &, feedbuf: u1&, feedlen: u8, cstage: u1&, csize: u8, cfill: u8&, idtype: u1&, finishing: i4
    var first_call: i4 = 1
    while 1 == 1
        var room: u8 = csize - cfill[0]
        var got: i8 = 0
        if finishing != 0
            got = codec_zenc_finish(zst, (&cstage[cfill[0]]: u1&), room)
        else
            var consumed: u8 = 0
            if first_call != 0
                got = codec_zenc_feed(zst, (feedbuf: &), feedlen, &consumed, (&cstage[cfill[0]]: u1&), room)
            else
                got = codec_zenc_feed(zst, (nil: &), 0, &consumed, (&cstage[cfill[0]]: u1&), room)
        if got < 0
            return -1
        first_call = 0
        cfill[0] = cfill[0] + (got: u8)
        if cfill[0] >= csize
            if png_wchunk(c, idtype, cstage, cfill[0]) < 0
                return -1
            cfill[0] = 0
        else
            return 0
    return 0

