# JPEG 熵扫描流式编解码（codec_jenc / codec_jdec）单元测试。
#   造若干 DU 块（zigzag 序、量化后、du[0]=DC）→ codec_jenc_block 熵编码 → drain 出扫描字节；
#   追加 EOI(0xFF 0xD9) 后 feed 给 codec_jdec → codec_jdec_block 逐块解码 →
#   校验解出的自然序系数 data[dezigzag[k]] == du[k]，DC 预测在编解码两侧对称推进。
# DC 用标准亮度表；AC 用一张 8 码全长度=3 的完备小表（含 0x00/0xF0 及若干 (run,size)）。
# 被测组件已迁出 codec，独立为 img_jpg_codec（JPEG 专属熵层），此处经 add 内联验证其自完备。
# 运行：scc tests/cases/jstream_test.sc --test

inc mem.sc
add ../../templates/utils/img/img_jpg_codec.sc

# zigzag→自然序映射（与 img_jpg_codec.c 的 cj_dezigzag 前 64 项一致）。
fnc fill_zz: zz: i4&
    zz[0]=0
    zz[1]=1
    zz[2]=8
    zz[3]=16
    zz[4]=9
    zz[5]=2
    zz[6]=3
    zz[7]=10
    zz[8]=17
    zz[9]=24
    zz[10]=32
    zz[11]=25
    zz[12]=18
    zz[13]=11
    zz[14]=4
    zz[15]=5
    zz[16]=12
    zz[17]=19
    zz[18]=26
    zz[19]=33
    zz[20]=40
    zz[21]=48
    zz[22]=41
    zz[23]=34
    zz[24]=27
    zz[25]=20
    zz[26]=13
    zz[27]=6
    zz[28]=7
    zz[29]=14
    zz[30]=21
    zz[31]=28
    zz[32]=35
    zz[33]=42
    zz[34]=49
    zz[35]=56
    zz[36]=57
    zz[37]=50
    zz[38]=43
    zz[39]=36
    zz[40]=29
    zz[41]=22
    zz[42]=15
    zz[43]=23
    zz[44]=30
    zz[45]=37
    zz[46]=44
    zz[47]=51
    zz[48]=58
    zz[49]=59
    zz[50]=52
    zz[51]=45
    zz[52]=38
    zz[53]=31
    zz[54]=39
    zz[55]=46
    zz[56]=53
    zz[57]=60
    zz[58]=61
    zz[59]=54
    zz[60]=47
    zz[61]=55
    zz[62]=62
    zz[63]=63
    return

# 建标准 DC 亮度表 + 小型完备 AC 表，装入编码器 je 与解码器 jd（tc=0 DC / 1 AC，th=0）。
fnc build_tables: je: &, jd: &
    # —— DC 亮度：counts {0,1,5,1,1,1,1,1,1,0,...}，values 0..11 ——
    var dcc[16]: u1
    var i: u4 = 0
    while i < 16
        dcc[i] = 0
        i = i + 1
    dcc[1] = 1
    dcc[2] = 5
    dcc[3] = 1
    dcc[4] = 1
    dcc[5] = 1
    dcc[6] = 1
    dcc[7] = 1
    dcc[8] = 1
    var dcv[12]: u1
    i = 0
    while i < 12
        dcv[i] = (i: u1)
        i = i + 1
    assert codec_jenc_dht(je, 0, 0, (&dcc[0]: u1&), (&dcv[0]: u1&)) == 0, "jenc dc dht"
    assert codec_jdec_dht(jd, 0, 0, (&dcc[0]: u1&), (&dcv[0]: u1&)) == 0, "jdec dc dht"
    # —— AC 小完备表：8 个长度=3 的码（counts {0,0,8,0,...}） ——
    var acc[16]: u1
    i = 0
    while i < 16
        acc[i] = 0
        i = i + 1
    acc[2] = 8
    var acv[8]: u1
    acv[0] = 0x00
    acv[1] = 0x01
    acv[2] = 0x02
    acv[3] = 0x11
    acv[4] = 0x21
    acv[5] = 0x31
    acv[6] = 0x41
    acv[7] = 0xF0
    assert codec_jenc_dht(je, 1, 0, (&acc[0]: u1&), (&acv[0]: u1&)) == 0, "jenc ac dht"
    assert codec_jdec_dht(jd, 1, 0, (&acc[0]: u1&), (&acv[0]: u1&)) == 0, "jdec ac dht"
    return

# 造三块 DU（zigzag 序，du[0]=DC 绝对值），仅用小 AC 表覆盖的 (run,size)。
#   块0：DC=5；AC pos1=1,pos2=-1,pos3=2，后续 0（EOB）。
#   块1：DC=5（差分 0 → DC 符号 0）；pos5=1（0x41）,pos6=1（0x01），EOB。
#   块2：DC=3（差分 -2）；pos17=1（0xF0 + 0x01），EOB。
fnc make_du: du: i4&
    var i: u4 = 0
    while i < 192
        du[i] = 0
        i = i + 1
    du[0] = 5
    du[1] = 1
    du[2] = 0 - 1
    du[3] = 2
    du[64 + 0] = 5
    du[64 + 5] = 1
    du[64 + 6] = 1
    du[128 + 0] = 3
    du[128 + 17] = 1
    return

tst "JPEG 熵层块级 round-trip（DC 差分 + AC 游程 + ZRL/EOB）"
    var zz[64]: i4
    fill_zz((&zz[0]: i4&))

    # 三块 DU（zigzag 序，du[0]=DC 绝对值），仅用小 AC 表覆盖的 (run,size)。
    #   块0：DC=5；AC pos1=1,pos2=-1,pos3=2，后续 0（EOB）。
    #   块1：DC=5（差分 0 → DC 符号 0）；pos1..4=0,pos5=1（0x41）,pos6=1（0x01），EOB。
    #   块2：DC=3（差分 -2）；pos1..16=0,pos17=1（0xF0 + 0x01），EOB。
    var du[192]: i4
    var i: u4 = 0
    while i < 192
        du[i] = 0
        i = i + 1
    # 块0
    du[0] = 5
    du[1] = 1
    du[2] = 0 - 1
    du[3] = 2
    # 块1（偏移 64）
    du[64 + 0] = 5
    du[64 + 5] = 1
    du[64 + 6] = 1
    # 块2（偏移 128）
    du[128 + 0] = 3
    du[128 + 17] = 1

    var jesz: u8 = codec_jenc_size()
    var je: & = chunk(jesz)
    assert codec_jenc_init(je) == 0, "jenc init"
    var jdsz: u8 = codec_jdec_size()
    var jd: & = chunk(jdsz)
    assert codec_jdec_init(jd) == 0, "jdec init"
    build_tables(je, jd)

    # 编码三块（DC 预测在编码侧推进）。
    var epred: i4 = 0
    var b: u4 = 0
    while b < 3
        assert codec_jenc_block(je, (&du[b * 64]: i4&), 0, 0, &epred) == 1, "jenc block"
        b = b + 1
    assert codec_jenc_flush(je) == 0, "jenc flush"

    # drain 出全部扫描字节。
    var scan[512]: u1
    var slen: u8 = 0
    var got: i8 = codec_jenc_drain(je, (&scan[0]: u1&), 512)
    while got > 0
        slen = slen + (got: u8)
        got = codec_jenc_drain(je, (&scan[slen]: u1&), (512 - slen: u8))
    assert slen > 0, "有扫描字节产出"
    # 追加 EOI，使解码器在熵末尾见 marker 而非缺字节挂起。
    scan[slen] = 0xFF
    scan[slen + 1] = 0xD9
    slen = slen + 2

    # feed 全部字节，逐块解码，校验自然序系数与原 DU 对齐。
    assert codec_jdec_feed(jd, (&scan[0]: &), slen) == (slen: i8), "jdec feed"
    var data[64]: i2
    var dpred: i4 = 0
    b = 0
    while b < 3
        var r: i4 = codec_jdec_block(jd, (&data[0]: i2&), 0, 0, &dpred)
        assert r == 1, "jdec block 完成"
        var k: u4 = 0
        var ok: i4 = 1
        while k < 64
            var nat: i4 = zz[k]
            if (data[nat]: i4) != du[b * 64 + k]
                ok = 0
            k = k + 1
        assert ok == 1, "块系数逐位 round-trip"
        b = b + 1

    codec_jenc_free(je)
    codec_jdec_free(jd)
    recycle((je: &))
    recycle((jd: &))

tst "JPEG 熵层流式：逐字节喂 + 块级回滚（need_more 挂起/重解）"
    var zz[64]: i4
    fill_zz((&zz[0]: i4&))
    var du[192]: i4
    make_du((&du[0]: i4&))

    var je: & = chunk(codec_jenc_size())
    assert codec_jenc_init(je) == 0, "jenc init"
    var jd: & = chunk(codec_jdec_size())
    assert codec_jdec_init(jd) == 0, "jdec init"
    build_tables(je, jd)

    # 编码三块 + drain 出扫描字节，尾附 EOI。
    var epred: i4 = 0
    var b: u4 = 0
    while b < 3
        assert codec_jenc_block(je, (&du[b * 64]: i4&), 0, 0, &epred) == 1, "jenc block"
        b = b + 1
    assert codec_jenc_flush(je) == 0, "jenc flush"
    var scan[512]: u1
    var slen: u8 = 0
    var got: i8 = codec_jenc_drain(je, (&scan[0]: u1&), 512)
    while got > 0
        slen = slen + (got: u8)
        got = codec_jenc_drain(je, (&scan[slen]: u1&), (512 - slen: u8))
    scan[slen] = 0xFF
    scan[slen + 1] = 0xD9
    slen = slen + 2

    # 逐字节喂：每次 feed 1 字节后尝试解出尽量多的块；块缺字节即回滚等待下一字节。
    var data[64]: i2
    var dpred: i4 = 0
    var fedpos: u8 = 0
    b = 0
    while b < 3
        var r: i4 = codec_jdec_block(jd, (&data[0]: i2&), 0, 0, &dpred)
        if r == 1
            var k: u4 = 0
            var ok: i4 = 1
            while k < 64
                if (data[zz[k]]: i4) != du[b * 64 + k]
                    ok = 0
                k = k + 1
            assert ok == 1, "流式块系数逐位 round-trip"
            b = b + 1
        else
            assert r == 0, "缺字节应回滚返回 0（非错）"
            assert fedpos < slen, "尚有字节可喂（无死锁）"
            assert codec_jdec_feed(jd, (&scan[fedpos]: &), 1) == 1, "喂 1 字节"
            fedpos = fedpos + 1

    codec_jenc_free(je)
    codec_jdec_free(jd)
    recycle((je: &))
    recycle((jd: &))
