# 流式 inflate（codec_zdec）单元测试：与整块 codec_inflate/zlib_decode 对拍。
#   覆盖 zlib(wrap=1) 动态块分块喂 + raw deflate(wrap=0) + 输出背压（小 out 反复抽取）。
# 运行：scc tests/cases/zstream_test.sc --test

inc mem.sc
inc codec.sc

# 造有大量重复子串的数据（每 200 字节循环一份伪随机基块）→ 触发 LZ77 匹配 + 动态 Huffman。
fnc fill_src: src: u1&, n: u4
    var base[200]: u1
    var st: u4 = 0x1234
    var i: u4 = 0
    while i < 200
        st = (st * 1103515245 + 12345) & 0x7FFFFFFF
        base[i] = ((st >> 16): u1)
        i = i + 1
    i = 0
    while i < n
        src[i] = base[i % 200]
        i = i + 1
    return

tst "流式 inflate vs 整块（zlib 动态块，分块喂 7 字节）"
    var N: u4 = 4000
    var src: u1& = (chunk((N: u8)): u1&)
    fill_src(src, N)
    var cb: u8 = codec_deflate_bound((N: u8)) + 16
    var comp: u1& = (chunk(cb): u1&)
    var clen: i8 = codec_zlib_encode((src: &), (N: u8), comp, cb, 2)
    assert clen > 0, "zlib_encode ok"

    var whole: u1& = (chunk((N: u8)): u1&)
    assert codec_zlib_decode((comp: &), (clen: u8), whole, (N: u8)) == (N: i8), "整块 oracle"

    var sz: u8 = codec_zdec_size()
    var s: & = chunk(sz)
    assert codec_zdec_init(s, 1) == 0, "init"
    var out: u1& = (chunk((N: u8)): u1&)
    var op: u8 = 0
    var i: u8 = 0
    while i < (clen: u8)
        var cl: u8 = 7
        if i + cl > (clen: u8)
            cl = (clen: u8) - i
        var consumed: u8 = 0
        var got: i8 = codec_zdec_feed(s, (&comp[i]: &), cl, &consumed, (&out[op]: u1&), (N: u8) - op)
        assert got >= 0, "feed 无错"
        assert consumed == cl, "consumed==喂入"
        op = op + (got: u8)
        i = i + cl
    assert codec_zdec_ended(s) == 1, "流结束"
    assert op == (N: u8), "产出长度"
    var ok: i4 = 1
    var k: u8 = 0
    while k < (N: u8)
        if out[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "逐字节 == 原文"
    codec_zdec_free(s)
    recycle((s: &))
    recycle((src: &))
    recycle((comp: &))
    recycle((whole: &))
    recycle((out: &))

tst "流式 inflate（raw DEFLATE wrap=0，一次喂 + 小 out 背压抽取）"
    var N: u4 = 3000
    var src: u1& = (chunk((N: u8)): u1&)
    fill_src(src, N)
    var cb: u8 = codec_deflate_bound((N: u8)) + 16
    var comp: u1& = (chunk(cb): u1&)
    var clen: i8 = codec_deflate((src: &), (N: u8), comp, cb, 2)
    assert clen > 0, "deflate ok"

    var sz: u8 = codec_zdec_size()
    var s: & = chunk(sz)
    assert codec_zdec_init(s, 0) == 0, "init raw"
    var out: u1& = (chunk((N: u8)): u1&)
    var op: u8 = 0
    # 先把整段压缩流喂入（consumed 恒为全部），out 每次只给 32 字节，反复抽取
    var consumed: u8 = 0
    var fed: i4 = 0
    while codec_zdec_ended(s) == 0
        var inlen: u8 = 0
        if fed == 0
            inlen = (clen: u8)
            fed = 1
        var capnow: u8 = 32
        if op + capnow > (N: u8)
            capnow = (N: u8) - op
        var inptr: & = (comp: &)
        var got: i8 = codec_zdec_feed(s, inptr, inlen, &consumed, (&out[op]: u1&), capnow)
        assert got >= 0, "feed 无错"
        op = op + (got: u8)
        if got == 0 && inlen == 0 && capnow == 0 && codec_zdec_ended(s) == 0
            assert 0 == 1, "死锁保护"
    assert op == (N: u8), "产出长度"
    var ok: i4 = 1
    var k: u8 = 0
    while k < (N: u8)
        if out[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "逐字节 == 原文"
    codec_zdec_free(s)
    recycle((s: &))
    recycle((src: &))
    recycle((comp: &))
    recycle((out: &))

tst "流式 inflate（固定 Huffman level=1，分块喂 3 字节）"
    var N: u4 = 1500
    var src: u1& = (chunk((N: u8)): u1&)
    fill_src(src, N)
    var cb: u8 = codec_deflate_bound((N: u8)) + 16
    var comp: u1& = (chunk(cb): u1&)
    var clen: i8 = codec_zlib_encode((src: &), (N: u8), comp, cb, 1)
    assert clen > 0, "zlib fixed ok"

    var sz: u8 = codec_zdec_size()
    var s: & = chunk(sz)
    assert codec_zdec_init(s, 1) == 0, "init"
    var out: u1& = (chunk((N: u8)): u1&)
    var op: u8 = 0
    var i: u8 = 0
    while i < (clen: u8)
        var cl: u8 = 3
        if i + cl > (clen: u8)
            cl = (clen: u8) - i
        var consumed: u8 = 0
        var got: i8 = codec_zdec_feed(s, (&comp[i]: &), cl, &consumed, (&out[op]: u1&), (N: u8) - op)
        assert got >= 0, "feed 无错"
        op = op + (got: u8)
        i = i + cl
    assert codec_zdec_ended(s) == 1, "流结束"
    assert op == (N: u8), "产出长度"
    var ok: i4 = 1
    var k: u8 = 0
    while k < (N: u8)
        if out[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "逐字节 == 原文"
    codec_zdec_free(s)
    recycle((s: &))
    recycle((src: &))
    recycle((comp: &))
    recycle((out: &))

tst "流式 deflate（zlib）分块喂 -> 整块 zlib_decode 还原 == 原文"
    var N: u4 = 5000
    var src: u1& = (chunk((N: u8)): u1&)
    fill_src(src, N)

    var sz: u8 = codec_zenc_size()
    var s: & = chunk(sz)
    assert codec_zenc_init(s, 1, 2) == 0, "zenc init"
    var ocap: u8 = (N: u8) + 4096
    var comp: u1& = (chunk(ocap): u1&)
    var clen: u8 = 0
    var i: u8 = 0
    while i < (N: u8)
        var cl: u8 = 137
        if i + cl > (N: u8)
            cl = (N: u8) - i
        var consumed: u8 = 0
        var got: i8 = codec_zenc_feed(s, (&src[i]: &), cl, &consumed, (&comp[clen]: u1&), ocap - clen)
        assert got >= 0, "feed 无错"
        assert consumed == cl, "consumed==喂入"
        clen = clen + (got: u8)
        i = i + cl
    while codec_zenc_ended(s) == 0
        var gf: i8 = codec_zenc_finish(s, (&comp[clen]: u1&), ocap - clen)
        assert gf >= 0, "finish 无错"
        clen = clen + (gf: u8)
    assert clen > 0, "有压缩产出"

    var dec: u1& = (chunk((N: u8)): u1&)
    assert codec_zlib_decode((comp: &), clen, dec, (N: u8)) == (N: i8), "整块解码长度"
    var ok: i4 = 1
    var k: u8 = 0
    while k < (N: u8)
        if dec[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "逐字节 == 原文"
    codec_zenc_free(s)
    recycle((s: &))
    recycle((src: &))
    recycle((comp: &))
    recycle((dec: &))

tst "流式 deflate(raw) 小 out 背压 -> 流式 inflate(raw) 往返 == 原文"
    var N: u4 = 9000
    var src: u1& = (chunk((N: u8)): u1&)
    fill_src(src, N)

    var esz: u8 = codec_zenc_size()
    var es: & = chunk(esz)
    assert codec_zenc_init(es, 0, 2) == 0, "zenc init raw"
    var ocap: u8 = (N: u8) + 8192
    var comp: u1& = (chunk(ocap): u1&)
    var clen: u8 = 0
    var i: u8 = 0
    while i < (N: u8)
        var cl: u8 = 251
        if i + cl > (N: u8)
            cl = (N: u8) - i
        var consumed: u8 = 0
        var got: i8 = codec_zenc_feed(es, (&src[i]: &), cl, &consumed, (&comp[clen]: u1&), 40)
        assert got >= 0, "feed 无错"
        clen = clen + (got: u8)
        var drained: i8 = 1
        while drained > 0
            var c2: u8 = 0
            drained = codec_zenc_feed(es, (nil: &), 0, &c2, (&comp[clen]: u1&), 40)
            assert drained >= 0, "drain 无错"
            clen = clen + (drained: u8)
        i = i + cl
    while codec_zenc_ended(es) == 0
        var gf: i8 = codec_zenc_finish(es, (&comp[clen]: u1&), 40)
        assert gf >= 0, "finish 无错"
        clen = clen + (gf: u8)
    codec_zenc_free(es)
    recycle((es: &))
    assert clen > 0, "有压缩产出"

    var dsz: u8 = codec_zdec_size()
    var ds: & = chunk(dsz)
    assert codec_zdec_init(ds, 0) == 0, "zdec init raw"
    var dec: u1& = (chunk((N: u8)): u1&)
    var op: u8 = 0
    var j: u8 = 0
    while j < clen
        var cl: u8 = 40
        if j + cl > clen
            cl = clen - j
        var consumed: u8 = 0
        var got: i8 = codec_zdec_feed(ds, (&comp[j]: &), cl, &consumed, (&dec[op]: u1&), (N: u8) - op)
        assert got >= 0, "zdec feed 无错"
        op = op + (got: u8)
        j = j + cl
    assert codec_zdec_ended(ds) == 1, "解码流结束"
    assert op == (N: u8), "还原长度"
    var ok: i4 = 1
    var k: u8 = 0
    while k < (N: u8)
        if dec[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "往返逐字节 == 原文"
    codec_zdec_free(ds)
    recycle((ds: &))
    recycle((src: &))
    recycle((comp: &))
    recycle((dec: &))

tst "流式 deflate（gzip）分块喂 -> 整块 gzip_decode 还原 == 原文"
    var N: u4 = 4096
    var src: u1& = (chunk((N: u8)): u1&)
    fill_src(src, N)

    var sz: u8 = codec_zenc_size()
    var s: & = chunk(sz)
    assert codec_zenc_init(s, 2, 2) == 0, "zenc init gzip"
    var ocap: u8 = (N: u8) + 4096
    var comp: u1& = (chunk(ocap): u1&)
    var clen: u8 = 0
    var i: u8 = 0
    while i < (N: u8)
        var cl: u8 = 100
        if i + cl > (N: u8)
            cl = (N: u8) - i
        var consumed: u8 = 0
        var got: i8 = codec_zenc_feed(s, (&src[i]: &), cl, &consumed, (&comp[clen]: u1&), ocap - clen)
        assert got >= 0, "feed 无错"
        clen = clen + (got: u8)
        i = i + cl
    while codec_zenc_ended(s) == 0
        var gf: i8 = codec_zenc_finish(s, (&comp[clen]: u1&), ocap - clen)
        assert gf >= 0, "finish 无错"
        clen = clen + (gf: u8)
    assert clen > 0, "有压缩产出"

    var dec: u1& = (chunk((N: u8)): u1&)
    assert codec_gzip_decode((comp: &), clen, dec, (N: u8)) == (N: i8), "gzip 解码长度(校验 CRC/ISIZE)"
    var ok: i4 = 1
    var k: u8 = 0
    while k < (N: u8)
        if dec[k] != src[k]
            ok = 0
        k = k + 1
    assert ok == 1, "逐字节 == 原文"
    codec_zenc_free(s)
    recycle((s: &))
    recycle((src: &))
    recycle((comp: &))
    recycle((dec: &))
