# img_bmp —— BMP 格式私有实现（由 img.sc 经 `add img_bmp.sc` 内联为子模块）
#
# 依赖：img 结构（定义于 img.sc）+ io.sc（com I/O）+ mem.sc（chunk/recycle）。
# 本文件是 img.sc 的内联子单元：只含 BMP 的私有类型与辅助函数（bmp_hdr / bmp_rd·wr /
#   bmp_ctz·scale / bmp_parse_header / bmp_decode_row），经 add 拼接后对 img.sc 可见；
#   公开接口 bmp_read/bmp_write/bmp_shape 定义在 img.sc。单独编译不完整（缺 img 结构）。
#
# ================================ BMP ================================
# 协议：https://en.wikipedia.org/wiki/BMP_file_format —— 小端；像素按行自底向上存储。
# 支持读：DIB 头 12(OS/2 CORE)/40/52/56/108/124；位深 1/4/8(调色板) 与 16/24/32(真彩色)；
#         BI_RGB(未压缩) 与 BI_BITFIELDS(掩码)；自顶/自底两种朝向；alpha_mode/flip_mode 与参考
#         iAlphaMode/iFlipMode 语义一致（强制 RGB/RGBA、朝向归一/翻转）。
# 支持写：24 位(BI_RGB) / 32 位(BI_BITFIELDS + alpha 掩码)；invert_mode 与参考 iInvertMode 一致
#         （height 正负号标朝向 + 源行翻转）。
# 暂不支持：BI_RLE4/RLE8（返回 <0；BMP 专用 RLE 与 codec.PackBits 不同，另议）。

inc io.sc
inc mem.sc

# 内部：解析后的 BMP 头信息
def bmp_hdr: {
    base:        i8     # 文件起始绝对位置（com 当前位置）
    pix_off:     u4     # 像素数据偏移（相对 base）
    header_size: u4     # DIB 头大小
    width:       i4
    height:      i4     # 已取绝对值（正）
    bpp:         u2     # 位深
    compress:    u4     # 压缩类型（0=BI_RGB / 3=BI_BITFIELDS）
    color_num:   u4     # 调色板项数（0=按位深推定）
    top_down:    i4     # 1=自顶向下（原始 height<0）
    channels:    i4     # 输出通道 3/4
    row_size:    u4     # 每行字节数（4 字节对齐）
    rmask:       u4
    gmask:       u4
    bmask:       u4
    amask:       u4
    rsh:         u4     # 各通道掩码右移量（末尾 0 数）——解析时算一次，解码逐像素复用
    gsh:         u4
    bsh:         u4
    ash:         u4
    rmax:        u4     # 各通道量程（mask >> sh）——线性映射到 0..255 的分母
    gmax:        u4
    bmax:        u4
    amax:        u4
    fast32:      i4     # 1=32 位标准 BGRA 掩码，解码走直接字节搬运快路径
}

# -------- com 精确读/写（循环至满 n 字节；0=成功 / <0=错或意外 EOF）--------
fnc bmp_rd: i4, c: com&, buf: &, n: u4
    var p: u1& = (buf: u1&)
    var got: u4 = 0
    while got < n
        var want: u4 = n - got
        var r: i4 = c->read((&p[got]: &), &want)
        if r < 0
            return -1
        if want == 0
            return -1
        got = got + want
    return 0

fnc bmp_wr: i4, c: com&, buf: &, n: u4
    var p: u1& = (buf: u1&)
    var put: u4 = 0
    while put < n
        var want: u4 = n - put
        var r: i4 = c->write((&p[put]: &), &want)
        if r < 0
            return -1
        if want == 0
            return -1
        put = put + want
    return 0

# -------- 位运算辅助 --------
# 末尾连续 0 的个数（掩码 → 通道右移量）
fnc bmp_ctz: u4, v: u4
    if v == 0
        return 0
    var c: u4 = 0
    while (v & 1) == 0
        v = v >> 1
        c = c + 1
    return c

# 把 [0, max] 的通道值线性映射到 [0, 255]（四舍五入）
fnc bmp_scale: u4, v: u4, max: u4
    if max == 0
        return 0
    return (v * 255 + max / 2) / max

# -------- 解析 BMP 文件头 + DIB 头，填充 h（不读调色板/像素）--------
fnc bmp_parse_header: i4, c: com&, h: bmp_hdr&
    # 小端读字节缓冲：真内联块（值 inl，仅本函数使用；调用点原地展开，无函数调用开销）
    inl bmp_le_u4: u4, b: u1&, o: u4
        return (b[o]: u4) | ((b[o + 1]: u4) << 8) | ((b[o + 2]: u4) << 16) | ((b[o + 3]: u4) << 24)
    inl bmp_le_i4: i4, b: u1&, o: u4
        return (bmp_le_u4(b, o): i4)
    inl bmp_le_u2: u2, b: u1&, o: u4
        return ((b[o]: u4) | ((b[o + 1]: u4) << 8): u2)

    var base: i8 = c->seek(0, 1)                 # 当前绝对位置=文件起点
    if base < 0
        return -1
    h->base = base

    # 文件头 14 字节：magic(2) + file_size(4) + reserved(4) + offset(4)
    var fh[14]: u1
    if bmp_rd(c, (&fh[0]: &), 14) < 0
        return -1
    if fh[0] != 0x42 || fh[1] != 0x4D            # 'B','M'
        return -1
    h->pix_off = bmp_le_u4((&fh[0]: u1&), 10)

    # header_size（4 字节）
    var hs[4]: u1
    if bmp_rd(c, (&hs[0]: &), 4) < 0
        return -1
    var header_size: u4 = bmp_le_u4((&hs[0]: u1&), 0)
    h->header_size = header_size
    h->compress = 0
    h->rmask = 0
    h->gmask = 0
    h->bmask = 0
    h->amask = 0
    h->color_num = 0

    if header_size == 12
        # OS/2 1.x BITMAPCOREHEADER：width u2, height u2, planes u2, bpp u2（8 字节）
        var cb[8]: u1
        if bmp_rd(c, (&cb[0]: &), 8) < 0
            return -1
        h->width  = (bmp_le_u2((&cb[0]: u1&), 0): i4)
        h->height = (bmp_le_u2((&cb[0]: u1&), 2): i4)
        var planes: u2 = bmp_le_u2((&cb[0]: u1&), 4)
        h->bpp = bmp_le_u2((&cb[0]: u1&), 6)
        if planes != 1
            return -1
        if h->bpp == 16 || h->bpp == 32          # CORE 不支持 16/32
            return -1
    else
        # BITMAPINFOHEADER 及以上：读 36 字节（width..important_color_num）
        var ib[36]: u1
        if bmp_rd(c, (&ib[0]: &), 36) < 0
            return -1
        h->width     = bmp_le_i4((&ib[0]: u1&), 0)
        h->height    = bmp_le_i4((&ib[0]: u1&), 4)
        var planes: u2 = bmp_le_u2((&ib[0]: u1&), 8)
        h->bpp       = bmp_le_u2((&ib[0]: u1&), 10)
        h->compress  = bmp_le_u4((&ib[0]: u1&), 12)
        h->color_num = bmp_le_u4((&ib[0]: u1&), 28)
        if planes != 1
            return -1

    # 朝向
    h->top_down = 0
    if h->height < 0
        h->top_down = 1
        h->height = -h->height

    # 掩码 + 通道
    var channels: i4 = 3
    if header_size != 12 && h->compress == 3
        # BI_BITFIELDS：掩码位于 40 字节 INFOHEADER 之后（base+54），3 或 4 个 u4
        if h->bpp != 16 && h->bpp != 32
            return -1
        if c->seek(base + 54, 0) < 0
            return -1
        var nmask: u4 = 12
        if header_size >= 56
            nmask = 16
        var mb[16]: u1
        if bmp_rd(c, (&mb[0]: &), nmask) < 0
            return -1
        h->rmask = bmp_le_u4((&mb[0]: u1&), 0)
        h->gmask = bmp_le_u4((&mb[0]: u1&), 4)
        h->bmask = bmp_le_u4((&mb[0]: u1&), 8)
        if nmask == 16
            h->amask = bmp_le_u4((&mb[0]: u1&), 12)
        if h->amask != 0
            channels = 4
    else if h->compress != 0
        return -1                                # RLE 等压缩暂不支持
    else if h->bpp == 16
        h->rmask = 0x7C00                        # 默认 RGB555
        h->gmask = 0x03E0
        h->bmask = 0x001F
    else if h->bpp == 32
        h->rmask = 0x00FF0000
        h->gmask = 0x0000FF00
        h->bmask = 0x000000FF

    # 每行字节数（4 字节对齐）
    var bpp: u4 = (h->bpp: u4)
    var rs: u4 = 0
    if bpp < 16
        if bpp == 1
            rs = ((h->width: u4) + 7) / 8
        else if bpp == 4
            rs = ((h->width: u4) + 1) / 2
        else if bpp == 8
            rs = (h->width: u4)
        else
            return -1                            # 仅支持 1/4/8 调色板
    else
        if bpp != 16 && bpp != 24 && bpp != 32
            return -1
        rs = (h->width: u4) * (bpp / 8)
    rs = (rs + 3) & 0xFFFFFFFC
    h->row_size = rs
    h->channels = channels

    # 掩码右移量/量程：仅 16/32 位掩码路径用到；此处每图算一次，避免解码逐行重算 ctz
    h->rsh = bmp_ctz(h->rmask)
    h->gsh = bmp_ctz(h->gmask)
    h->bsh = bmp_ctz(h->bmask)
    h->rmax = h->rmask >> h->rsh
    h->gmax = h->gmask >> h->gsh
    h->bmax = h->bmask >> h->bsh
    h->ash = 0
    h->amax = 0
    if h->amask != 0
        h->ash = bmp_ctz(h->amask)
        h->amax = h->amask >> h->ash

    # 32 位标准 BGRA（R=0x00FF0000/G=0x0000FF00/B=0x000000FF，alpha 无或标准）→ 快路径
    h->fast32 = 0
    if h->bpp == 32 && h->rmask == 0x00FF0000 && h->gmask == 0x0000FF00 && h->bmask == 0x000000FF
        if h->amask == 0 || h->amask == 0xFF000000
            h->fast32 = 1
    return 0

# -------- 解码一行像素到 out（out[dbase..]，RGB(A)）--------
fnc bmp_decode_row: src: u1&, out: u1&, dbase: u4, w: u4, ch: u4, h: bmp_hdr&, pal: u1&
    var bpp: u4 = (h->bpp: u4)
    var o: u4 = dbase
    var i: u4 = 0
    if bpp == 1
        while i < w
            var byte: u4 = (src[i >> 3]: u4)
            var bit: u4 = 7 - (i & 7)
            var idx: u4 = (byte >> bit) & 1
            out[o]     = pal[idx * 4 + 2]
            out[o + 1] = pal[idx * 4 + 1]
            out[o + 2] = pal[idx * 4 + 0]
            if ch == 4
                out[o + 3] = 255
            o = o + ch
            i = i + 1
    else if bpp == 4
        while i < w
            var byte: u4 = (src[i >> 1]: u4)
            var idx: u4 = 0
            if (i & 1) == 0
                idx = (byte >> 4) & 0x0F
            else
                idx = byte & 0x0F
            out[o]     = pal[idx * 4 + 2]
            out[o + 1] = pal[idx * 4 + 1]
            out[o + 2] = pal[idx * 4 + 0]
            if ch == 4
                out[o + 3] = 255
            o = o + ch
            i = i + 1
    else if bpp == 8
        while i < w
            var idx: u4 = (src[i]: u4)
            out[o]     = pal[idx * 4 + 2]
            out[o + 1] = pal[idx * 4 + 1]
            out[o + 2] = pal[idx * 4 + 0]
            if ch == 4
                out[o + 3] = 255
            o = o + ch
            i = i + 1
    else if bpp == 24
        var s: u4 = 0
        while i < w
            out[o]     = src[s + 2]
            out[o + 1] = src[s + 1]
            out[o + 2] = src[s + 0]
            if ch == 4
                out[o + 3] = 255
            o = o + ch
            s = s + 3
            i = i + 1
    else if h->fast32 != 0
        # 32 位标准 BGRA 快路径：直接字节搬运，免拼装 u32 / 掩码 / 移位 / 缩放
        var s: u4 = 0
        while i < w
            out[o]     = src[s + 2]
            out[o + 1] = src[s + 1]
            out[o + 2] = src[s + 0]
            if ch == 4
                if h->amask != 0
                    out[o + 3] = src[s + 3]
                else
                    out[o + 3] = 255
            o = o + ch
            s = s + 4
            i = i + 1
    else
        # 16 / 32 位：按掩码取通道并线性映射到 0..255（移量/量程已在 parse_header 预算）
        var bpb: u4 = bpp / 8
        var s: u4 = 0
        while i < w
            var p: u4 = 0
            if bpb == 4
                p = (src[s]: u4) | ((src[s + 1]: u4) << 8) | ((src[s + 2]: u4) << 16) | ((src[s + 3]: u4) << 24)
            else
                p = (src[s]: u4) | ((src[s + 1]: u4) << 8)
            out[o]     = (bmp_scale((p & h->rmask) >> h->rsh, h->rmax): u1)
            out[o + 1] = (bmp_scale((p & h->gmask) >> h->gsh, h->gmax): u1)
            out[o + 2] = (bmp_scale((p & h->bmask) >> h->bsh, h->bmax): u1)
            if ch == 4
                if h->amask != 0
                    out[o + 3] = (bmp_scale((p & h->amask) >> h->ash, h->amax): u1)
                else
                    out[o + 3] = 255
            o = o + ch
            s = s + bpb
            i = i + 1
    return
