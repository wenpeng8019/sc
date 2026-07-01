# JPEG 单元测试：templates/utils/img/img.sc 的 jpg_read/jpg_write/jpg_shape。
#   经 com（内存 stream 设备）做编解码 round-trip。JPEG 有损，故用平滑渐变图 + 容差断言；
#   编解码器的标准符合性（与 libjpeg 双向对齐 baseline/progressive/灰度/4:2:0）已另经 PIL 交叉验证。
# 运行：scc tests/cases/jpg_test.sc --test
#
# 被测：templates/utils/img/img.sc（inc io.sc + mem.sc + img_jpg_codec 熵层）。

inc io.sc
inc mem.sc
inc ../../templates/utils/img/img.sc

# 平滑渐变（无 &255 回绕，避免有损量化在硬边处的振铃）填 RGB。
fnc jt_fill_rgb: dst: u1&, w: i4, h: i4
    var y: i4 = 0
    while y < h
        var x: i4 = 0
        while x < w
            var p: i4 = (y * w + x) * 3
            var r: i4 = x * 3
            if r > 255
                r = 255
            var g: i4 = y * 4
            if g > 255
                g = 255
            var b: i4 = 80 + x + y
            if b > 255
                b = 255
            dst[p] = (r: u1)
            dst[p + 1] = (g: u1)
            dst[p + 2] = (b: u1)
            x = x + 1
        y = y + 1
    return

# 逐像素最大绝对差。
fnc jt_maxdiff: i4, a: u1&, b: u1&, n: i4
    var m: i4 = 0
    var i: i4 = 0
    while i < n
        var d: i4 = (a[i]: i4) - (b[i]: i4)
        if d < 0
            d = -d
        if d > m
            m = d
        i = i + 1
    return m

tst "JPEG RGB round-trip（32x24，jpg_shape + 容差）"
    var W: i4 = 32
    var H: i4 = 24
    var src: u1& = (chunk((W * H * 3: u8)): u1&)
    jt_fill_rgb(src, W, H)
    var info: img
    info.width    = W
    info.height   = H
    info.channels = 3
    info.depth    = 8
    info.pixels   = src

    var buf[16384]: u1
    var wc: com& = stream((&buf[0]: &), 16384, 0, 1)
    assert jpg_write(wc, &info, 95, 0) == 0, "jpg_write RGB ok"
    wc->close()

    var sc0: com& = stream((&buf[0]: &), 16384, 1, 0)
    var shp: img
    assert jpg_shape(sc0, &shp) == 0, "jpg_shape ok"
    assert shp.width == W, "shape width"
    assert shp.height == H, "shape height"
    assert shp.channels == 3, "shape channels"
    assert shp.depth == 8, "shape depth"
    sc0->close()

    var rc: com& = stream((&buf[0]: &), 16384, 1, 0)
    var out: img
    assert jpg_read(rc, &out, 3, 0) == 0, "jpg_read RGB ok"
    rc->close()
    assert out.width == W && out.height == H && out.channels == 3, "read dims"
    assert out.depth == 8, "read depth"
    assert jt_maxdiff(out.pixels, src, W * H * 3) <= 24, "RGB 有损容差内"
    recycle((out.pixels: &))
    recycle((src: &))

tst "JPEG 灰度 round-trip（24x16，channels=1）"
    var W: i4 = 24
    var H: i4 = 16
    var src: u1& = (chunk((W * H: u8)): u1&)
    var y: i4 = 0
    while y < H
        var x: i4 = 0
        while x < W
            var v: i4 = x * 4 + y * 3
            if v > 255
                v = 255
            src[y * W + x] = (v: u1)
            x = x + 1
        y = y + 1
    var info: img
    info.width    = W
    info.height   = H
    info.channels = 1
    info.depth    = 8
    info.pixels   = src

    var buf[16384]: u1
    var wc: com& = stream((&buf[0]: &), 16384, 0, 1)
    assert jpg_write(wc, &info, 92, 0) == 0, "jpg_write gray ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 16384, 1, 0)
    var out: img
    assert jpg_read(rc, &out, 1, 0) == 0, "jpg_read gray ok"
    rc->close()
    assert out.width == W && out.height == H && out.channels == 1, "gray dims"
    assert jt_maxdiff(out.pixels, src, W * H) <= 12, "灰度有损容差内"
    recycle((out.pixels: &))
    recycle((src: &))

tst "JPEG flip 垂直翻转 round-trip（16x16）"
    var W: i4 = 16
    var H: i4 = 16
    var src: u1& = (chunk((W * H * 3: u8)): u1&)
    jt_fill_rgb(src, W, H)
    var info: img
    info.width    = W
    info.height   = H
    info.channels = 3
    info.depth    = 8
    info.pixels   = src

    var buf[16384]: u1
    var wc: com& = stream((&buf[0]: &), 16384, 0, 1)
    assert jpg_write(wc, &info, 95, 1) == 0, "jpg_write flip ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 16384, 1, 0)
    var out: img
    assert jpg_read(rc, &out, 3, 1) == 0, "jpg_read flip ok"
    rc->close()
    # 写时翻转 + 读时翻转 = 复原
    assert jt_maxdiff(out.pixels, src, W * H * 3) <= 24, "双翻转复原容差内"
    recycle((out.pixels: &))
    recycle((src: &))

tst "JPEG req_comp=1 从彩色取灰度"
    var W: i4 = 16
    var H: i4 = 16
    var src: u1& = (chunk((W * H * 3: u8)): u1&)
    jt_fill_rgb(src, W, H)
    var info: img
    info.width    = W
    info.height   = H
    info.channels = 3
    info.depth    = 8
    info.pixels   = src

    var buf[16384]: u1
    var wc: com& = stream((&buf[0]: &), 16384, 0, 1)
    assert jpg_write(wc, &info, 95, 0) == 0, "jpg_write ok"
    wc->close()

    var rc: com& = stream((&buf[0]: &), 16384, 1, 0)
    var out: img
    assert jpg_read(rc, &out, 1, 0) == 0, "jpg_read gray-from-rgb ok"
    rc->close()
    assert out.channels == 1, "req_comp=1 → channels=1"
    assert out.width == W && out.height == H, "dims"
    recycle((out.pixels: &))
    recycle((src: &))
