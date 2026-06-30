# codec 单元测试：批1（校验和 + RLE）
#   CRC-32:  IEEE 802.3 标准检验值（"123456789" -> 0xCBF43926）
#   Adler-32: RFC 1950（"Wikipedia" -> 0x11E60398）
#   PackBits: round-trip（编码→解码还原）+ 流式分块一致性
# 运行：scc tests/cases/codec_test.sc --test
#
# 被测组件 builtins/codec/codec.sc 经默认 builtins 搜索路径 inc（自动链接 codec_impl.c）。

inc codec.sc

tst "CRC-32 IEEE 802.3 标准检验值"
    # 经典检验值："123456789" 的 CRC-32 = 0xCBF43926。
    assert codec_crc32("123456789", 9) == 0xCBF43926, "crc32('123456789')"
    # 空输入 -> 0。
    assert codec_crc32("", 0) == 0x00000000, "crc32('')"
    # "The quick brown fox jumps over the lazy dog" -> 0x414FA339。
    assert codec_crc32("The quick brown fox jumps over the lazy dog", 43) == 0x414FA339, "crc32(fox)"

tst "CRC-32 流式分块与一次性一致"
    # 分两段累进，结果须等于一次性。
    var c: u4 = 0
    c = codec_crc32_update(0, "12345", 5)
    c = codec_crc32_update(c, "6789", 4)
    assert c == 0xCBF43926, "crc32 streaming"

tst "Adler-32 RFC 1950 向量"
    assert codec_adler32("Wikipedia", 9) == 0x11E60398, "adler32('Wikipedia')"
    # 空输入 -> 1。
    assert codec_adler32("", 0) == 0x00000001, "adler32('')"
    # "abc" -> 0x024D0127。
    assert codec_adler32("abc", 3) == 0x024D0127, "adler32('abc')"

tst "Adler-32 流式分块与一次性一致"
    var a: u4 = 1
    a = codec_adler32_update(1, "Wiki", 4)
    a = codec_adler32_update(a, "pedia", 5)
    assert a == 0x11E60398, "adler32 streaming"

tst "PackBits RLE round-trip（含重复块与字面块）"
    # 构造一段含长重复与离散字面的数据：64 字节全 0xAB，再 8 字节递增，再 32 字节 0x5A。
    var src[104]: u1
    var i: u4 = 0
    while i < 64
        src[i] = 0xAB
        i = i + 1
    while i < 72
        src[i] = (i: u1)
        i = i + 1
    while i < 104
        src[i] = 0x5A
        i = i + 1
    # 编码。
    var bound: u8 = codec_rle_bound(104)
    var enc[256]: u1
    var elen: i8 = codec_rle_encode((&src[0]: u1&), 104, (&enc[0]: u1&), 256)
    assert elen > 0, "rle encode ok"
    # 高重复数据应明显压缩（远小于原 104 字节）。
    assert elen < 50, "rle compresses runs"
    # 解码还原。
    var dec[256]: u1
    var dlen: i8 = codec_rle_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 256)
    assert dlen == 104, "rle decode length"
    # 逐字节比对。
    var same: i4 = 1
    i = 0
    while i < 104
        if dec[i] != src[i]
            same = 0
        i = i + 1
    assert same == 1, "rle round-trip identical"

tst "PackBits cap 不足返回 -1"
    var src2[40]: u1
    var i2: u4 = 0
    while i2 < 40
        src2[i2] = (i2: u1)
        i2 = i2 + 1
    var tiny[4]: u1
    var r: i8 = codec_rle_encode((&src2[0]: u1&), 40, (&tiny[0]: u1&), 4)
    assert r == -1, "rle encode cap overflow"

# ===== 批2：DEFLATE inflate / zlib / gzip 解封装 =====
# 向量由 Python zlib/gzip（level 9）生成；zlib 内部校验 Adler-32、gzip 内部校验 CRC-32/ISIZE，
# 故返回值非 -1 即已与嵌入校验和交叉印证正确性。

tst "zlib 解封装（动态 Huffman + Adler-32 校验，原文 307 字节）"
    var zdata[92]: u1 = [0x78, 0xda, 0x2b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56, 0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a, 0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28, 0x01, 0x4a, 0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0xeb, 0x81, 0x79, 0xc3, 0x5a, 0xb1, 0x8b, 0xab, 0x9b, 0x8f, 0x63, 0x88, 0xab, 0x42, 0x51, 0x7e, 0x69, 0x5e, 0x8a, 0x6e, 0x49, 0x51, 0x66, 0x81, 0x42, 0x59, 0x66, 0xa2, 0x42, 0x72, 0x7e, 0x4a, 0x6a, 0x72, 0x7c, 0x66, 0x5e, 0x5a, 0x4e, 0x62, 0x49, 0xaa, 0x22, 0x00, 0x04, 0x4a, 0x6e, 0xb5]
    var obuf[512]: u1
    var n: i8 = codec_zlib_decode((&zdata[0]: u1&), 92, (&obuf[0]: u1&), 512)
    assert n == 307, "zlib decode length"

tst "gzip 解封装（CRC-32 / ISIZE 校验，原文 307 字节）"
    var gdata[104]: u1 = [0x1f, 0x8b, 0x08, 0x00, 0x3b, 0xc9, 0x43, 0x6a, 0x02, 0xff, 0x2b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56, 0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a, 0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28, 0x01, 0x4a, 0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0xeb, 0x81, 0x79, 0xc3, 0x5a, 0xb1, 0x8b, 0xab, 0x9b, 0x8f, 0x63, 0x88, 0xab, 0x42, 0x51, 0x7e, 0x69, 0x5e, 0x8a, 0x6e, 0x49, 0x51, 0x66, 0x81, 0x42, 0x59, 0x66, 0xa2, 0x42, 0x72, 0x7e, 0x4a, 0x6a, 0x72, 0x7c, 0x66, 0x5e, 0x5a, 0x4e, 0x62, 0x49, 0xaa, 0x22, 0x00, 0xf6, 0xd8, 0xdd, 0x4a, 0x33, 0x01, 0x00, 0x00]
    var obuf[512]: u1
    var n: i8 = codec_gzip_decode((&gdata[0]: u1&), 104, (&obuf[0]: u1&), 512)
    assert n == 307, "gzip decode length"

tst "zlib 小数据解码并逐字节比对"
    var zsmall[21]: u1 = [0x78, 0xda, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x22, 0xcb, 0xf3, 0x8b, 0x72, 0x52, 0x00, 0x68, 0x7d, 0x08, 0xc5]
    var obuf[64]: u1
    var n: i8 = codec_zlib_decode((&zsmall[0]: u1&), 21, (&obuf[0]: u1&), 64)
    assert n == 23, "zsmall length"
    var exp: char& = "hello hello hello world"
    var ok: i4 = 1
    var i: u4 = 0
    while i < 23
        if obuf[i] != (exp[i]: u1)
            ok = 0
        i = i + 1
    assert ok == 1, "zsmall content"

tst "raw DEFLATE inflate（跳过 zlib 头尾，直接解原始码流）"
    var zsmall[21]: u1 = [0x78, 0xda, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x22, 0xcb, 0xf3, 0x8b, 0x72, 0x52, 0x00, 0x68, 0x7d, 0x08, 0xc5]
    var obuf[64]: u1
    # 跳过 zlib 2 字节头；raw deflate 长度 = 21 - 2(头) - 4(Adler) = 15。
    var n: i8 = codec_inflate((&zsmall[2]: u1&), 15, (&obuf[0]: u1&), 64)
    assert n == 23, "raw inflate length"

# ===== 批2：DEFLATE 编码 + zlib/gzip 封装（自洽 round-trip）=====
# 自实现 encode → 自实现 decode 全链路还原，且 decode 内部校验 Adler-32/CRC-32。

tst "deflate(level 1) → inflate round-trip（高重复数据应压缩）"
    # 构造 512 字节高重复数据。
    var src[512]: u1
    var i: u4 = 0
    while i < 512
        src[i] = ((i % 16): u1)
        i = i + 1
    var enc[1024]: u1
    var elen: i8 = codec_deflate((&src[0]: u1&), 512, (&enc[0]: u1&), 1024, 1)
    assert elen > 0, "deflate ok"
    assert elen < 512, "deflate compresses repetitive"
    var dec[1024]: u1
    var dlen: i8 = codec_inflate((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 1024)
    assert dlen == 512, "inflate length"
    var ok: i4 = 1
    i = 0
    while i < 512
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "deflate round-trip identical"

tst "deflate(level 0, stored) → inflate round-trip"
    var src[300]: u1
    var i: u4 = 0
    while i < 300
        src[i] = ((i * 37 + 5): u1)
        i = i + 1
    var enc[512]: u1
    var elen: i8 = codec_deflate((&src[0]: u1&), 300, (&enc[0]: u1&), 512, 0)
    assert elen > 0, "stored deflate ok"
    var dec[512]: u1
    var dlen: i8 = codec_inflate((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 512)
    assert dlen == 300, "stored inflate length"
    var ok: i4 = 1
    i = 0
    while i < 300
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "stored round-trip identical"

tst "zlib encode → zlib decode round-trip（校验 Adler-32）"
    var src[400]: u1
    var i: u4 = 0
    while i < 400
        src[i] = ((i % 7) + 65: u1)
        i = i + 1
    var enc[1024]: u1
    var elen: i8 = codec_zlib_encode((&src[0]: u1&), 400, (&enc[0]: u1&), 1024, 1)
    assert elen > 0, "zlib encode ok"
    var dec[1024]: u1
    var dlen: i8 = codec_zlib_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 1024)
    assert dlen == 400, "zlib decode length (adler ok)"
    var ok: i4 = 1
    i = 0
    while i < 400
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "zlib round-trip identical"

tst "gzip encode → gzip decode round-trip（校验 CRC-32/ISIZE）"
    var src[400]: u1
    var i: u4 = 0
    while i < 400
        src[i] = ((i % 11): u1)
        i = i + 1
    var enc[1024]: u1
    var elen: i8 = codec_gzip_encode((&src[0]: u1&), 400, (&enc[0]: u1&), 1024, 1)
    assert elen > 0, "gzip encode ok"
    var dec[1024]: u1
    var dlen: i8 = codec_gzip_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 1024)
    assert dlen == 400, "gzip decode length (crc ok)"
    var ok: i4 = 1
    i = 0
    while i < 400
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "gzip round-trip identical"

# ===== Layer 0：规范 Huffman 熵编码原子 =====

tst "Huffman 建树：频率越高码越短，且满足 Kraft 完备"
    # 4 个符号，频率 8/4/2/2 → 最优码长应为 1/2/3/3（Kraft = 1/2+1/4+1/8+1/8 = 1）
    var freq[4]: u4 = [8, 4, 2, 2]
    var len[4]: u1 = [0, 0, 0, 0]
    var maxlen: i4 = codec_huffman_build((&freq[0]: u4&), 4, 15, (&len[0]: u1&))
    assert maxlen == 3, "max code length"
    assert len[0] == 1, "freq 8 -> len 1"
    assert len[1] == 2, "freq 4 -> len 2"
    assert len[2] == 3, "freq 2 -> len 3"
    assert len[3] == 3, "freq 2 -> len 3"
    # Kraft 和 = Σ 2^(-len) ，用 2^maxlen 标度求整数和应 == 2^maxlen
    var kraft: i4 = 0
    var i: u4 = 0
    while i < 4
        kraft = kraft + (8 >> len[i])
        i = i + 1
    assert kraft == 8, "Kraft 完备 (== 2^maxlen)"

tst "Huffman 建树：单符号退化为码长 1"
    var freq[3]: u4 = [0, 5, 0]
    var len[3]: u1 = [9, 9, 9]
    var maxlen: i4 = codec_huffman_build((&freq[0]: u4&), 3, 15, (&len[0]: u1&))
    assert maxlen == 1, "single symbol max len"
    assert len[0] == 0, "absent symbol len 0"
    assert len[1] == 1, "only symbol len 1"
    assert len[2] == 0, "absent symbol len 0"

tst "Huffman order-0 编码 → 解码 round-trip（偏斜文本）"
    var src[120]: u1
    var i: u4 = 0
    while i < 120
        # 高度偏斜：大量 'A'，少量其他 → Huffman 应显著压缩
        if i % 5 == 0
            src[i] = ((66 + (i % 7)): u1)
        else
            src[i] = 65
        i = i + 1
    var enc[512]: u1
    var elen: i8 = codec_huffman_encode((&src[0]: u1&), 120, (&enc[0]: u1&), 512)
    assert elen > 0, "huffman encode ok"
    var dec[120]: u1
    var dlen: i8 = codec_huffman_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 120)
    assert dlen == 120, "huffman decode length"
    var ok: i4 = 1
    i = 0
    while i < 120
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "huffman round-trip identical"

tst "Huffman order-0：全相同字节 + 空输入边界"
    # 全相同 → 单符号码（码长 1），仍须正确还原
    var same[64]: u1
    var i: u4 = 0
    while i < 64
        same[i] = 200
        i = i + 1
    var enc[256]: u1
    var elen: i8 = codec_huffman_encode((&same[0]: u1&), 64, (&enc[0]: u1&), 256)
    assert elen > 0, "all-same encode ok"
    var dec[64]: u1
    var dlen: i8 = codec_huffman_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 64)
    assert dlen == 64, "all-same decode length"
    var ok: i4 = 1
    i = 0
    while i < 64
        if dec[i] != 200
            ok = 0
        i = i + 1
    assert ok == 1, "all-same round-trip identical"
    # 空输入 → 仅 8 字节头
    var e2[16]: u1
    var el2: i8 = codec_huffman_encode((&same[0]: u1&), 0, (&e2[0]: u1&), 16)
    assert el2 == 8, "empty encode = 8-byte header"
    var d2[8]: u1
    var dl2: i8 = codec_huffman_decode((&e2[0]: u1&), 8, (&d2[0]: u1&), 8)
    assert dl2 == 0, "empty decode = 0"

# ===== 批4：动态 Huffman（DEFLATE type 2，level >= 2）=====
# level 2 按频率建 litlen/dist 树并写动态块头；本模块 inflate 经 cd_dynamic 路径还原。

tst "deflate(level 2, 动态 Huffman) → inflate round-trip（偏斜分布）"
    # 强偏斜字节分布：动态树应优于固定树。
    var src[600]: u1
    var i: u4 = 0
    while i < 600
        if (i % 5) == 0
            src[i] = ((i % 4): u1)
        else
            src[i] = 65
        i = i + 1
    var enc[1200]: u1
    var elen: i8 = codec_deflate((&src[0]: u1&), 600, (&enc[0]: u1&), 1200, 2)
    assert elen > 0, "dynamic deflate ok"
    assert elen < 600, "dynamic compresses skewed data"
    # 确认确实走的是动态块：BTYPE 位 = 10（首字节 bit1..2）。
    var btype: u4 = (((enc[0]: u4) >> 1) & 3)
    assert btype == 2, "block type is dynamic (10)"
    var dec[1200]: u1
    var dlen: i8 = codec_inflate((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 1200)
    assert dlen == 600, "dynamic inflate length"
    var ok: i4 = 1
    i = 0
    while i < 600
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "dynamic round-trip identical"

tst "deflate(level 2) 动态优于固定，且 zlib/gzip 封装可还原"
    # 含长程重复 + 偏斜字面：动态树通常比固定树更短。
    var src[800]: u1
    var i: u4 = 0
    while i < 800
        src[i] = (((i / 8) % 3): u1)
        i = i + 1
    var e1[1600]: u1
    var e2[1600]: u1
    var l1: i8 = codec_deflate((&src[0]: u1&), 800, (&e1[0]: u1&), 1600, 1)
    var l2: i8 = codec_deflate((&src[0]: u1&), 800, (&e2[0]: u1&), 1600, 2)
    assert l1 > 0, "fixed ok"
    assert l2 > 0, "dynamic ok"
    assert l2 <= l1, "dynamic not larger than fixed on structured data"
    # zlib（level 2）round-trip
    var zenc[1600]: u1
    var zl: i8 = codec_zlib_encode((&src[0]: u1&), 800, (&zenc[0]: u1&), 1600, 2)
    assert zl > 0, "zlib(level2) encode ok"
    var zdec[1600]: u1
    var zdl: i8 = codec_zlib_decode((&zenc[0]: u1&), (zl: u8), (&zdec[0]: u1&), 1600)
    assert zdl == 800, "zlib(level2) decode length"
    var ok: i4 = 1
    i = 0
    while i < 800
        if zdec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "zlib(level2) round-trip identical"

tst "deflate(level 2) 边界：全字面无匹配 + 空输入 + 单字节"
    # 无任何 3 字节重复 → 无距离码，动态块须发 1 个占位距离码。
    var src[256]: u1
    var i: u4 = 0
    while i < 256
        src[i] = (i: u1)
        i = i + 1
    var enc[1024]: u1
    var elen: i8 = codec_deflate((&src[0]: u1&), 256, (&enc[0]: u1&), 1024, 2)
    assert elen > 0, "no-match dynamic ok"
    var dec[1024]: u1
    var dlen: i8 = codec_inflate((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 1024)
    assert dlen == 256, "no-match inflate length"
    var ok: i4 = 1
    i = 0
    while i < 256
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "no-match round-trip identical"
    # 空输入：动态块仅 EOB
    var ee[32]: u1
    var el: i8 = codec_deflate((&src[0]: u1&), 0, (&ee[0]: u1&), 32, 2)
    assert el > 0, "empty dynamic ok"
    var ed[8]: u1
    var edl: i8 = codec_inflate((&ee[0]: u1&), (el: u8), (&ed[0]: u1&), 8)
    assert edl == 0, "empty dynamic inflate = 0"
    # 单字节
    var one[1]: u1 = [42]
    var oe[32]: u1
    var ol: i8 = codec_deflate((&one[0]: u1&), 1, (&oe[0]: u1&), 32, 2)
    assert ol > 0, "single-byte dynamic ok"
    var od[4]: u1
    var odl: i8 = codec_inflate((&oe[0]: u1&), (ol: u8), (&od[0]: u1&), 4)
    assert odl == 1, "single-byte inflate length"
    assert od[0] == 42, "single-byte value"

# ===== 批5：ANS 熵编码（静态 rANS，Layer 0 簇 5）=====
# 与 Huffman 并列的熵编码原子：频率归一 + order-0 字节编解码，自洽 round-trip。

tst "rANS 频率归一：和恰为 2^tablelog，出现符号 >=1"
    var freq[4]: u4 = [10, 0, 3, 1]
    var norm[4]: u2
    var tl: i4 = codec_rans_normalize((&freq[0]: u4&), 4, 12, (&norm[0]: u2&))
    assert tl == 12, "tablelog returned"
    # 总和 == 4096
    var sum: i4 = ((norm[0]: i4) + (norm[1]: i4) + (norm[2]: i4) + (norm[3]: i4))
    assert sum == 4096, "normalized total == 4096"
    # 出现的符号 >= 1，未出现的为 0
    assert norm[0] >= 1, "present symbol 0 >= 1"
    assert norm[1] == 0, "absent symbol 1 == 0"
    assert norm[2] >= 1, "present symbol 2 >= 1"
    assert norm[3] >= 1, "present symbol 3 >= 1"
    # 频率越高归一值越大
    assert norm[0] > norm[2], "higher freq -> larger norm"
    assert norm[2] > norm[3], "ordering preserved"

tst "rANS order-0 编码 → 解码 round-trip（偏斜文本）"
    var src[512]: u1
    var i: u4 = 0
    while i < 512
        if (i % 8) == 0
            src[i] = ((i % 5): u1)
        else
            src[i] = 88
        i = i + 1
    var enc[2048]: u1
    var elen: i8 = codec_rans_encode((&src[0]: u1&), 512, (&enc[0]: u1&), 2048)
    assert elen > 0, "rans encode ok"
    var dec[512]: u1
    var dlen: i8 = codec_rans_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 512)
    assert dlen == 512, "rans decode length"
    var ok: i4 = 1
    i = 0
    while i < 512
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "rans round-trip identical"
    # 负载（去掉 521 字节自描述头）应小于原文：偏斜数据可压缩
    var payload: i8 = (elen - 521)
    assert payload < 512, "rans compresses skewed payload"

tst "rANS 边界：全相同字节 + 空输入 + 全字节谱"
    # 全相同：负载应极小（状态 4 字节）
    var same[300]: u1
    var i: u4 = 0
    while i < 300
        same[i] = 77
        i = i + 1
    var senc[2048]: u1
    var sl: i8 = codec_rans_encode((&same[0]: u1&), 300, (&senc[0]: u1&), 2048)
    assert sl > 0, "all-same encode ok"
    assert (sl - 521) <= 8, "all-same payload tiny"
    var sdec[300]: u1
    var sdl: i8 = codec_rans_decode((&senc[0]: u1&), (sl: u8), (&sdec[0]: u1&), 300)
    assert sdl == 300, "all-same decode length"
    var ok: i4 = 1
    i = 0
    while i < 300
        if sdec[i] != 77
            ok = 0
        i = i + 1
    assert ok == 1, "all-same round-trip identical"
    # 空输入 → 仅 8 字节头
    var e2[16]: u1
    var el2: i8 = codec_rans_encode((&same[0]: u1&), 0, (&e2[0]: u1&), 16)
    assert el2 == 8, "empty encode = 8-byte header"
    var d2[8]: u1
    var dl2: i8 = codec_rans_decode((&e2[0]: u1&), 8, (&d2[0]: u1&), 8)
    assert dl2 == 0, "empty decode = 0"
    # 全 256 字节谱（含所有符号）round-trip
    var spec[256]: u1
    i = 0
    while i < 256
        spec[i] = (i: u1)
        i = i + 1
    var penc[2048]: u1
    var pl: i8 = codec_rans_encode((&spec[0]: u1&), 256, (&penc[0]: u1&), 2048)
    assert pl > 0, "full-spectrum encode ok"
    var pdec[256]: u1
    var pdl: i8 = codec_rans_decode((&penc[0]: u1&), (pl: u8), (&pdec[0]: u1&), 256)
    assert pdl == 256, "full-spectrum decode length"
    ok = 1
    i = 0
    while i < 256
        if pdec[i] != (i: u1)
            ok = 0
        i = i + 1
    assert ok == 1, "full-spectrum round-trip identical"

# ===== 批6：区间编码（算术编码字节实现，Layer 0 簇 6）=====
# 熵编码家族第三个原子：Subbotin 无进位区间编码，order-0 字节自洽 round-trip。

tst "区间编码 order-0 编码 → 解码 round-trip（偏斜文本）"
    var src[512]: u1
    var i: u4 = 0
    while i < 512
        if (i % 8) == 0
            src[i] = ((i % 5): u1)
        else
            src[i] = 88
        i = i + 1
    var enc[2048]: u1
    var elen: i8 = codec_range_encode((&src[0]: u1&), 512, (&enc[0]: u1&), 2048)
    assert elen > 0, "range encode ok"
    var dec[512]: u1
    var dlen: i8 = codec_range_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 512)
    assert dlen == 512, "range decode length"
    var ok: i4 = 1
    i = 0
    while i < 512
        if dec[i] != src[i]
            ok = 0
        i = i + 1
    assert ok == 1, "range round-trip identical"
    # 负载（去掉 521 字节自描述头）应小于原文：偏斜数据可压缩
    var payload: i8 = (elen - 521)
    assert payload < 512, "range compresses skewed payload"

tst "区间编码 边界：全相同字节 + 空输入 + 全字节谱"
    # 全相同：负载应极小
    var same[300]: u1
    var i: u4 = 0
    while i < 300
        same[i] = 77
        i = i + 1
    var senc[2048]: u1
    var sl: i8 = codec_range_encode((&same[0]: u1&), 300, (&senc[0]: u1&), 2048)
    assert sl > 0, "all-same encode ok"
    assert (sl - 521) <= 16, "all-same payload tiny"
    var sdec[300]: u1
    var sdl: i8 = codec_range_decode((&senc[0]: u1&), (sl: u8), (&sdec[0]: u1&), 300)
    assert sdl == 300, "all-same decode length"
    var ok: i4 = 1
    i = 0
    while i < 300
        if sdec[i] != 77
            ok = 0
        i = i + 1
    assert ok == 1, "all-same round-trip identical"
    # 空输入 → 仅 8 字节头
    var e2[16]: u1
    var el2: i8 = codec_range_encode((&same[0]: u1&), 0, (&e2[0]: u1&), 16)
    assert el2 == 8, "empty encode = 8-byte header"
    var d2[8]: u1
    var dl2: i8 = codec_range_decode((&e2[0]: u1&), 8, (&d2[0]: u1&), 8)
    assert dl2 == 0, "empty decode = 0"
    # 全 256 字节谱（含所有符号）round-trip
    var spec[256]: u1
    i = 0
    while i < 256
        spec[i] = (i: u1)
        i = i + 1
    var penc[2048]: u1
    var pl: i8 = codec_range_encode((&spec[0]: u1&), 256, (&penc[0]: u1&), 2048)
    assert pl > 0, "full-spectrum encode ok"
    var pdec[256]: u1
    var pdl: i8 = codec_range_decode((&penc[0]: u1&), (pl: u8), (&pdec[0]: u1&), 256)
    assert pdl == 256, "full-spectrum decode length"
    ok = 1
    i = 0
    while i < 256
        if pdec[i] != (i: u1)
            ok = 0
        i = i + 1
    assert ok == 1, "full-spectrum round-trip identical"

tst "LZW 字典编码 → 解码 round-trip（重复文本可压缩）"
    var rep[3000]: u1
    var i: u4 = 0
    while i < 3000
        rep[i] = ((i % 4): u1)
        i = i + 1
    var enc[6500]: u1
    var elen: i8 = codec_lzw_encode((&rep[0]: u1&), 3000, (&enc[0]: u1&), 6500)
    assert elen > 0, "lzw encode ok"
    # 去掉 8 字节自描述头后应明显小于原文
    assert (elen - 8) < 3000, "lzw compresses repetitive payload"
    var dec[3000]: u1
    var dlen: i8 = codec_lzw_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 3000)
    assert dlen == 3000, "lzw decode length"
    var ok: i4 = 1
    i = 0
    while i < 3000
        if dec[i] != rep[i]
            ok = 0
        i = i + 1
    assert ok == 1, "lzw round-trip identical"

tst "LZW 大输入：字典增长 + 满表 CLEAR 复位（伪随机）"
    var big[6000]: u1
    var i: u4 = 0
    var seed: u4 = 2463534242
    while i < 6000
        seed = seed * 1103515245 + 12345
        big[i] = ((seed >> 16): u1)
        i = i + 1
    var enc[13000]: u1
    var elen: i8 = codec_lzw_encode((&big[0]: u1&), 6000, (&enc[0]: u1&), 13000)
    assert elen > 0, "lzw big encode ok"
    var dec[6000]: u1
    var dlen: i8 = codec_lzw_decode((&enc[0]: u1&), (elen: u8), (&dec[0]: u1&), 6000)
    assert dlen == 6000, "lzw big decode length"
    var ok: i4 = 1
    i = 0
    while i < 6000
        if dec[i] != big[i]
            ok = 0
        i = i + 1
    assert ok == 1, "lzw big round-trip identical（含 9→12 位升宽与 CLEAR）"

tst "LZW 边界：空输入 + 单字节 + 全相同"
    var one[1]: u1
    one[0] = 200
    var e1[64]: u1
    var l1: i8 = codec_lzw_encode((&one[0]: u1&), 1, (&e1[0]: u1&), 64)
    assert l1 > 8, "single-byte encode > header"
    var d1[1]: u1
    var dl1: i8 = codec_lzw_decode((&e1[0]: u1&), (l1: u8), (&d1[0]: u1&), 1)
    assert dl1 == 1, "single-byte decode length"
    assert d1[0] == 200, "single-byte round-trip"
    # 空输入 → 仅 8 字节头
    var e0: i8 = codec_lzw_encode((&one[0]: u1&), 0, (&e1[0]: u1&), 64)
    assert e0 == 8, "empty encode = 8-byte header"
    var d0buf[8]: u1
    var d0: i8 = codec_lzw_decode((&e1[0]: u1&), 8, (&d0buf[0]: u1&), 8)
    assert d0 == 0, "empty decode = 0"
    # 全相同长串
    var same[2000]: u1
    var i: u4 = 0
    while i < 2000
        same[i] = 42
        i = i + 1
    var es[4500]: u1
    var ls: i8 = codec_lzw_encode((&same[0]: u1&), 2000, (&es[0]: u1&), 4500)
    assert ls > 0, "all-same encode ok"
    assert (ls - 8) < 2000, "all-same compresses"
    var ds[2000]: u1
    var dls: i8 = codec_lzw_decode((&es[0]: u1&), (ls: u8), (&ds[0]: u1&), 2000)
    assert dls == 2000, "all-same decode length"
    var ok: i4 = 1
    i = 0
    while i < 2000
        if ds[i] != 42
            ok = 0
        i = i + 1
    assert ok == 1, "all-same round-trip identical"

tst "裸 LZW（TIFF/MSB-first）解码真实 libtiff 子流 ← 地面真值互通"
    # tiff_small：16×8 灰度图，像素由 LCG(seed=0x1234) 生成，libtiff 压成 147 字节 LZW 子流。
    # 此向量取自 Pillow 的 tiff_lzw 输出（见 tests/data/gen_tiff_lzw.py），锁定与真实 libtiff 互通。
    var lzw[147]: u1 = [128, 50, 215, 205, 53, 50, 4, 32, 88, 111, 141, 8, 106, 99, 179, 24, 66, 209, 115, 45, 8, 132, 246, 96, 76, 200, 82, 6, 147, 12, 46, 118, 58, 92, 30, 211, 124, 14, 24, 162, 135, 129, 241, 72, 171, 60, 28, 149, 14, 86, 1, 168, 224, 11, 4, 139, 217, 168, 240, 184, 16, 24, 130, 42, 23, 192, 139, 6, 33, 157, 188, 122, 86, 139, 212, 231, 16, 18, 193, 46, 183, 73, 162, 3, 225, 103, 185, 213, 104, 221, 26, 164, 11, 199, 241, 169, 37, 254, 233, 116, 137, 140, 138, 97, 208, 236, 26, 181, 90, 154, 23, 130, 103, 145, 85, 128, 119, 100, 11, 159, 203, 1, 144, 125, 136, 64, 0, 35, 201, 201, 97, 232, 112, 40, 127, 49, 150, 91, 36, 34, 9, 36, 232, 125, 84, 64, 64]
    var got[128]: u1
    var n: i8 = codec_lzw_raw_decode((&lzw[0]: u1&), 147, (&got[0]: u1&), 128, 1)
    assert n == 128, "tiff_small 解出 128 字节"
    # 复算同一 LCG 像素序列逐字节对照（PIL: byte=(s>>16)&0xFF，等价 (s>>16):u1）
    var s: u4 = 4660
    var i: u4 = 0
    var ok: i4 = 1
    while i < 128
        s = s * 1103515245 + 12345
        if got[i] != ((s >> 16): u1)
            ok = 0
        i = i + 1
    assert ok == 1, "解码逐字节等于 libtiff 原始像素"

tst "裸 LZW round-trip：MSB(TIFF) 与 LSB 两种字节序，含码宽增长 + 满表 CLEAR"
    var data[6000]: u1
    var s: u4 = 305419896
    var i: u4 = 0
    while i < 6000
        s = s * 1103515245 + 12345
        data[i] = ((s >> 16): u1)
        i = i + 1
    # MSB-first（flags=1，TIFF 习惯）
    var em[13000]: u1
    var lm: i8 = codec_lzw_raw_encode((&data[0]: u1&), 6000, (&em[0]: u1&), 13000, 1)
    assert lm > 0, "MSB encode ok"
    var dm[6000]: u1
    var dlm: i8 = codec_lzw_raw_decode((&em[0]: u1&), (lm: u8), (&dm[0]: u1&), 6000, 1)
    assert dlm == 6000, "MSB decode length"
    var okm: i4 = 1
    i = 0
    while i < 6000
        if dm[i] != data[i]
            okm = 0
        i = i + 1
    assert okm == 1, "MSB round-trip identical（含 9→12 位升宽与 CLEAR）"
    # LSB-first（flags=0）
    var el[13000]: u1
    var ll: i8 = codec_lzw_raw_encode((&data[0]: u1&), 6000, (&el[0]: u1&), 13000, 0)
    assert ll > 0, "LSB encode ok"
    var dl[6000]: u1
    var dll: i8 = codec_lzw_raw_decode((&el[0]: u1&), (ll: u8), (&dl[0]: u1&), 6000, 0)
    assert dll == 6000, "LSB decode length"
    var okl: i4 = 1
    i = 0
    while i < 6000
        if dl[i] != data[i]
            okl = 0
        i = i + 1
    assert okl == 1, "LSB round-trip identical"
    # 两序仅位流字节序不同、码序一致 → 编码长度相等
    assert lm == ll, "两种字节序编码长度一致"

tst "裸 LZW 边界：空输入 + 单字节（两种字节序）"
    var one[1]: u1 = [200]
    var e[64]: u1
    var d1[1]: u1
    # LSB-first
    assert codec_lzw_raw_encode((&one[0]: u1&), 0, (&e[0]: u1&), 64, 0) == 0, "empty raw encode = 0 (LSB)"
    var l0: i8 = codec_lzw_raw_encode((&one[0]: u1&), 1, (&e[0]: u1&), 64, 0)
    assert l0 > 0, "single raw encode ok (LSB)"
    assert codec_lzw_raw_decode((&e[0]: u1&), (l0: u8), (&d1[0]: u1&), 1, 0) == 1, "single decode len (LSB)"
    assert d1[0] == 200, "single raw round-trip (LSB)"
    # MSB-first
    assert codec_lzw_raw_encode((&one[0]: u1&), 0, (&e[0]: u1&), 64, 1) == 0, "empty raw encode = 0 (MSB)"
    var l1: i8 = codec_lzw_raw_encode((&one[0]: u1&), 1, (&e[0]: u1&), 64, 1)
    assert l1 > 0, "single raw encode ok (MSB)"
    assert codec_lzw_raw_decode((&e[0]: u1&), (l1: u8), (&d1[0]: u1&), 1, 1) == 1, "single decode len (MSB)"
    assert d1[0] == 200, "single raw round-trip (MSB)"

tst "varint（LEB128）+ ZigZag 编解码"
    var buf[16]: u1
    # 0 → 1 字节
    assert codec_varint_encode(0, (&buf[0]: u1&)) == 1, "0 → 1 byte"
    # 127 → 1 字节；128 → 2 字节
    assert codec_varint_encode(127, (&buf[0]: u1&)) == 1, "127 → 1 byte"
    assert codec_varint_encode(128, (&buf[0]: u1&)) == 2, "128 → 2 bytes"
    # 16383 → 2 字节；16384 → 3 字节
    assert codec_varint_encode(16383, (&buf[0]: u1&)) == 2, "16383 → 2 bytes"
    assert codec_varint_encode(16384, (&buf[0]: u1&)) == 3, "16384 → 3 bytes"
    # round-trip 一组值（含最大 u64）
    var vals[6]: u8
    vals[0] = 0
    vals[1] = 1
    vals[2] = 300
    vals[3] = 70000
    vals[4] = 9876543210
    vals[5] = 18446744073709551615
    var i: u4 = 0
    while i < 6
        var n: i4 = codec_varint_encode(vals[i], (&buf[0]: u1&))
        assert n >= 1, "encode produces bytes"
        var got: u8 = 0
        var m: i4 = codec_varint_decode((&buf[0]: u1&), (n: u8), (&got: u8&))
        assert m == n, "decode consumes same byte count"
        assert got == vals[i], "varint round-trip identical"
        i = i + 1
    # u64 最大值占 10 字节
    assert codec_varint_encode(18446744073709551615, (&buf[0]: u1&)) == 10, "u64 max → 10 bytes"
    # 截断输入 → -1
    buf[0] = 200
    assert codec_varint_decode((&buf[0]: u1&), 1, (&vals[0]: u8&)) == -1, "truncated → -1"
    # ZigZag 映射：0,-1,1,-2,2 → 0,1,2,3,4
    assert codec_zigzag_encode(0) == 0, "zigzag 0"
    assert codec_zigzag_encode(-1) == 1, "zigzag -1"
    assert codec_zigzag_encode(1) == 2, "zigzag 1"
    assert codec_zigzag_encode(-2) == 3, "zigzag -2"
    assert codec_zigzag_encode(2) == 4, "zigzag 2"
    # ZigZag round-trip（含负数与极值）
    assert codec_zigzag_decode(codec_zigzag_encode(-123456789)) == -123456789, "zigzag round-trip neg"
    assert codec_zigzag_decode(codec_zigzag_encode(123456789)) == 123456789, "zigzag round-trip pos"

