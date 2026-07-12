# img_tga —— TGA（Truevision）格式私有实现（由 img.sc 经 `add img_tga.sc` 内联为子模块）
#
# 依赖：img 结构（定义于 img.sc）+ io.sc（com I/O）+ mem.sc（chunk/recycle）+ codec.sc（流式 RLE）。
# 本文件是 img.sc 的内联子单元：只含 TGA 的私有类型与辅助函数（tga_hdr / tga_rd·wr /
#   tga_parse_header），经 add 拼接后对 img.sc 可见；公开接口 tga_read/tga_write/tga_shape
#   定义在 img.sc。单独编译不完整（缺 img 结构）。
#
# ================================ TGA ================================
# 协议：https://en.wikipedia.org/wiki/Truevision_TGA —— 18 字节头（小端）。
# 支持读（对齐 c_format_tga）：
#   类型位 img_type = (RLE 位 0x08) | (基类型 0x07)；基类型 0=空图 / 1=索引色(COLORMAP)
#     / 2=真彩色(TRUECOLOR) / 3=灰度(MONO)；RLE 位与基类型正交（任一基类型可行程编码）。
#   位深：真彩色 15/16/24/32（15/16 为 5-5-5[+alpha 位]）；灰度 8/16；索引 8/16（调色板条目 15/16/24/32）。
#   输出统一 RGB(A)、上→下；alpha 按格式自动判定（含 alpha → 4 通道，否则 3）；
#     描述符 bit5 决定源朝向（1=自顶向下 / 0=自底向上）。
#   tga_read(c, info, alpha_mode, flip_mode)：alpha_mode 对齐 iAlphaMode（<0 强 RGB/0 自动/>0 强 RGBA），
#     flip_mode 对齐 iFlipMode（0 按存储/1 归一化上→下/>1 翻转/<0 归一化下→上）。
# 支持写（对齐 F_tga_save）：truecolor 24/32，tga_write(c, info, invert_mode, rle)：rle 选 RLE(10)/未压缩(2)，
#   invert_mode 对齐 iInvertMode（朝向标记 + 源行翻转）。
# 行程编码复用 builtins/codec 的流式 RLE（unit=源每像素字节数，TGA 兼容控制字节）；
#   写时 RLE 包不跨扫描行——每写完一行即 flush，符合 TGA 惯例。

inc io.sc
inc mem.sc
inc codec.sc

# 内部：解析后的 TGA 头信息
def tga_hdr: {
    base:      i8   # 文件起始绝对位置（com 当前位置）
    id_len:    i4   # 图像 ID 字段长度
    img_type:  i4   # 原始类型字节
    base_type: i4   # img_type & 0x07（0/1/2/3）
    is_rle:    i4   # (img_type & 0x08) != 0
    empty:     i4   # img_type == 0（空图）
    width:     i4
    height:    i4
    bpp:       i4   # 像素/索引位深
    sbpp:      i4   # 源每像素字节数 = bpp/8（RLE unit）
    map_type:  i4   # 颜色表类型 0/1
    map_entry: i4   # 颜色表入口索引（字节偏移基准）
    map_num:   i4   # 颜色表条目数
    cf:        i4   # 颜色分量字节数（2=15/16位 / 3=24 / 4=32）
    b15:       i4   # 15 位标志（无 alpha 位）
    channels:  i4   # 输出通道 3/4
    alpha:     i4   # 输出是否含 alpha
    top_down:  i4   # 源朝向：1=自顶向下（描述符 bit5=1）
}

# -------- com 精确读/写（循环至满 n 字节；0=成功 / <0=错或意外 EOF）--------
fnc tga_rd: i4, c: com&, buf: &, n: u4
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

fnc tga_wr: i4, c: com&, buf: &, n: u4
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

# -------- 解析 TGA 18 字节头，填充 h，并把 com 定位到（跳过 ID 字段后的）颜色表/像素起点 --------
# 对齐 c_format_tga：支持空图/灰度/索引色/真彩色 与 15/16/24/32 位、RLE 位与基类型正交。
fnc tga_parse_header: i4, c: com&, h: tga_hdr&
    var base: i8 = c->seek(0, 1)                  # 当前绝对位置=文件起点
    if base < 0
        return -1
    h->base = base

    var hd[18]: u1
    if tga_rd(c, (&hd[0]: &), 18) < 0
        return -1
    h->id_len    = (hd[0]: i4)
    h->map_type  = (hd[1]: i4)
    h->img_type  = (hd[2]: i4)
    h->map_entry = ((hd[3]: i4) | ((hd[4]: i4) << 8))
    h->map_num   = ((hd[5]: i4) | ((hd[6]: i4) << 8))
    var map_bpp: i4 = (hd[7]: i4)                 # 调色板条目位深
    h->width     = ((hd[12]: i4) | ((hd[13]: i4) << 8))
    h->height    = ((hd[14]: i4) | ((hd[15]: i4) << 8))
    var bpp: i4  = (hd[16]: i4)
    h->bpp = bpp
    if h->width <= 0 || h->height <= 0
        return -1
    if (hd[17] & 0x20) != 0
        h->top_down = 1
    else
        h->top_down = 0

    h->base_type = (h->img_type & 0x07)
    if (h->img_type & 0x08) != 0
        h->is_rle = 1
    else
        h->is_rle = 0
    h->empty = 0
    h->b15 = 0
    h->cf = 0
    h->sbpp = 0

    if h->img_type == 0                           # 空图（黑色区域，无像素/调色板数据）
        h->empty = 1
        if bpp == 16 || bpp == 24
            h->alpha = 1
        else
            h->alpha = 0
    else if h->base_type == 3                        # MONO 灰度
        if bpp != 8 && bpp != 16
            return -1
        h->sbpp = bpp / 8
        if bpp == 16                              # 16 位灰度第二字节为 alpha
            h->alpha = 1
        else
            h->alpha = 0
    else if h->base_type == 1                        # COLORMAP 索引色
        if h->map_type == 0                       # 索引色必须带调色板
            return -1
        if bpp != 8 && bpp != 16
            return -1
        h->sbpp = bpp / 8
        var cf: i4 = map_bpp
        if cf == 15
            cf = 16
            h->b15 = 1
        if (cf & 0x07) != 0                       # 调色板位深非 8 的倍数
            return -1
        cf = cf / 8
        if cf < 2 || cf > 4
            return -1
        h->cf = cf
        if cf == 4 || (cf == 2 && h->b15 == 0)
            h->alpha = 1
        else
            h->alpha = 0
    else if h->base_type == 2                        # TRUECOLOR 真彩色
        var pb: i4 = bpp
        if pb == 15
            pb = 16
            h->b15 = 1
        if (pb & 0x07) != 0
            return -1
        var cf: i4 = pb / 8
        if cf < 2 || cf > 4
            return -1
        h->cf = cf
        h->sbpp = cf
        if cf == 4 || (cf == 2 && h->b15 == 0)
            h->alpha = 1
        else
            h->alpha = 0
    else
        return -1                                 # 不支持的基类型

    if h->alpha != 0
        h->channels = 4
    else
        h->channels = 3

    # 跳过图像 ID 字段，定位到颜色表（若有）或像素数据起点
    if c->seek(base + 18 + (h->id_len: i8), 0) < 0
        return -1
    return 0
