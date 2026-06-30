# codec —— sc 压缩/编解码原语内置模块
# 唯一事实源：C ABI 契约见同目录 codec.h，默认实现见 codec_impl.c；跨平台经 builtins/platform.h。
#
# 用法：inc codec.sc
#
# 模块内容（按层次/算法簇）：
#   Layer 0 熵编码原子   ：簇1 规范 Huffman · 簇5 ANS(rANS) · 簇6 区间编码
#   Layer 1 通用无损引擎 ：簇2 校验和(CRC-32/Adler-32) · 簇3 RLE · 簇4 DEFLATE/zlib/gzip
#                          · 簇7 LZW（自描述 + 裸子流，裸子流供 TIFF/GIF 图片容器用）
#   工具                 ：簇8 变长整数(LEB128 / ZigZag)
#
# 备选能力（当前未实现，等有具体需求再补）：
#   · 其他 CRC 校验：现只有 CRC-32。若要对接 zstd / ext4 / iSCSI 可加 CRC-32C；要对接 xz 可加 CRC-64。
#   · 流式处理：现在所有函数都是「一次性传入整段数据、返回整段结果」。若要处理超大文件或网络流
#     （边收边解、不全部读进内存），需要再加一套可分块喂数据的接口。
#   · bzip2 类算法（BWT + MTF）：实现量大、更接近一个完整文件格式而非小积木，暂不放进本模块。

# ─────────────────── Layer 0 · 簇 1：熵编码原子（规范 Huffman）───────────────────
# Huffman 是无损压缩的最底层积木：DEFLATE 动态块、JPEG 的 DC/AC 码表都构建于其上。
# 本模块把「频率 → 建树 → 限长码长 → 规范码字」做成独立可复用的原子，并配一体化的
# order-0 字节 Huffman 编解码器（码字布局与 DEFLATE 一致，LSB-first 打包 / MSB-first 码字）。

# 频率 → 限长规范码长（建 Huffman 树 → 叶深度 → limit 限长 → 按频降序赋最短码）。
#   freq[0..n)：各符号频次；limit：最大码长（1..15）；lengths[0..n)：回填码长（0=未出现）。
#   返回实际最大码长（≥1）；无符号返回 0；参数非法 / limit 过小返回 -1。
@fnc codec_huffman_build:: i4, freq: u4&, n: i4, limit: i4, lengths: u1&

# 一体化 order-0 字节 Huffman 编解码：自带建树 + 码长表序列化（原长8 + 码长表128 + 位流）。
# 编码输出上界（用于分配 out 缓冲）。
@fnc codec_huffman_bound:: u8, len: u8
# 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1（len==0 仅写 8 字节头）。
@fnc codec_huffman_encode:: i8, src: &, len: u8, out: u1&, cap: u8
# 解码 src[0..len) -> out[0..cap)；返回原始字节数，失败 / cap 不足返回 -1。
@fnc codec_huffman_decode:: i8, src: &, len: u8, out: u1&, cap: u8

# ─────────────────── Layer 0 · 簇 5：ANS 熵编码（静态 rANS）───────────────────
# rANS（range Asymmetric Numeral Systems，Jarek Duda）：与 Huffman 并列的熵编码原子。
# 区别于 Huffman 的整数比特，rANS 以单个状态寄存器逼近【分数比特】，压缩率更接近信息熵；
# 内核仅几十行（状态 renorm + 频率归一查表），自包含、可 round-trip 验证，零外链。
# 本簇提供：频率归一原子（freq→2^tablelog 的归一频次）+ 一体化 order-0 字节 rANS 编解码。
# 注：rANS 无标准线格式，仅本模块自洽（与 Huffman 的 order-0 codec 同为「自描述容器」）。

# 频率归一：把原始频次缩放到总和恰为 2^tablelog（每个出现的符号 ≥1）。
#   freq[0..n)：原始频次；tablelog：表对数（8..14，TOTAL=1<<tablelog）；norm[0..n)：回填归一频次。
#   返回 tablelog；无符号返回 0；distinct>TOTAL 或参数非法返回 -1。
@fnc codec_rans_normalize:: i4, freq: u4&, n: i4, tablelog: i4, norm: u2&

# 一体化 order-0 字节 rANS 编解码（自带频率统计 + 归一表序列化）。
# 编码输出上界（用于分配 out 缓冲）。
@fnc codec_rans_bound:: u8, len: u8
# 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1（len==0 仅写 8 字节头）。
@fnc codec_rans_encode:: i8, src: &, len: u8, out: u1&, cap: u8
# 解码 src[0..len) -> out[0..cap)；返回原始字节数，失败 / cap 不足返回 -1。
@fnc codec_rans_decode:: i8, src: &, len: u8, out: u1&, cap: u8

# ─────────────────── Layer 0 · 簇 6：区间编码（算术编码的字节实现）───────────────────
# 区间编码（range coding）是算术编码的字节流式变体，熵编码家族第三个原子：
#   Huffman=整数比特 · rANS=分数比特(逆序状态) · 区间编码=分数比特(正序区间细分)。
# 采用 Subbotin 无进位(carryless)编码器：以 [low, low+range) 区间逐符号细分逼近熵，
#   单次正序扫描即可编/解，内核仅几十行。频率归一复用簇5 的 codec_rans_normalize。
# 自描述布局与 rANS 一致：原长8 + tablelog(1) + 256×归一频次u2(512) + 区间码负载；仅本模块自洽。

# 编码输出上界（用于分配 out 缓冲）。
@fnc codec_range_bound:: u8, len: u8
# 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1（len==0 仅写 8 字节头）。
@fnc codec_range_encode:: i8, src: &, len: u8, out: u1&, cap: u8
# 解码 src[0..len) -> out[0..cap)；返回原始字节数，失败 / cap 不足返回 -1。
@fnc codec_range_decode:: i8, src: &, len: u8, out: u1&, cap: u8

# ─────────────────── Layer 1 · 簇 2：校验和（Checksum）───────────────────
# 无损压缩容器（zlib/gzip/png）的完整性校验基石，也独立可用。

# CRC-32（IEEE 802.3，反射多项式 0xEDB88320）——与 zlib crc32() 语义一致。
#   一次性：codec_crc32(data, len)。
@fnc codec_crc32:: u4, data: &, len: u8
# 流式：初值传 0，链式累进 data[0..len)；用于分块/流场景（gzip CRC、png 分块）。
@fnc codec_crc32_update:: u4, crc: u4, data: &, len: u8

# Adler-32（RFC 1950）——zlib 流尾校验。
#   一次性：codec_adler32(data, len)。
@fnc codec_adler32:: u4, data: &, len: u8
# 流式：初值传 1，链式累进。
@fnc codec_adler32_update:: u4, adler: u4, data: &, len: u8

# ─────────────────── Layer 1 · 簇 3：RLE 行程编码 ───────────────────
# PackBits（TIFF/TGA/PSD 标准 RLE）：控制字节 n（有符号）——
#   0..127   → 紧随的 n+1 个字面字节
#   -1..-127 → 紧随 1 个字节重复 1-n 次
#   -128     → 空操作
# 简单、自描述、广泛用于图像容器，是 DEFLATE 之外最轻量的无损原语。

# 编码最坏输出上界（每 128 字节字面块多 1 控制字节）：len + len/128 + 1。
@fnc codec_rle_bound:: u8, len: u8
# 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1。
@fnc codec_rle_encode:: i8, src: &, len: u8, out: u1&, cap: u8
# 解码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足 / 数据截断返回 -1。
@fnc codec_rle_decode:: i8, src: &, len: u8, out: u1&, cap: u8

# ─────────────────── Layer 1 · 簇 4：DEFLATE / zlib / gzip ───────────────────
# DEFLATE（RFC 1951）：三种块（stored / 固定 Huffman / 动态 Huffman）+ LZ77 回溯。
# zlib（RFC 1950）/ gzip（RFC 1952）为其封装容器。out 须为预分配缓冲。

# raw DEFLATE 解码（无封装头/尾）。返回输出字节数；失败 / cap 不足返回 -1。
@fnc codec_inflate:: i8, src: &, len: u8, out: u1&, cap: u8
# zlib 解封装（RFC 1950：2 字节头 + deflate + Adler-32 尾，并校验 Adler-32）。
@fnc codec_zlib_decode:: i8, src: &, len: u8, out: u1&, cap: u8
# gzip 解封装（RFC 1952：魔数头 + deflate + CRC-32 + ISIZE 尾，并校验 CRC-32/ISIZE）。
@fnc codec_gzip_decode:: i8, src: &, len: u8, out: u1&, cap: u8

# 编码侧。level：0=stored 仅封装（最快、保证不失败），1=固定 Huffman + LZ77，
#   >=2=动态 Huffman + LZ77（按频率建树，压缩率更高；容量不足自动回退固定块）。
# 输出可被本模块 inflate 及任何标准 zlib/gzip 解码器还原。
# 编码最坏输出上界（incompressible 时略大于原文）。
@fnc codec_deflate_bound:: u8, len: u8
# raw DEFLATE 编码。返回输出字节数；cap 不足返回 -1。
@fnc codec_deflate:: i8, src: &, len: u8, out: u1&, cap: u8, level: i4
# zlib 封装（头 + deflate + Adler-32 尾）。返回输出字节数；cap 不足返回 -1。
@fnc codec_zlib_encode:: i8, src: &, len: u8, out: u1&, cap: u8, level: i4
# gzip 封装（头 + deflate + CRC-32 + ISIZE 尾）。返回输出字节数；cap 不足返回 -1。
@fnc codec_gzip_encode:: i8, src: &, len: u8, out: u1&, cap: u8, level: i4

# ─────────────────── Layer 1 · 簇 7：LZW 字典编码 ───────────────────
# 变长码 LZW（GIF / Unix compress 同族），与 DEFLATE 并列的字典编码原子：
#   码宽 9→12 位随字典自增长，满 4096 项发 CLEAR(256) 复位重学，EOI(257) 收尾；位流 LSB-first。
# 自描述布局：原长(8,LE) + LZW 位流；编/解码逐码同步，仅本模块自洽。

# 编码输出上界（用于分配 out 缓冲）。
@fnc codec_lzw_bound:: u8, len: u8
# 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1（len==0 仅写 8 字节头）。
@fnc codec_lzw_encode:: i8, src: &, len: u8, out: u1&, cap: u8
# 解码 src[0..len) -> out[0..cap)；返回原始字节数，失败 / cap 不足返回 -1。
@fnc codec_lzw_decode:: i8, src: &, len: u8, out: u1&, cap: u8

# 裸 LZW 子流（无长度头，供 TIFF/GIF 等图片容器用，读到 EOI 或输入耗尽即停）。
# flags 位 0：1 = MSB-first（TIFF 习惯，已对真实 libtiff 双向验证），0 = LSB-first。
@fnc codec_lzw_raw_encode:: i8, src: &, len: u8, out: u1&, cap: u8, flags: i4
@fnc codec_lzw_raw_decode:: i8, src: &, len: u8, out: u1&, cap: u8, flags: i4

# ─────────────────── 工具 · 簇 8：变长整数（LEB128 / ZigZag）───────────────────
# 无符号 LEB128：每字节低 7 位载荷 + 高位续接标志，小端序；u64 至多 10 字节。
# ZigZag：有符号 ↔ 无符号折叠（小绝对值得小编码），与 LEB128 配合编码有符号量。

# 编码 value 到 out（须 ≥10 字节余量）；返回写入字节数（1..10）。
@fnc codec_varint_encode:: i4, value: u8, out: u1&
# 从 src[0..len) 解一个变长整数到 *value；返回消耗字节数（1..10），截断/溢出返回 -1。
@fnc codec_varint_decode:: i4, src: &, len: u8, value: u8&
# ZigZag 编码：有符号 -> 无符号。
@fnc codec_zigzag_encode:: u8, v: i8
# ZigZag 解码：无符号 -> 有符号。
@fnc codec_zigzag_decode:: i8, v: u8
