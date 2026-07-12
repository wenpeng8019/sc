# img —— 多格式图像编解码模块（templates 通用 utils 组件，非语言基础模块）
#
# 设计约定（逐格式实现，各格式独立一个 .sc，经 add 内联拼接进本单元）：
#   1. 接口命名：<格式>_read / <格式>_write / <格式>_shape（如 bmp_read/bmp_write/bmp_shape）。
#   2. 统一用 com 对象做数据 I/O（inc io.sc；file/stream 等可寻址设备）。像素随机寻址依赖
#      com 的 seek（whence 0=SET/1=CUR/2=END），故要求传入的 com 可寻址。
#   3. 纯 sc 实现，不用 _glue.c。动态内存经 mem.sc（chunk/recycle）。
#   4. 涉及压缩的格式统一走 builtins/codec（BMP 未压缩，暂无 codec 依赖）。
#   5. 目标「边 I/O 边编解码」：逐行读写、就地转换，不整文件载入内存（BMP 已按行流式）。
#   6. 各格式实现为内联子模块 img_<格式>.sc，在文件末尾用 `add` 拼接进本单元；
#      本文件仅承载跨格式共享的 img 结构与格式子模块清单。
#
# 像素统一表示（img 结构）：行优先、从上到下；通道 R,G,B[,A] 或灰度（channels=1/2/3/4）；
#   每通道位深 depth=8 或 16（16 位样本按本机字节序 u2 存于 pixels）。
#
# 用法：
#   inc ../../templates/.scenv/modules/img/img.sc   # 或按实际相对路径
#   var info: img
#   if bmp_shape(c, &info) == 0            # 只读元信息（宽/高/通道）
#       ...
#   if bmp_read(c, &info, 0, 1) == 0      # 解码：alpha_mode=0 自动 / flip_mode=1 归一自顶向下
#       ... info.pixels ...
#       recycle((info.pixels: &))
#   bmp_write(c, &info, 0)                 # 编码写出（channels 3→24bit / 4→32bit；invert_mode=0 自顶向下）

inc io.sc
inc mem.sc

# ---------------- 图像数据结构 ----------------
# 像素缓冲行优先、上→下、通道次序 R,G,B[,A] 或灰度；<格式>_read 分配、调用方 recycle。
@def img: {
    width:    i4        # 宽（像素）
    height:   i4        # 高（像素）
    channels: i4        # 1=灰度 / 2=灰度+alpha / 3=RGB / 4=RGBA
    depth:    i4        # 每通道位深：8 或 16（16 位样本在 pixels 中按本机字节序 u2 存）
    pixels:   u1&       # 像素缓冲（<格式>_read 分配；<格式>_shape 置 nil）
}

# ---------------- 各格式私有实现（内联子模块）----------------
# 每种格式的私有实现独立一个 img_<格式>.sc（头结构 + I/O 辅助 + 解析/解码），经 add
# 内联拼接进本单元；公开接口 <格式>_read/write/shape 则定义在本文件下方。
# 新增格式：加一行 add，并在下方补充对应的 @公开接口。
add img_bmp.sc          # BMP 私有实现：bmp_hdr / bmp_rd·wr / bmp_ctz·scale / bmp_parse_header / bmp_decode_row
add img_tga.sc          # TGA 私有实现：tga_hdr / tga_rd·wr / tga_parse_header（RLE 复用 codec 流式）
add img_png.sc          # PNG 私有实现：png_parse / png_create(_raw) / png_trans / png_expand_pal / png_conv / png_enc_line（zlib+crc 复用 codec）
add img_jpg.sc          # JPEG 私有实现：容器 marker 解析 / IDCT·FDCT / 反量化·量化 / 上采样 / 颜色变换（熵层复用 img_jpg_codec）

# ================================ BMP 公开接口 ================================

# -------- bmp_shape：仅读头，填充 width/height/channels（不读像素，不分配）--------
@fnc bmp_shape: i4, c: com&, info: img&
    var h: bmp_hdr
    if bmp_parse_header(c, &h) < 0
        return -1
    info->width    = h.width
    info->height   = h.height
    info->channels = h.channels
    info->depth    = 8
    info->pixels   = nil
    return 0

# -------- bmp_read：解码整幅图，分配 info.pixels（RGB/RGBA），回填元信息 --------
# 逐行从 com 读取并就地转换（不整文件缓冲）；成功返回 0，失败 <0。用后 recycle(info.pixels)。
#   alpha_mode（对齐参考 iAlphaMode）：<0 强制 RGB / 0 自动（有 alpha 掩码则 RGBA）/ >0 强制 RGBA。
#   flip_mode （对齐参考 iFlipMode）：0 按存储朝向 / 1 归一为自顶向下 / >1 相对存储翻转 / <0 归一为自底向上。
@fnc bmp_read: i4, c: com&, info: img&, alpha_mode: i4, flip_mode: i4
    var h: bmp_hdr
    if bmp_parse_header(c, &h) < 0
        return -1

    var w: u4 = (h.width: u4)
    var ht: u4 = (h.height: u4)
    if w == 0 || ht == 0
        return -1

    # 输出通道数：alpha_mode 覆盖自动判定（<0 去 alpha / >0 加 alpha / 0 沿用头部推定）
    var ch: u4 = (h.channels: u4)
    if alpha_mode < 0
        ch = 3
    else if alpha_mode > 0
        ch = 4

    # 目标朝向 out_td：1=自顶向下 / 0=自底向上（对齐参考 iFlipMode 多级语义）
    var out_td: i4 = h.top_down
    if flip_mode == 1
        out_td = 1
    else if flip_mode > 1
        out_td = 1 - h.top_down
    else if flip_mode < 0
        out_td = 0

    # 调色板（bpp<16）：位于 DIB 头之后 base+14+header_size
    var pal[1024]: u1                            # 256 项 × (b,g,r,a)
    if (h.bpp: u4) < 16
        var cn: u4 = h.color_num
        if cn == 0
            cn = (1: u4) << (h.bpp: u4)
        if cn > 256
            cn = 256
        if c->seek(h.base + 14 + (h.header_size: i8), 0) < 0
            return -1
        if h.header_size == 12
            var i: u4 = 0
            while i < cn
                var e[3]: u1                     # CORE 调色板每项 3 字节 b,g,r
                if bmp_rd(c, (&e[0]: &), 3) < 0
                    return -1
                pal[i * 4 + 0] = e[0]
                pal[i * 4 + 1] = e[1]
                pal[i * 4 + 2] = e[2]
                pal[i * 4 + 3] = 0
                i = i + 1
        else
            if bmp_rd(c, (&pal[0]: &), cn * 4) < 0
                return -1

    # 分配输出
    var out: u1& = (chunk((w * ht * ch: u8)): u1&)
    if out == nil
        return -1
    # 行缓冲
    var row: u1& = (chunk((h.row_size: u8)): u1&)
    if row == nil
        recycle((out: &))
        return -1

    # 定位到像素数据
    if c->seek(h.base + (h.pix_off: i8), 0) < 0
        recycle((row: &))
        recycle((out: &))
        return -1

    var srow: u4 = 0
    while srow < ht
        if bmp_rd(c, (&row[0]: &), h.row_size) < 0
            recycle((row: &))
            recycle((out: &))
            return -1
        # 文件行 srow → 图像行（按存储朝向），再按目标朝向定位输出行
        var img_row: u4 = srow
        if h.top_down == 0
            img_row = ht - 1 - srow
        var orow: u4 = img_row
        if out_td == 0
            orow = ht - 1 - img_row
        bmp_decode_row((&row[0]: u1&), (&out[0]: u1&), orow * w * ch, w, ch, &h, (&pal[0]: u1&))
        srow = srow + 1

    recycle((row: &))
    info->width    = h.width
    info->height   = (ht: i4)
    info->channels = (ch: i4)
    info->depth    = 8
    info->pixels   = out
    return 0

# -------- bmp_write：把 info（RGB/RGBA 像素）编码为 BMP 写入 com --------
# channels 3→24 位(BI_RGB) / 4→32 位(BI_BITFIELDS+alpha)；成功返回 0，失败 <0。
#   invert_mode（对齐参考 iInvertMode）：>0 → height 存正值(自底向上标记)，否则存负值(自顶向下标记)；
#     源行翻转 = (invert_mode<0 || invert_mode>1)。默认 0：自顶向下存储、不翻转（与参考默认一致）。
@fnc bmp_write: i4, c: com&, info: img&, invert_mode: i4
    # 小端写字节缓冲：真内联块（void inl，仅本函数使用；当语句用，调用点原地展开）
    inl bmp_pe_u4: b: u1&, o: u4, v: u4
        b[o]     = (v & 0xFF: u1)
        b[o + 1] = ((v >> 8) & 0xFF: u1)
        b[o + 2] = ((v >> 16) & 0xFF: u1)
        b[o + 3] = ((v >> 24) & 0xFF: u1)
    inl bmp_pe_i4: b: u1&, o: u4, v: i4
        bmp_pe_u4(b, o, (v: u4))
    inl bmp_pe_u2: b: u1&, o: u4, v: u2
        b[o]     = ((v: u4) & 0xFF: u1)
        b[o + 1] = (((v: u4) >> 8) & 0xFF: u1)

    var ch: u4 = (info->channels: u4)
    if ch != 3 && ch != 4
        return -1
    var w: u4 = (info->width: u4)
    var ht: u4 = (info->height: u4)
    if w == 0 || ht == 0
        return -1

    var bpp: u4 = ch * 8
    var rowb: u4 = 0
    if ch == 3
        rowb = w * 3
        rowb = (rowb + 3) & 0xFFFFFFFC           # 4 字节对齐
    else
        rowb = w * 4                             # 32 位天然对齐

    var dib_size: u4 = 40
    if ch == 4
        dib_size = 56                            # + 4 个掩码
    var pix_off: u4 = 14 + dib_size
    var img_bytes: u4 = rowb * ht
    var file_size: u4 = pix_off + img_bytes

    # 文件头 14 字节
    var fh[14]: u1
    fh[0] = 0x42
    fh[1] = 0x4D
    bmp_pe_u4((&fh[0]: u1&), 2, file_size)
    bmp_pe_u4((&fh[0]: u1&), 6, 0)               # reserved1/2
    bmp_pe_u4((&fh[0]: u1&), 10, pix_off)
    if bmp_wr(c, (&fh[0]: &), 14) < 0
        return -1

    # DIB 头（含 header_size 字段，最长 56 字节）
    var dib[56]: u1
    var z: u4 = 0
    while z < 56
        dib[z] = 0
        z = z + 1
    bmp_pe_u4((&dib[0]: u1&), 0, dib_size)
    bmp_pe_i4((&dib[0]: u1&), 4, (w: i4))
    var hval: i4 = (ht: i4)                       # invert_mode>0 存正(自底向上标记)，否则存负(自顶向下标记)
    if invert_mode <= 0
        hval = -(ht: i4)
    bmp_pe_i4((&dib[0]: u1&), 8, hval)
    bmp_pe_u2((&dib[0]: u1&), 12, 1)             # planes
    bmp_pe_u2((&dib[0]: u1&), 14, (bpp: u2))
    var compress: u4 = 0
    if ch == 4
        compress = 3                             # BI_BITFIELDS
    bmp_pe_u4((&dib[0]: u1&), 16, compress)
    bmp_pe_u4((&dib[0]: u1&), 20, img_bytes)
    bmp_pe_i4((&dib[0]: u1&), 24, 2835)          # x_ppm（~72 DPI）
    bmp_pe_i4((&dib[0]: u1&), 28, 2835)          # y_ppm
    if ch == 4
        bmp_pe_u4((&dib[0]: u1&), 40, 0x00FF0000)   # R
        bmp_pe_u4((&dib[0]: u1&), 44, 0x0000FF00)   # G
        bmp_pe_u4((&dib[0]: u1&), 48, 0x000000FF)   # B
        bmp_pe_u4((&dib[0]: u1&), 52, 0xFF000000)   # A
    if bmp_wr(c, (&dib[0]: &), dib_size) < 0
        return -1

    # 像素：按文件行顺序写出，源行是否翻转由 invert_mode 决定；行内 BGR(A)
    var flip: i4 = 0
    if invert_mode < 0 || invert_mode > 1
        flip = 1
    var row: u1& = (chunk((rowb: u8)): u1&)
    if row == nil
        return -1
    var pi: u4 = 0
    while pi < rowb
        row[pi] = 0                              # 清零（含行尾补位）
        pi = pi + 1
    var src: u1& = info->pixels
    var y: u4 = 0
    while y < ht
        var sy: u4 = y                           # 源行（翻转时取镜像行）
        if flip != 0
            sy = ht - 1 - y
        var sbase: u4 = sy * w * ch
        var x: u4 = 0
        var o: u4 = 0
        while x < w
            var sb: u4 = sbase + x * ch
            row[o]     = src[sb + 2]             # B
            row[o + 1] = src[sb + 1]             # G
            row[o + 2] = src[sb + 0]             # R
            if ch == 4
                row[o + 3] = src[sb + 3]         # A
            o = o + ch
            x = x + 1
        if bmp_wr(c, (&row[0]: &), rowb) < 0
            recycle((row: &))
            return -1
        y = y + 1
    recycle((row: &))
    return 0

# ================================ TGA 公开接口 ================================

# -------- tga_shape：仅读头，填充 width/height/channels（不读像素，不分配）--------
@fnc tga_shape: i4, c: com&, info: img&
    var h: tga_hdr
    if tga_parse_header(c, &h) < 0
        return -1
    info->width    = h.width
    info->height   = h.height
    info->channels = h.channels
    info->depth    = 8
    info->pixels   = nil
    return 0

# -------- tga_read：全功能读，对齐 c_format F_tga_load_from_file（alpha_mode/flip_mode 语义一致）。--------
# alpha_mode：<0=强制 RGB / 0=按格式自动 / >0=强制 RGBA（对齐 iAlphaMode）。
# flip_mode：0=按存储朝向 / 1=归一化自顶向下 / >1=翻转存储朝向 / <0=归一化自底向上（对齐 iFlipMode）。
# 支持 空图 / 灰度(MONO) / 索引色(COLORMAP) / 真彩色，位深 15/16/24/32，RLE 位与基类型正交。
#   RLE 走 codec 流式解码（整块喂，产出满即优雅停）。成功 0，失败 <0。用后 recycle(info.pixels)。
@fnc tga_read: i4, c: com&, info: img&, alpha_mode: i4, flip_mode: i4
    var h: tga_hdr
    if tga_parse_header(c, &h) < 0
        return -1
    var w: u4 = (h.width: u4)
    var ht: u4 = (h.height: u4)
    var och: u4 = (h.channels: u4)               # 输出通道：自动=按格式
    if alpha_mode < 0
        och = 3                                   # 强制 RGB
    else if alpha_mode > 0
        och = 4                                   # 强制 RGBA
    var npix: u8 = (w * ht: u8)
    var total: u8 = (npix * (och: u8))

    var out: u1& = (chunk(total): u1&)
    if out == nil
        return -1

    # 空图（类型 0）：全 0（黑；含 alpha 则透明）
    if h.empty != 0
        var i: u8 = 0
        while i < total
            out[i] = 0
            i = i + 1
        info->width    = h.width
        info->height   = h.height
        info->channels = (och: i4)
        info->depth    = 8
        info->pixels   = out
        return 0

    # 颜色表（索引色）：读 cf*map_num 字节到 palette
    var palette: u1& = nil
    var pbytes: u8 = 0
    if h.map_type != 0
        pbytes = ((h.cf: u8) * (h.map_num: u8))
        if pbytes > 0
            palette = (chunk(pbytes): u1&)
            if palette == nil
                recycle((out: &))
                return -1
            if tga_rd(c, (&palette[0]: &), (pbytes: u4)) < 0
                recycle((palette: &))
                recycle((out: &))
                return -1

    # 源像素（存储朝向，sbpp 字节/像素）：RLE 解码或原样读取
    var sbpp: u4 = (h.sbpp: u4)
    var srcbytes: u8 = (npix * (sbpp: u8))
    var src: u1& = (chunk(srcbytes): u1&)
    if src == nil
        if palette != nil
            recycle((palette: &))
        recycle((out: &))
        return -1

    if h.is_rle != 0
        # 源像素粒度 RLE（unit=sbpp）。整块喂，cap=剩余字节，忽略 com 尾部补零。
        var d: codec_rle_dec
        codec_rle_dec_init(&d, (sbpp: i4))
        var produced: u8 = 0
        var inbuf[512]: u1
        while produced < srcbytes
            var want: u4 = 512
            var r: i4 = c->read((&inbuf[0]: &), &want)
            if r < 0 || want == 0
                if palette != nil
                    recycle((palette: &))
                recycle((src: &))
                recycle((out: &))
                return -1
            var m: i8 = codec_rle_dec_feed(&d, (&inbuf[0]: u1&), (want: u8), (&src[produced]: u1&), (srcbytes - produced))
            produced = produced + (m: u8)
    else
        if tga_rd(c, (&src[0]: &), (srcbytes: u4)) < 0
            if palette != nil
                recycle((palette: &))
            recycle((src: &))
            recycle((out: &))
            return -1

    # 输出朝向（对齐 iFlipMode）：out_td=1 输出自顶向下 / 0 自底向上
    var out_td: i4 = h.top_down                   # flip_mode==0：按存储朝向
    if flip_mode == 1
        out_td = 1                                # 归一化自顶向下
    else if flip_mode > 1
        if h.top_down != 0                        # 翻转存储朝向
            out_td = 0
        else
            out_td = 1
    else if flip_mode < 0
        out_td = 0                                # 归一化自底向上

    # 展开每像素为 (r,g,b,a) 并按输出朝向落位；alpha 是否写出由 och 决定
    var bt: i4 = h.base_type
    var cf: u4 = (h.cf: u4)
    var b15: i4 = h.b15
    var mentry: u4 = (h.map_entry: u4)
    var oy: u4 = 0
    while oy < ht
        # 输出行 oy → 图像行 image_row → 源文件扫描行 sy
        var image_row: u4 = oy
        if out_td == 0
            image_row = ht - 1 - oy
        var sy: u4 = image_row
        if h.top_down == 0
            sy = ht - 1 - image_row
        var x: u4 = 0
        while x < w
            var si: u4 = (sy * w + x) * sbpp
            var di: u4 = (oy * w + x) * och
            var rr: u1 = (0: u1)
            var gg: u1 = (0: u1)
            var bb: u1 = (0: u1)
            var aa: u1 = (0xFF: u1)
            if bt == 3
                # 灰度：R=G=B=src[si]；16 位第二字节为 alpha
                var g: u1 = src[si]
                rr = g
                gg = g
                bb = g
                if sbpp > 1
                    aa = src[si + 1]
            else
                # 真彩色/索引色：定位分量指针 P[poff]（BGR(A) 序）
                var P: u1& = (src: u1&)
                var poff: u4 = si
                if bt == 1
                    var idx: u4 = (src[si]: u4)
                    if sbpp > 1
                        idx = (src[si]: u4) | ((src[si + 1]: u4) << 8)
                    var boff: u4 = idx * cf
                    if boff < mentry                     # 索引越界（下溢）
                        if palette != nil
                            recycle((palette: &))
                        recycle((src: &))
                        recycle((out: &))
                        return -1
                    boff = boff - mentry
                    if ((boff: u8) + (cf: u8)) > pbytes  # 索引越界（上溢）
                        if palette != nil
                            recycle((palette: &))
                        recycle((src: &))
                        recycle((out: &))
                        return -1
                    P = (palette: u1&)
                    poff = boff
                if cf == 2
                    # 15/16 位 5-5-5[+alpha 位]
                    var c16: u4 = (P[poff]: u4) | ((P[poff + 1]: u4) << 8)
                    rr = ((((c16 >> 10) & 0x1F) << 3): u1)
                    gg = ((((c16 >> 5) & 0x1F) << 3): u1)
                    bb = (((c16 & 0x1F) << 3): u1)
                    if b15 != 0 || (c16 & 0x8000) != 0
                        aa = (0xFF: u1)
                    else
                        aa = (0: u1)
                else if cf == 3
                    rr = P[poff + 2]
                    gg = P[poff + 1]
                    bb = P[poff + 0]
                else
                    rr = P[poff + 2]
                    gg = P[poff + 1]
                    bb = P[poff + 0]
                    aa = P[poff + 3]
            out[di]     = rr
            out[di + 1] = gg
            out[di + 2] = bb
            if och == 4
                out[di + 3] = aa
            x = x + 1
        oy = oy + 1

    recycle((src: &))
    if palette != nil
        recycle((palette: &))
    info->width    = h.width
    info->height   = h.height
    info->channels = (och: i4)
    info->depth    = 8
    info->pixels   = out
    return 0

# -------- tga_write：全功能写，对齐 c_format F_tga_save_to_file（truecolor 24/32）。--------
# invert_mode：0=自顶向下存储(identity) / >0=标记自底向上 / (<0 或 >1)=额外翻转源行（对齐 iInvertMode）。
# rle：非 0=RLE(类型 10) / 0=未压缩(类型 2)（对齐 bRLE）。像素 BGR(A)；成功 0，失败 <0。
@fnc tga_write: i4, c: com&, info: img&, invert_mode: i4, rle: i4
    # 小端写 16 位：真内联块（void inl，仅本函数使用）
    inl tga_wle16: b: u1&, o: u4, v: u4
        b[o]     = (v & 0xFF: u1)
        b[o + 1] = ((v >> 8) & 0xFF: u1)

    var ch: u4 = (info->channels: u4)
    if ch != 3 && ch != 4
        return -1
    var w: u4 = (info->width: u4)
    var ht: u4 = (info->height: u4)
    if w == 0 || ht == 0
        return -1

    # 18 字节头
    var hd[18]: u1
    var z: u4 = 0
    while z < 18
        hd[z] = 0
        z = z + 1
    if rle != 0
        hd[2] = 10                                # RLE truecolor
    else
        hd[2] = 2                                 # 未压缩 truecolor
    tga_wle16((&hd[0]: u1&), 12, w)
    tga_wle16((&hd[0]: u1&), 14, ht)
    hd[16] = (ch * 8: u1)                         # 位深
    var desc: u4 = 0
    if invert_mode <= 0
        desc = 0x20                               # bit5=1 标记自顶向下（invert_mode>0 则标记自底向上）
    if ch == 4
        desc = desc | 0x08                        # 低 4 位=8 alpha 位
    hd[17] = (desc: u1)
    var flip: i4 = 0
    if invert_mode < 0 || invert_mode > 1
        flip = 1                                  # 额外翻转源行
    if tga_wr(c, (&hd[0]: &), 18) < 0
        return -1

    # 单行缓冲：BGR(A)；RLE 模式再备一块压缩输出
    var bound: u8 = codec_rle_enc_bound((w: u8), (ch: i4))
    var rowbuf: u1& = (chunk((w * ch: u8)): u1&)
    if rowbuf == nil
        return -1
    var encbuf: u1& = nil
    if rle != 0
        encbuf = (chunk(bound): u1&)
        if encbuf == nil
            recycle((rowbuf: &))
            return -1

    var e: codec_rle_enc
    if rle != 0
        codec_rle_enc_init(&e, (ch: i4))
    var src: u1& = info->pixels
    var y: u4 = 0
    while y < ht
        var sy: u4 = y
        if flip != 0
            sy = ht - 1 - y                       # 翻转：文件首行=图像底行
        var sbase: u4 = sy * w * ch
        var x: u4 = 0
        var o: u4 = 0
        while x < w
            var sb: u4 = sbase + x * ch
            rowbuf[o]     = src[sb + 2]           # B
            rowbuf[o + 1] = src[sb + 1]           # G
            rowbuf[o + 2] = src[sb + 0]           # R
            if ch == 4
                rowbuf[o + 3] = src[sb + 3]       # A
            o = o + ch
            x = x + 1
        if rle != 0
            # 逐行 RLE（复用 codec 流式；每行 flush，行程包不跨扫描行）
            var n: i8 = codec_rle_enc_feed(&e, (&rowbuf[0]: u1&), (w: u8), (&encbuf[0]: u1&), bound)
            if n < 0
                recycle((encbuf: &))
                recycle((rowbuf: &))
                return -1
            var nf: i8 = codec_rle_enc_flush(&e, (&encbuf[n]: u1&), (bound - (n: u8)))
            if nf < 0
                recycle((encbuf: &))
                recycle((rowbuf: &))
                return -1
            if tga_wr(c, (&encbuf[0]: &), ((n + nf): u4)) < 0
                recycle((encbuf: &))
                recycle((rowbuf: &))
                return -1
        else
            # 未压缩：整行 BGR(A) 直写
            if tga_wr(c, (&rowbuf[0]: &), (w * ch: u4)) < 0
                recycle((rowbuf: &))
                return -1
        y = y + 1

    if encbuf != nil
        recycle((encbuf: &))
    recycle((rowbuf: &))
    return 0

# ================================ PNG 公开接口 ================================

# -------- png_shape：仅读头信息（IHDR + PLTE/tRNS 扫描），填 width/height/channels/depth --------
# channels 反映按文件原生解码（req_comp=0）会得到的通道数（含 tRNS 追加 alpha / 调色板展开）。
@fnc png_shape: i4, c: com&, info: img&
    var a: png_img
    var m: png_meta
    if png_parse(c, &a, &m, 1, 0) < 0
        return -1
    info->width    = a.w
    info->height   = a.h
    info->channels = m.out_n
    info->depth    = (m.depth == 16 ? 16 : 8)
    info->pixels   = nil
    return 0

# -------- png_read：全功能解码，分配 info.pixels，回填元信息 --------
# 覆盖 color 0/2/3/4/6、depth 1/2/4/8/16、Adam7 隔行、tRNS、5 种过滤器、调色板；输出统一
#   为上→下、通道 R,G,B[,A] 或灰度。成功返回 0，失败 <0。用后 recycle(info.pixels)。
#   req_comp（对齐 stb desired_channels）：0=按文件原生通道 / 1=灰 / 2=灰A / 3=RGB / 4=RGBA。
#   flip_mode（对齐参考 iFlipMode）：0/1=保持自顶向下（PNG 原生朝向）/ >1 或 <0=翻转为自底向上。
@fnc png_read: i4, c: com&, info: img&, req_comp: i4, flip_mode: i4
    var a: png_img
    var m: png_meta
    if png_parse(c, &a, &m, 0, req_comp) < 0
        return -1
    var out_n: i4 = m.out_n
    if req_comp != 0 && req_comp != out_n
        var nb: u1& = nil
        if m.depth == 8
            nb = png_conv8(a.out, out_n, req_comp, (a.w: u4), (a.h: u4))
        else
            nb = png_conv16(a.out, out_n, req_comp, (a.w: u4), (a.h: u4))
        if nb == nil
            return -1
        a.out = nb
        out_n = req_comp
    if flip_mode < 0 || flip_mode > 1
        var bpp: u4 = (out_n: u4) * (m.depth == 16 ? 2 : 1)
        png_flip_rows(a.out, (a.w: u4), (a.h: u4), bpp)
    info->width    = a.w
    info->height   = a.h
    info->channels = out_n
    info->depth    = (m.depth == 16 ? 16 : 8)
    info->pixels   = a.out
    return 0

# -------- png_write：编码写出（8 位；channels 1/2/3/4=灰/灰A/RGB/RGBA，对齐 stb_image_write）--------
# 逐行自适应选最优过滤器（试 5 种取绝对值熵最小），流式 codec_zenc 边 filter 边压缩、边写 IDAT。
#   flip_mode：0=按存储朝向（自顶向下）/ 非 0=垂直翻转后再写。level：codec zlib 压缩级（<0 取默认 2）。
#   仅支持 depth==8（stb 写侧亦然）；depth==16 返回 <0。
@fnc png_write: i4, c: com&, info: img&, flip_mode: i4, level: i4
    if info->depth != 8
        return -1
    var x: i4 = info->width
    var y: i4 = info->height
    var n: i4 = info->channels
    if x <= 0 || y <= 0 || n < 1 || n > 4
        return -1
    var flip: i4 = flip_mode != 0 ? 1 : 0
    var stride: u8 = (x: u8) * (n: u8)
    var lv: i4 = level
    if lv < 0
        lv = 2
    # 签名
    var sig[8]: u1
    sig[0] = 0x89
    sig[1] = 0x50
    sig[2] = 0x4E
    sig[3] = 0x47
    sig[4] = 0x0D
    sig[5] = 0x0A
    sig[6] = 0x1A
    sig[7] = 0x0A
    if png_wr(c, (&sig[0]: &), 8) < 0
        return -1
    # IHDR
    var ihdr[13]: u1
    ihdr[0] = ((x >> 24) & 255: u1)
    ihdr[1] = ((x >> 16) & 255: u1)
    ihdr[2] = ((x >> 8) & 255: u1)
    ihdr[3] = (x & 255: u1)
    ihdr[4] = ((y >> 24) & 255: u1)
    ihdr[5] = ((y >> 16) & 255: u1)
    ihdr[6] = ((y >> 8) & 255: u1)
    ihdr[7] = (y & 255: u1)
    ihdr[8] = 8
    var ct: i4 = 6
    if n == 1
        ct = 0
    else if n == 2
        ct = 4
    else if n == 3
        ct = 2
    ihdr[9] = (ct: u1)
    ihdr[10] = 0
    ihdr[11] = 0
    ihdr[12] = 0
    var ihtype[4]: u1
    ihtype[0] = 0x49
    ihtype[1] = 0x48
    ihtype[2] = 0x44
    ihtype[3] = 0x52
    if png_wchunk(c, (&ihtype[0]: u1&), (&ihdr[0]: u1&), 13) < 0
        return -1
    # 流式编码器 + 行缓冲 + 压缩暂存
    var pix: u1& = info->pixels
    var line: u1& = (chunk(stride): u1&)
    if line == nil
        return -1
    var frow: u1& = (chunk(stride + 1): u1&)
    if frow == nil
        recycle((line: &))
        return -1
    var csize: u8 = 32768
    var cstage: u1& = (chunk(csize): u1&)
    if cstage == nil
        recycle((line: &))
        recycle((frow: &))
        return -1
    var zsz: u8 = codec_zenc_size()
    var zst: & = chunk(zsz)
    if zst == nil
        recycle((line: &))
        recycle((frow: &))
        recycle((cstage: &))
        return -1
    codec_zenc_init(zst, 1, lv)
    var idtype[4]: u1
    idtype[0] = 0x49
    idtype[1] = 0x44
    idtype[2] = 0x41
    idtype[3] = 0x54
    var cfill: u8 = 0
    var werr: i4 = 0
    var j: i4 = 0
    while j < y && werr == 0
        var best: i4 = 0
        png_enc_line(pix, stride, x, y, j, n, 0, line, flip)
        var bestval: i8 = png_line_est(line, stride)
        var f: i4 = 1
        while f < 5
            png_enc_line(pix, stride, x, y, j, n, f, line, flip)
            var est: i8 = png_line_est(line, stride)
            if est < bestval
                bestval = est
                best = f
            f = f + 1
        png_enc_line(pix, stride, x, y, j, n, best, line, flip)
        frow[0] = (best: u1)
        png_bcopy((&frow[1]: u1&), line, stride)
        if png_zdrain(c, zst, frow, stride + 1, cstage, csize, &cfill, (&idtype[0]: u1&), 0) < 0
            werr = 1
        j = j + 1
    # 收尾：flush 末块 + zlib 尾，并写出 cstage 剩余不足一批的压缩字节
    if werr == 0
        if png_zdrain(c, zst, (nil: u1&), 0, cstage, csize, &cfill, (&idtype[0]: u1&), 1) < 0
            werr = 1
    if werr == 0 && cfill > 0
        if png_wchunk(c, (&idtype[0]: u1&), cstage, cfill) < 0
            werr = 1
    codec_zenc_free(zst)
    recycle((zst: &))
    recycle((cstage: &))
    recycle((frow: &))
    recycle((line: &))
    if werr != 0
        return -1
    # IEND
    var ietype[4]: u1
    ietype[0] = 0x49
    ietype[1] = 0x45
    ietype[2] = 0x4E
    ietype[3] = 0x44
    if png_wchunk(c, (&ietype[0]: u1&), nil, 0) < 0
        return -1
    return 0

# ================================ JPEG 公开接口 ================================

# -------- jpg_shape：仅读头（SOF），填 width/height/channels/depth（不解像素，不分配）--------
# channels 反映按文件原生解码（req_comp=0）会得到的通道数：img_n>=3 → 3，否则 1。
@fnc jpg_shape: i4, c: com&, info: img&
    var dec: jpg_dec
    if jpg_dec_setup(&dec) < 0
        return -1
    var br: jpg_br
    if jpg_br_init(&br, c) < 0
        jpg_dec_cleanup(&dec)
        return -1
    # 只解析到首个 SOF 即可拿到几何；复用主循环但在拿到 img_n 后即返回。
    var rc: i4 = jpg_scan_header(&dec, &br)
    var ok: i4 = 0
    if rc == 0
        info->width    = dec.img_w
        info->height   = dec.img_h
        info->channels = (dec.img_n >= 3 ? 3 : 1)
        info->depth    = 8
        info->pixels   = nil
        ok = 1
    jpg_br_free(&br)
    jpg_dec_cleanup(&dec)
    if ok == 0
        return -1
    return 0

# -------- jpg_read：全功能解码，分配 info.pixels，回填元信息 --------
# 覆盖 baseline(SOF0/1) + progressive(SOF2)、灰度/YCbCr/RGB/CMYK/YCCK、任意 H/V 采样、重启间隔；
#   输出统一为上→下、通道 R,G,B[,A] 或灰度。成功返回 0，失败 <0。用后 recycle(info.pixels)。
#   req_comp（对齐 stb desired_channels）：0=按文件（>=3 通道→3，否则 1）/ 1=灰 / 2=灰A / 3=RGB / 4=RGBA。
#   flip_mode：0=按存储朝向（自顶向下）/ 非 0=垂直翻转输出。
@fnc jpg_read: i4, c: com&, info: img&, req_comp: i4, flip_mode: i4
    var dec: jpg_dec
    if jpg_dec_setup(&dec) < 0
        return -1
    var br: jpg_br
    if jpg_br_init(&br, c) < 0
        jpg_dec_cleanup(&dec)
        return -1
    var rc: i4 = jpg_decode(&dec, &br)
    jpg_br_free(&br)
    if rc < 0
        jpg_dec_cleanup(&dec)
        return -1
    var flip: i4 = flip_mode != 0 ? 1 : 0
    var pixels: & = nil
    var outn: i4 = 0
    if jpg_assemble(&dec, req_comp, flip, &pixels, &outn) < 0
        jpg_dec_cleanup(&dec)
        return -1
    info->width    = dec.img_w
    info->height   = dec.img_h
    info->channels = outn
    info->depth    = 8
    info->pixels   = (pixels: u1&)
    jpg_dec_cleanup(&dec)
    return 0

# -------- jpg_write：baseline 编码写出（quality 可调；quality<=90 时 4:2:0 色度下采样）--------
# channels 1/2=灰（编为零色度 YCbCr）/ 3=RGB / 4=RGBA（忽略 alpha）；始终输出 3 分量 JPEG。
#   quality：1..100（<=0 取 90）。flip_mode：0=按存储朝向 / 非 0=垂直翻转后再写。仅支持 depth==8。
@fnc jpg_write: i4, c: com&, info: img&, quality: i4, flip_mode: i4
    if info->depth != 8
        return -1
    var flip: i4 = flip_mode != 0 ? 1 : 0
    return jpg_encode(c, info, quality, flip)
