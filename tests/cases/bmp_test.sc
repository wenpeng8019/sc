# BMP 单元测试：templates/.scenv/modules/img/img.sc 的 bmp_read/bmp_write/bmp_shape。
#   经 com（内存 stream 设备）做编解码 round-trip + 手工构造位流覆盖读侧格式，
#   覆盖 24 位(RGB)/32 位(RGBA)、行 4 字节对齐、朝向、调色板 8 位、16 位 RGB555、
#   以及 alpha_mode/flip_mode/invert_mode 参数语义（对齐参考 iAlphaMode/iFlipMode/iInvertMode）。
# 运行：scc tests/cases/bmp_test.sc --test
#
# 被测：templates/.scenv/modules/img/img.sc（inc io.sc + mem.sc）。

inc io.sc
inc mem.sc
inc ../../templates/.scenv/modules/img/img.sc

# -------- 手工构造位流用的小端写入辅助 --------
fnc bmp_le32: buf: u1&, o: u4, v: u4
    buf[o]     = (v & 0xFF: u1)
    buf[o + 1] = ((v >> 8) & 0xFF: u1)
    buf[o + 2] = ((v >> 16) & 0xFF: u1)
    buf[o + 3] = ((v >> 24) & 0xFF: u1)
    return

fnc bmp_le16: buf: u1&, o: u4, v: u4
    buf[o]     = (v & 0xFF: u1)
    buf[o + 1] = ((v >> 8) & 0xFF: u1)
    return

tst "BMP 24 位 round-trip（5x4 RGB，含行对齐 + 朝向 + bmp_shape）"
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
    assert bmp_write(wc, &info, 0) == 0, "bmp_write 24 位 ok"
    wc->close()

    # bmp_shape：只读元信息
    var sc0: com& = stream((&buf[0]: &), 512, 1, 0)
    var shp: img
    assert bmp_shape(sc0, &shp) == 0, "bmp_shape ok"
    assert shp.width == 5, "shape width"
    assert shp.height == 4, "shape height"
    assert shp.channels == 3, "shape channels"
    sc0->close()

    # bmp_read：解码逐像素比对
    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 0, 1) == 0, "bmp_read 24 位 ok"
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

tst "BMP 32 位 round-trip（3x2 RGBA，BITFIELDS + alpha 通道）"
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
    assert bmp_write(wc, &info, 0) == 0, "bmp_write 32 位 ok"
    wc->close()

    var sc0: com& = stream((&buf[0]: &), 512, 1, 0)
    var shp: img
    assert bmp_shape(sc0, &shp) == 0, "bmp_shape 32 位 ok"
    assert shp.width == 3 && shp.height == 2 && shp.channels == 4, "shape 32 位（含 alpha）"
    sc0->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 0, 1) == 0, "bmp_read 32 位 ok"
    assert out.width == 3 && out.height == 2 && out.channels == 4, "read dims 32 位"
    var ok: i4 = 1
    i = 0
    while i < 24
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "32 位 round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "BMP alpha_mode：24 位强制 RGBA（alpha 补 255）"
    var src[12]: u1                              # 2x2 RGB = 12
    var i: u4 = 0
    while i < 12
        src[i] = ((i * 11 + 5): u1)
        i = i + 1
    var info: img
    info.width = 2
    info.height = 2
    info.channels = 3
    info.pixels = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert bmp_write(wc, &info, 0) == 0, "写 24 位 ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 1, 1) == 0, "bmp_read 强制 RGBA ok"       # alpha_mode>0
    assert out.channels == 4, "强制 4 通道"
    var ok: i4 = 1
    var p: u4 = 0
    while p < 4                                   # 4 像素
        if out.pixels[p * 4 + 0] != src[p * 3 + 0]
            ok = 0
        if out.pixels[p * 4 + 1] != src[p * 3 + 1]
            ok = 0
        if out.pixels[p * 4 + 2] != src[p * 3 + 2]
            ok = 0
        if out.pixels[p * 4 + 3] != 255
            ok = 0
        p = p + 1
    assert ok == 1, "强制 RGBA：RGB 保真 + alpha=255"
    recycle((out.pixels: &))
    rc->close()

tst "BMP alpha_mode：32 位强制 RGB（丢弃 alpha）"
    var src[16]: u1                              # 2x2 RGBA = 16
    var i: u4 = 0
    while i < 16
        src[i] = ((i * 13 + 7): u1)
        i = i + 1
    var info: img
    info.width = 2
    info.height = 2
    info.channels = 4
    info.pixels = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert bmp_write(wc, &info, 0) == 0, "写 32 位 ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert bmp_read(rc, &out, -1, 1) == 0, "bmp_read 强制 RGB ok"       # alpha_mode<0
    assert out.channels == 3, "强制 3 通道"
    var ok: i4 = 1
    var p: u4 = 0
    while p < 4
        if out.pixels[p * 3 + 0] != src[p * 4 + 0]
            ok = 0
        if out.pixels[p * 3 + 1] != src[p * 4 + 1]
            ok = 0
        if out.pixels[p * 3 + 2] != src[p * 4 + 2]
            ok = 0
        p = p + 1
    assert ok == 1, "强制 RGB：RGB 保真、alpha 被丢弃"
    recycle((out.pixels: &))
    rc->close()

tst "BMP flip_mode：flip_mode<0 归一自底向上（相对自顶存储翻转）"
    var src[18]: u1                              # 3x2 RGB = 18（每行 9 字节）
    var i: u4 = 0
    while i < 18
        src[i] = ((i * 5 + 1): u1)
        i = i + 1
    var info: img
    info.width = 3
    info.height = 2
    info.channels = 3
    info.pixels = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert bmp_write(wc, &info, 0) == 0, "写 24 位（自顶存储）ok"      # invert_mode=0：自顶向下
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 0, -1) == 0, "bmp_read flip_mode<0 ok"    # 归一自底向上→垂直翻转
    var ok: i4 = 1
    var y: u4 = 0
    while y < 2
        var x: u4 = 0
        while x < 9
            if out.pixels[y * 9 + x] != src[(1 - y) * 9 + x]
                ok = 0
            x = x + 1
        y = y + 1
    assert ok == 1, "flip_mode<0：输出相对源垂直翻转"
    recycle((out.pixels: &))
    rc->close()

tst "BMP invert_mode=2：自底向上存储 + flip_mode=1 归一 round-trip 一致"
    var src[36]: u1                              # 4x3 RGB = 36（每行 12 字节）
    var i: u4 = 0
    while i < 36
        src[i] = ((i * 3 + 2): u1)
        i = i + 1
    var info: img
    info.width = 4
    info.height = 3
    info.channels = 3
    info.pixels = (&src[0]: u1&)

    var buf[512]: u1
    var wc: com& = stream((&buf[0]: &), 512, 0, 1)
    assert bmp_write(wc, &info, 2) == 0, "写 24 位（invert_mode=2 自底存储）ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 512, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 0, 1) == 0, "bmp_read 归一自顶 ok"
    var ok: i4 = 1
    i = 0
    while i < 36
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "invert_mode=2 存储 + flip_mode=1 归一：与源一致"
    recycle((out.pixels: &))
    rc->close()

tst "BMP 读：手工构造 8 位调色板（2x2，自底向上）"
    var buf[128]: u1
    var k: u4 = 0
    while k < 128
        buf[k] = 0
        k = k + 1
    buf[0] = 0x42
    buf[1] = 0x4D
    bmp_le32((&buf[0]: u1&), 2, 78)              # file_size
    bmp_le32((&buf[0]: u1&), 10, 70)             # pix_off = 14+40+16
    bmp_le32((&buf[0]: u1&), 14, 40)             # header_size
    bmp_le32((&buf[0]: u1&), 18, 2)              # width
    bmp_le32((&buf[0]: u1&), 22, 2)              # height（正=自底向上）
    bmp_le16((&buf[0]: u1&), 26, 1)              # planes
    bmp_le16((&buf[0]: u1&), 28, 8)              # bpp
    bmp_le32((&buf[0]: u1&), 30, 0)              # compress = BI_RGB
    bmp_le32((&buf[0]: u1&), 46, 4)              # color_num
    # 调色板 4 项 × BGRA（@54）
    buf[54] = 10
    buf[55] = 20
    buf[56] = 30
    buf[57] = 0                                   # idx0 → RGB(30,20,10)
    buf[58] = 40
    buf[59] = 50
    buf[60] = 60
    buf[61] = 0                                   # idx1 → RGB(60,50,40)
    buf[62] = 70
    buf[63] = 80
    buf[64] = 90
    buf[65] = 0                                   # idx2 → RGB(90,80,70)
    buf[66] = 100
    buf[67] = 110
    buf[68] = 120
    buf[69] = 0                                   # idx3 → RGB(120,110,100)
    # 像素（@70）自底向上，行 4 字节对齐（每行 2 索引 + 2 补位）
    buf[70] = 2
    buf[71] = 3                                   # 文件行0=底=图像行1
    buf[74] = 0
    buf[75] = 1                                   # 文件行1=顶=图像行0

    var rc: com& = stream((&buf[0]: &), 128, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 0, 1) == 0, "读 8 位调色板 ok"
    assert out.width == 2 && out.height == 2 && out.channels == 3, "调色板 dims"
    var exp[12]: u1
    exp[0]  = 30
    exp[1]  = 20
    exp[2]  = 10
    exp[3]  = 60
    exp[4]  = 50
    exp[5]  = 40
    exp[6]  = 90
    exp[7]  = 80
    exp[8]  = 70
    exp[9]  = 120
    exp[10] = 110
    exp[11] = 100
    var ok: i4 = 1
    var j: u4 = 0
    while j < 12
        if out.pixels[j] != exp[j]
            ok = 0
        j = j + 1
    assert ok == 1, "8 位调色板解码 + 朝向归一正确"
    recycle((out.pixels: &))
    rc->close()

tst "BMP 读：手工构造 16 位 RGB555（2x1）"
    var buf[128]: u1
    var k: u4 = 0
    while k < 128
        buf[k] = 0
        k = k + 1
    buf[0] = 0x42
    buf[1] = 0x4D
    bmp_le32((&buf[0]: u1&), 2, 58)              # file_size = 54 + 4
    bmp_le32((&buf[0]: u1&), 10, 54)             # pix_off = 14+40
    bmp_le32((&buf[0]: u1&), 14, 40)             # header_size
    bmp_le32((&buf[0]: u1&), 18, 2)              # width
    bmp_le32((&buf[0]: u1&), 22, 1)              # height
    bmp_le16((&buf[0]: u1&), 26, 1)              # planes
    bmp_le16((&buf[0]: u1&), 28, 16)             # bpp
    bmp_le32((&buf[0]: u1&), 30, 0)              # compress=BI_RGB → 默认 RGB555
    bmp_le16((&buf[0]: u1&), 54, 0x7C00)         # pixel0 = R=31
    bmp_le16((&buf[0]: u1&), 56, 0x001F)         # pixel1 = B=31

    var rc: com& = stream((&buf[0]: &), 128, 1, 0)
    var out: img
    assert bmp_read(rc, &out, 0, 1) == 0, "读 16 位 RGB555 ok"
    assert out.width == 2 && out.height == 1 && out.channels == 3, "16 位 dims"
    assert out.pixels[0] == 255 && out.pixels[1] == 0 && out.pixels[2] == 0, "px0 = 红"
    assert out.pixels[3] == 0 && out.pixels[4] == 0 && out.pixels[5] == 255, "px1 = 蓝"
    recycle((out.pixels: &))
    rc->close()
