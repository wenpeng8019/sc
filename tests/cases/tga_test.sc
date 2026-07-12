# TGA 单元测试：templates/.scenv/modules/img/img.sc 的 tga_read/tga_write/tga_shape。
#   经 com（内存 stream 设备）做编解码 round-trip，覆盖 24 位(RGB)/32 位(RGBA)、
#   类型 10（RLE，复用 codec 流式行程码）、自顶向下朝向、tga_shape 只读元信息。
#   含「行内含长重复」用例，专门压实 RLE 行程包路径。
# 运行：scc tests/cases/tga_test.sc --test
#
# 被测：templates/.scenv/modules/img/img.sc（inc io.sc + mem.sc + codec.sc）。

inc io.sc
inc mem.sc
inc ../../templates/.scenv/modules/img/img.sc

# 构造 TGA 18 字节头（清零后填公共字段）。颜色表字段（b[1]/b[3..7]）由调用方另设。
fnc tga_put_hdr: i4, b: u1&, itype: u4, w: u4, hgt: u4, bpp: u4, desc: u4
    var i: u4 = 0
    while i < 18
        b[i] = 0
        i = i + 1
    b[2]  = (itype: u1)
    b[12] = ((w & 0xFF): u1)
    b[13] = (((w >> 8) & 0xFF): u1)
    b[14] = ((hgt & 0xFF): u1)
    b[15] = (((hgt >> 8) & 0xFF): u1)
    b[16] = (bpp: u1)
    b[17] = (desc: u1)
    return 0

tst "TGA 24 位 round-trip（5x4 RGB，RLE + tga_shape）"
    var src[60]: u1                              # 5x4 RGB = 60
    var i: u4 = 0
    while i < 60
        src[i] = ((i * 9 + 3): u1)
        i = i + 1
    var info: img
    info.width    = 5
    info.height   = 4
    info.channels = 3
    info.pixels   = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 1) == 0, "tga_write 24 位 ok"
    wc->close()

    # tga_shape：只读元信息
    var sc0: com& = stream((&buf[0]: &), 512, 1, 0)
    var shp: img
    assert tga_shape(sc0, &shp) == 0, "tga_shape ok"
    assert shp.width == 5, "shape width"
    assert shp.height == 4, "shape height"
    assert shp.channels == 3, "shape channels"
    sc0->close()

    # tga_read：解码逐像素比对
    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "tga_read 24 位 ok"
    assert out.width == 5 && out.height == 4 && out.channels == 3, "read dims"
    var ok: i4 = 1
    i = 0
    while i < 60
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "24 位 round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 32 位 round-trip（3x2 RGBA）"
    var src[24]: u1                              # 3x2 RGBA = 24
    var i: u4 = 0
    while i < 24
        src[i] = ((i * 7 + 13): u1)
        i = i + 1
    var info: img
    info.width    = 3
    info.height   = 2
    info.channels = 4
    info.pixels   = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 1) == 0, "tga_write 32 位 ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "tga_read 32 位 ok"
    assert out.width == 3 && out.height == 2 && out.channels == 4, "read dims"
    var ok: i4 = 1
    i = 0
    while i < 24
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "32 位 round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "TGA RLE 行程包路径（8x3 RGB，行内含长重复）"
    # 每行前 6 像素同色（触发行程包）、后 2 像素渐变（触发原始包），逐行换色。
    var W: u4 = 8
    var H: u4 = 3
    var src[72]: u1                              # 8x3 RGB = 72
    var y: u4 = 0
    while y < H
        var x: u4 = 0
        while x < W
            var b: u4 = (y * W + x) * 3
            if x < 6
                src[b]     = (y * 40 + 5: u1)     # 同色行程
                src[b + 1] = (y * 40 + 5: u1)
                src[b + 2] = (y * 40 + 5: u1)
            else
                src[b]     = (x * 17: u1)         # 渐变字面
                src[b + 1] = (x * 11: u1)
                src[b + 2] = (x * 7: u1)
            x = x + 1
        y = y + 1
    var info: img
    info.width    = (W: i4)
    info.height   = (H: i4)
    info.channels = 3
    info.pixels   = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 1) == 0, "tga_write RLE ok"
    var wpos: i8 = wc->seek(0, 1)                 # 已写字节数
    wc->close()
    # RLE 应比未压缩 18+72=90 更小（大量同色行程）。
    assert wpos < 90, "RLE 实际压缩（< 未压缩 90 字节）"

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "tga_read RLE ok"
    assert out.width == 8 && out.height == 3 && out.channels == 3, "read dims"
    var ok: i4 = 1
    var k: u4 = 0
    while k < 72
        if out.pixels[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "RLE round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 灰度 MONO 8 位（type 3，未压缩 2x2）"
    var buf[64]: u1
    tga_put_hdr((&buf[0]: u1&), 3, 2, 2, 8, 0x20)    # 灰度 8 位，自顶向下
    buf[18] = 10
    buf[19] = 20
    buf[20] = 30
    buf[21] = 40
    var rc: com& = stream((&buf[0]: &), 64, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "read mono8 ok"
    assert out.width == 2 && out.height == 2 && out.channels == 3, "mono8 dims/ch"
    assert out.pixels[0] == 10 && out.pixels[1] == 10 && out.pixels[2] == 10, "px0 灰 10"
    assert out.pixels[3] == 20 && out.pixels[5] == 20, "px1 灰 20"
    assert out.pixels[6] == 30 && out.pixels[9] == 40, "px2 灰 30 / px3 灰 40"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 灰度 MONO 16 位（type 3，含 alpha，2x1）"
    var buf[64]: u1
    tga_put_hdr((&buf[0]: u1&), 3, 2, 1, 16, 0x20)   # 16 位灰度：第二字节 alpha
    buf[18] = 100
    buf[19] = 200                                     # px0 灰 100 / alpha 200
    buf[20] = 150
    buf[21] = 250                                     # px1 灰 150 / alpha 250
    var rc: com& = stream((&buf[0]: &), 64, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "read mono16 ok"
    assert out.channels == 4, "mono16 → RGBA"
    assert out.pixels[0] == 100 && out.pixels[1] == 100 && out.pixels[3] == 200, "px0 灰/alpha"
    assert out.pixels[4] == 150 && out.pixels[7] == 250, "px1 灰/alpha"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 索引色 COLORMAP（8 位索引 + 24 位调色板，2x2）"
    var buf[64]: u1
    tga_put_hdr((&buf[0]: u1&), 1, 2, 2, 8, 0x20)    # 索引 8 位
    buf[1] = 1                                        # map_type=1（有调色板）
    buf[5] = 4                                        # map_num=4
    buf[7] = 24                                       # 调色板条目 24 位
    var pb: u4 = 18                                   # 调色板起点
    var e: u4 = 0
    while e < 4                                       # entryN BGR = (3n+1, 3n+2, 3n+3)
        buf[pb + e * 3]     = (e * 3 + 1: u1)
        buf[pb + e * 3 + 1] = (e * 3 + 2: u1)
        buf[pb + e * 3 + 2] = (e * 3 + 3: u1)
        e = e + 1
    var ib: u4 = pb + 12                              # 索引数据起点=30
    buf[ib]     = 0
    buf[ib + 1] = 1
    buf[ib + 2] = 2
    buf[ib + 3] = 3
    var rc: com& = stream((&buf[0]: &), 64, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "read colormap ok"
    assert out.channels == 3, "24 位调色板 → RGB"
    # BGR(3n+1,3n+2,3n+3) → RGB(3n+3,3n+2,3n+1)
    assert out.pixels[0] == 3 && out.pixels[1] == 2 && out.pixels[2] == 1, "px0=entry0"
    assert out.pixels[3] == 6 && out.pixels[5] == 4, "px1=entry1"
    assert out.pixels[6] == 9 && out.pixels[9] == 12, "px2=entry2 / px3=entry3"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 16 位真彩色（5-5-5 + alpha 位，1x2）"
    var buf[64]: u1
    tga_put_hdr((&buf[0]: u1&), 2, 1, 2, 16, 0x20)   # 真彩 16 位
    buf[18] = 0x00
    buf[19] = 0xFC                                    # px0 c16=0xFC00：alpha 位=1, r=31
    buf[20] = 0xE0
    buf[21] = 0x03                                    # px1 c16=0x03E0：alpha 位=0, g=31
    var rc: com& = stream((&buf[0]: &), 64, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "read 16 位真彩 ok"
    assert out.channels == 4, "16 位 → RGBA（含 alpha 位）"
    assert out.pixels[0] == 248 && out.pixels[1] == 0 && out.pixels[2] == 0, "px0 红(248)"
    assert out.pixels[3] == 255, "px0 alpha 位=1 → 255"
    assert out.pixels[4] == 0 && out.pixels[5] == 248 && out.pixels[6] == 0, "px1 绿(248)"
    assert out.pixels[7] == 0, "px1 alpha 位=0 → 0"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 灰度 RLE（type 3|0x08=11，4x1，行程+字面）"
    var buf[64]: u1
    tga_put_hdr((&buf[0]: u1&), 11, 4, 1, 8, 0x20)   # RLE 位与灰度基类型正交
    buf[18] = 0x82                                    # 行程包：重复 (2+1)=3 次
    buf[19] = 50                                      # 值 50
    buf[20] = 0x00                                    # 原始包：1 个字面
    buf[21] = 90                                      # 值 90
    var rc: com& = stream((&buf[0]: &), 64, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "read mono-rle ok"
    assert out.width == 4 && out.channels == 3, "mono-rle dims/ch"
    assert out.pixels[0] == 50 && out.pixels[3] == 50 && out.pixels[6] == 50, "行程 3×灰 50"
    assert out.pixels[9] == 90 && out.pixels[11] == 90, "字面 灰 90"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 空图（type 0，2x2，全 0）"
    var buf[64]: u1
    tga_put_hdr((&buf[0]: u1&), 0, 2, 2, 24, 0x20)   # 空图 bpp24 → 带 alpha（c_format 语义）
    var rc: com& = stream((&buf[0]: &), 64, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "read empty ok"
    assert out.width == 2 && out.height == 2, "empty dims"
    assert out.channels == 4, "空图 bpp24 → 4 通道"
    var z: i4 = 1
    var i: u4 = 0
    while i < 16
        if out.pixels[i] != 0
            z = 0
        i = i + 1
    assert z == 1, "空图全 0"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 未压缩写 tga_write(rle=0)（4x3 RGB round-trip + 类型字节=2）"
    var src[36]: u1                              # 4x3 RGB = 36
    var i: u4 = 0
    while i < 36
        src[i] = ((i * 5 + 7): u1)
        i = i + 1
    var info: img
    info.width    = 4
    info.height   = 3
    info.channels = 3
    info.pixels   = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 0) == 0, "tga_write 未压缩 ok"
    var wpos: i8 = wc->seek(0, 1)
    wc->close()
    assert wpos == 54, "未压缩=18 头 + 36 像素 = 54 字节"
    assert buf[2] == 2, "类型字节=2（未压缩）"

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "tga_read 未压缩 ok"
    assert out.width == 4 && out.height == 3 && out.channels == 3, "read dims"
    var ok: i4 = 1
    i = 0
    while i < 36
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "未压缩 round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "TGA 自底向上存储写 tga_write(invert_mode=2)（3x3 RGB round-trip + desc bit5=0）"
    var src[27]: u1                              # 3x3 RGB = 27
    var i: u4 = 0
    while i < 27
        src[i] = ((i * 3 + 11): u1)
        i = i + 1
    var info: img
    info.width    = 3
    info.height   = 3
    info.channels = 3
    info.pixels   = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 2, 0) == 0, "tga_write 自底向上存储 ok"
    wc->close()
    assert (buf[17] & 0x20) == 0, "desc bit5=0（自底向上存储）"

    # 读回：解码器按 desc 归一化为自顶向下，应与原图一致
    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, 1) == 0, "tga_read 自底向上 ok"
    var ok: i4 = 1
    i = 0
    while i < 27
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "自底向上 round-trip（归一化后）逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "TGA tga_read 强制 RGBA（24 位源 alpha_mode>0 → 补不透明 alpha）"
    # 写一幅 24 位未压缩 2x1 图，再用 tga_read(alpha_mode>0) 强制 4 通道
    var info: img
    var pix[6]: u1
    pix[0] = (10: u1)                            # 像素0 RGB
    pix[1] = (20: u1)
    pix[2] = (30: u1)
    pix[3] = (40: u1)                            # 像素1 RGB
    pix[4] = (50: u1)
    pix[5] = (60: u1)
    info.width    = 2
    info.height   = 1
    info.channels = 3
    info.pixels   = (&pix[0]: u1&)
    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 0) == 0, "写 24 位未压缩 ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 1, 1) == 0, "tga_read alpha_mode>0 ok"
    assert out.channels == 4, "强制 RGBA → 4 通道"
    assert out.pixels[0] == 10 && out.pixels[1] == 20 && out.pixels[2] == 30, "像素0 RGB"
    assert out.pixels[3] == 0xFF, "像素0 alpha 补 0xFF"
    assert out.pixels[7] == 0xFF, "像素1 alpha 补 0xFF"
    recycle((out.pixels: &))
    rc->close()

tst "TGA tga_read 强制 RGB（32 位源 alpha_mode<0 → 丢弃 alpha）"
    var info: img
    var pix[8]: u1
    pix[0] = (11: u1)                            # 像素0 RGBA
    pix[1] = (22: u1)
    pix[2] = (33: u1)
    pix[3] = (99: u1)
    pix[4] = (44: u1)                            # 像素1 RGBA
    pix[5] = (55: u1)
    pix[6] = (66: u1)
    pix[7] = (88: u1)
    info.width    = 2
    info.height   = 1
    info.channels = 4
    info.pixels   = (&pix[0]: u1&)
    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 0) == 0, "写 32 位未压缩 ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, -1, 1) == 0, "tga_read alpha_mode<0 ok"
    assert out.channels == 3, "强制 RGB → 3 通道"
    assert out.pixels[0] == 11 && out.pixels[1] == 22 && out.pixels[2] == 33, "像素0 RGB（丢 alpha）"
    assert out.pixels[3] == 44 && out.pixels[4] == 55 && out.pixels[5] == 66, "像素1 RGB（紧邻，无 alpha 间隔）"
    recycle((out.pixels: &))
    rc->close()

tst "TGA tga_read 输出自底向上（flip_mode<0 → 行序翻转）"
    # 2 行 1 列 RGB：行0=(1,2,3) 行1=(4,5,6)。自顶向下写入，再以 top_down=0 读回应翻转行序。
    var info: img
    var pix[6]: u1
    pix[0] = (1: u1)
    pix[1] = (2: u1)
    pix[2] = (3: u1)
    pix[3] = (4: u1)
    pix[4] = (5: u1)
    pix[5] = (6: u1)
    info.width    = 1
    info.height   = 2
    info.channels = 3
    info.pixels   = (&pix[0]: u1&)
    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert tga_write(wc, &info, 0, 0) == 0, "写 2x1 未压缩 ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert tga_read(rc, &out, 0, -1) == 0, "tga_read flip_mode<0 ok"
    # 输出行序翻转：out 行0 应为原行1=(4,5,6)，out 行1 应为原行0=(1,2,3)
    assert out.pixels[0] == 4 && out.pixels[1] == 5 && out.pixels[2] == 6, "输出行0=原底行"
    assert out.pixels[3] == 1 && out.pixels[4] == 2 && out.pixels[5] == 3, "输出行1=原顶行"
    recycle((out.pixels: &))
    rc->close()
