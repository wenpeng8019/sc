# PNG 单元测试：templates/.scenv/modules/img/img.sc 的 png_read/png_write/png_shape。
#   经 com（内存 stream 设备）做编解码 round-trip，覆盖 channels 1/2/3/4（灰/灰A/RGB/RGBA）、
#   req_comp 通道强制转换、flip_mode 朝向、png_shape 元信息。
# 运行：scc tests/cases/png_test.sc --test
#
# 被测：templates/.scenv/modules/img/img.sc（inc io.sc + mem.sc + codec.sc）。

inc io.sc
inc mem.sc
inc ../../templates/.scenv/modules/img/img.sc

tst "PNG RGB 8 位 round-trip（6x5，含 png_shape）"
    var src[90]: u1                              # 6x5 RGB = 90
    var i: u4 = 0
    while i < 90
        src[i] = ((i * 7 + 11): u1)
        i = i + 1
    var info: img
    info.width    = 6
    info.height   = 5
    info.channels = 3
    info.depth    = 8
    info.pixels   = (&src[0]: u1&)

    var buf[2048]: u1
    var wc: com& = stream((&buf[0]: &), 2048, 0, 1)
    assert png_write(wc, &info, 0, 2) == 0, "png_write RGB ok"
    wc->close()

    var sc0: com& = stream((&buf[0]: &), 2048, 1, 0)
    var shp: img
    assert png_shape(sc0, &shp) == 0, "png_shape ok"
    assert shp.width == 6, "shape width"
    assert shp.height == 5, "shape height"
    assert shp.channels == 3, "shape channels"
    assert shp.depth == 8, "shape depth"
    sc0->close()

    var rc: com& = stream((&buf[0]: &), 2048, 1, 0)
    var out: img
    assert png_read(rc, &out, 0, 0) == 0, "png_read RGB ok"
    assert out.width == 6 && out.height == 5 && out.channels == 3, "read dims"
    assert out.depth == 8, "read depth"
    var ok: i4 = 1
    i = 0
    while i < 90
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "RGB round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "PNG RGBA 8 位 round-trip（4x4，alpha 保留）"
    var src[64]: u1                              # 4x4 RGBA = 64
    var i: u4 = 0
    while i < 64
        src[i] = ((i * 13 + 5): u1)
        i = i + 1
    var info: img
    info.width    = 4
    info.height   = 4
    info.channels = 4
    info.depth    = 8
    info.pixels   = (&src[0]: u1&)

    var buf[2048]: u1
    var wc: com& = stream((&buf[0]: &), 2048, 0, 1)
    assert png_write(wc, &info, 0, 2) == 0, "png_write RGBA ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 2048, 1, 0)
    var out: img
    assert png_read(rc, &out, 0, 0) == 0, "png_read RGBA ok"
    assert out.width == 4 && out.height == 4 && out.channels == 4, "read dims"
    var ok: i4 = 1
    i = 0
    while i < 64
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "RGBA round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "PNG 灰度 8 位 round-trip（8x3，channels=1）"
    var src[24]: u1                              # 8x3 gray = 24
    var i: u4 = 0
    while i < 24
        src[i] = ((i * 10 + 1): u1)
        i = i + 1
    var info: img
    info.width    = 8
    info.height   = 3
    info.channels = 1
    info.depth    = 8
    info.pixels   = (&src[0]: u1&)

    var buf[2048]: u1
    var wc: com& = stream((&buf[0]: &), 2048, 0, 1)
    assert png_write(wc, &info, 0, 2) == 0, "png_write gray ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 2048, 1, 0)
    var out: img
    assert png_read(rc, &out, 0, 0) == 0, "png_read gray ok"
    assert out.channels == 1, "read channels=1"
    var ok: i4 = 1
    i = 0
    while i < 24
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "gray round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "PNG 灰度+A 8 位 round-trip（5x2，channels=2）"
    var src[20]: u1                              # 5x2 grayA = 20
    var i: u4 = 0
    while i < 20
        src[i] = ((i * 12 + 7): u1)
        i = i + 1
    var info: img
    info.width    = 5
    info.height   = 2
    info.channels = 2
    info.depth    = 8
    info.pixels   = (&src[0]: u1&)

    var buf[2048]: u1
    var wc: com& = stream((&buf[0]: &), 2048, 0, 1)
    assert png_write(wc, &info, 0, 2) == 0, "png_write grayA ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 2048, 1, 0)
    var out: img
    assert png_read(rc, &out, 0, 0) == 0, "png_read grayA ok"
    assert out.channels == 2, "read channels=2"
    var ok: i4 = 1
    i = 0
    while i < 20
        if out.pixels[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "grayA round-trip 逐像素一致"
    recycle((out.pixels: &))
    rc->close()

tst "PNG req_comp 强制通道（写 RGB，读成 RGBA，alpha=255）"
    var src[36]: u1                              # 4x3 RGB = 36
    var i: u4 = 0
    while i < 36
        src[i] = ((i * 5 + 2): u1)
        i = i + 1
    var info: img
    info.width    = 4
    info.height   = 3
    info.channels = 3
    info.depth    = 8
    info.pixels   = (&src[0]: u1&)

    var buf[2048]: u1
    var wc: com& = stream((&buf[0]: &), 2048, 0, 1)
    assert png_write(wc, &info, 0, 2) == 0, "png_write ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 2048, 1, 0)
    var out: img
    assert png_read(rc, &out, 4, 0) == 0, "png_read req_comp=4 ok"
    assert out.channels == 4, "read channels=4"
    var ok: i4 = 1
    var p: u4 = 0
    while p < 12
        var r: u1 = out.pixels[p * 4 + 0]
        var g: u1 = out.pixels[p * 4 + 1]
        var b: u1 = out.pixels[p * 4 + 2]
        var av: u1 = out.pixels[p * 4 + 3]
        if r != src[p * 3 + 0] || g != src[p * 3 + 1] || b != src[p * 3 + 2]
            ok = 0
        if av != 255
            ok = 0
        p = p + 1
    assert ok == 1, "RGB→RGBA 强制转换正确（alpha 全 255）"
    recycle((out.pixels: &))
    rc->close()

tst "PNG flip_mode 读朝向翻转（写自顶向下，读自底向上）"
    var src[36]: u1                              # 4x3 RGB = 36
    var i: u4 = 0
    while i < 36
        src[i] = ((i * 6 + 4): u1)
        i = i + 1
    var info: img
    info.width    = 4
    info.height   = 3
    info.channels = 3
    info.depth    = 8
    info.pixels   = (&src[0]: u1&)

    var buf[2048]: u1
    var wc: com& = stream((&buf[0]: &), 2048, 0, 1)
    assert png_write(wc, &info, 0, 2) == 0, "png_write ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 2048, 1, 0)
    var out: img
    assert png_read(rc, &out, 0, -1) == 0, "png_read flip_mode=-1 ok"
    # 翻转后第 j 行 == 原第 (2-j) 行
    var ok: i4 = 1
    var rowb: u4 = 4 * 3
    var j: u4 = 0
    while j < 3
        var k: u4 = 0
        while k < rowb
            if out.pixels[j * rowb + k] != src[(2 - j) * rowb + k]
                ok = 0
            k = k + 1
        j = j + 1
    assert ok == 1, "flip_mode<0 行序翻转正确"
    recycle((out.pixels: &))
    rc->close()
