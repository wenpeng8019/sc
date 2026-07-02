/* codec.h —— sc codec 内置模块的 C ABI 契约
 *
 * 唯一事实源：与同目录 codec.sc 的 @fnc 声明逐一对应（类型必须精确一致，
 *   因 scc 既经本头 #include 进类型，又按 codec.sc 生成等价 extern 原型，
 *   两者须为同一 C 类型，故长度统一 uint64_t、缓冲统一 uint8_t*）。
 * 实现：同目录 codec_impl.c。
 */
#ifndef SC_CODEC_H
#define SC_CODEC_H

#include <stdint.h>

/* ====================== Layer 0 · 簇 1：熵编码原子（规范 Huffman）====================== */

/* 频率 → 限长规范码长（建 Huffman 树 → 叶深度 → limit 限长 → 按频赋长）。
 * freq[0..n)：各符号频次；limit：最大码长（1..15）；lengths[0..n)：回填码长（0=未出现）。
 * 返回实际最大码长（≥1）；无任何符号返回 0；参数非法 / limit 过小返回 -1。 */
int32_t  sc_codec_huffman_build(uint32_t *freq, int32_t n, int32_t limit, uint8_t *lengths);

/* 一体化 order-0 字节 Huffman 编解码（自带建树 + 码长表序列化）。 */
uint64_t sc_codec_huffman_bound(uint64_t len);                                       /* 输出上界 */
int64_t  sc_codec_huffman_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  sc_codec_huffman_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 0 · 簇 5：ANS 熵编码（静态 rANS）====================== */

/* 频率归一：把原始频次缩放到总和恰为 2^tablelog（每个出现的符号 ≥1）。
 * freq[0..n)：原始频次；tablelog：表对数（8..14）；norm[0..n)：回填归一频次。
 * 返回 tablelog；无任何符号返回 0；distinct>TOTAL 或参数非法返回 -1。 */
int32_t  sc_codec_rans_normalize(uint32_t *freq, int32_t n, int32_t tablelog, uint16_t *norm);

/* 一体化 order-0 字节 rANS 编解码（自带频率统计 + 归一表序列化）。 */
uint64_t sc_codec_rans_bound(uint64_t len);                                       /* 输出上界 */
int64_t  sc_codec_rans_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  sc_codec_rans_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 0 · 簇 6：区间编码（算术编码的字节实现）====================== */

/* Subbotin 无进位区间编码器的 order-0 字节编解码（频率归一复用 sc_codec_rans_normalize）。 */
uint64_t sc_codec_range_bound(uint64_t len);                                       /* 输出上界 */
int64_t  sc_codec_range_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  sc_codec_range_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 1 · 簇 2：校验和 ====================== */

/* CRC-32（IEEE 802.3，反射多项式 0xEDB88320）：zlib crc32() 兼容语义。 */
uint32_t sc_codec_crc32(void *data, uint64_t len);                        /* 一次性 */
uint32_t sc_codec_crc32_update(uint32_t crc, void *data, uint64_t len);   /* 流式，初值 0 */

/* Adler-32（RFC 1950）：zlib 流尾校验。 */
uint32_t sc_codec_adler32(void *data, uint64_t len);                        /* 一次性 */
uint32_t sc_codec_adler32_update(uint32_t adler, void *data, uint64_t len); /* 流式，初值 1 */

/* ====================== Layer 1 · 簇 3：RLE（PackBits）====================== */

/* 编码最坏输出上界：len + len/128 + 1。 */
uint64_t sc_codec_rle_bound(uint64_t len);
/* PackBits 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1。 */
int64_t  sc_codec_rle_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* PackBits 解码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足 / 截断返回 -1。 */
int64_t  sc_codec_rle_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ---- 流式 RLE（unit 粒度 + feed/flush，TGA/TrueVision 兼容线格式）----
 * 状态结构由调用方持有；与 codec.sc 的 @def 逐字段一致（scc 经本头 #include 取得类型）。
 * 原始包：控制字节 bit7=0，紧随 (ctrl&0x7F)+1 个字面 unit；
 * 行程包：控制字节 bit7=1，紧随 1 个 unit 重复 (ctrl&0x7F)+1 次。unit=每 unit 字节数(1..8)。 */
typedef struct sc_codec_rle_enc {
    int32_t unit;        /* 每 unit 字节数（1..8） */
    int32_t st;          /* 0=INIT / 1=LIT / 2=RUN */
    int32_t n;           /* LIT：字面 unit 数；RUN：行程计数 */
    uint8_t buf[1024];   /* LIT 字面缓冲（≤128 unit × ≤8 字节） */
    uint8_t rv[8];       /* RUN 重复值 */
} sc_codec_rle_enc;

typedef struct sc_codec_rle_dec {
    int32_t unit;        /* 每 unit 字节数（1..8） */
    int32_t phase;       /* 0=待控制 / 1=原始载荷 / 2=行程载荷 */
    int32_t rem;         /* 原始：剩余字面字节数；行程：剩余重复次数 */
    int32_t rvn;         /* 行程：已收集重复值字节数 */
    uint8_t rv[8];       /* 行程重复值 */
} sc_codec_rle_dec;

/* 流式 RLE 接口（实现于 codec.sc，纯 sc 编译进 codec 模块单元；本头供消费方取得原型）。 */
void     sc_codec_rle_enc_init(sc_codec_rle_enc *e, int32_t unit);
uint64_t sc_codec_rle_enc_bound(uint64_t nunits, int32_t unit);
int64_t  sc_codec_rle_enc_feed(sc_codec_rle_enc *e, uint8_t *in, uint64_t nunits, uint8_t *out, uint64_t cap);
int64_t  sc_codec_rle_enc_flush(sc_codec_rle_enc *e, uint8_t *out, uint64_t cap);
void     sc_codec_rle_dec_init(sc_codec_rle_dec *d, int32_t unit);
int64_t  sc_codec_rle_dec_feed(sc_codec_rle_dec *d, uint8_t *in, uint64_t inlen, uint8_t *out, uint64_t cap);

/* ====================== Layer 1 · 簇 4：DEFLATE / zlib / gzip ====================== */

/* raw DEFLATE 解码（RFC 1951，无封装）。返回输出字节数；失败 / cap 不足返回 -1。 */
int64_t sc_codec_inflate(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* zlib 解封装（RFC 1950）并校验尾部 Adler-32。返回输出字节数；失败返回 -1。 */
int64_t sc_codec_zlib_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* gzip 解封装（RFC 1952）并校验尾部 CRC-32 / ISIZE。返回输出字节数；失败返回 -1。 */
int64_t sc_codec_gzip_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* —— 流式 inflate（簇 4b）：状态结构对调用方不透明，经 size() 取字节数自行分配、当 & 传入。 —— */
/* 返回状态结构字节数（供分配）。 */
uint64_t sc_codec_zdec_size(void);
/* 初始化解码器。wrap：0=raw DEFLATE / 1=zlib / 2=gzip(待补)。返回 0。 */
int32_t  sc_codec_zdec_init(void *sp, int32_t wrap);
/* 喂 inlen 字节压缩流（全部吸入内部缓冲，*consumed 恒为 inlen），尽量解出到 out[0..cap)。
 * 返回本次产出字节数（>=0）；出错 -1。可反复调用（out 满则抽走后再喂 inlen=0 续解）。 */
int64_t  sc_codec_zdec_feed(void *sp, void *in, uint64_t inlen, uint64_t *consumed, uint8_t *out, uint64_t cap);
/* 流是否已完整结束（末块 + 尾校验通过）。 */
int32_t  sc_codec_zdec_ended(void *sp);
/* 释放内部输入缓冲（用完须调；不释放状态结构本身）。 */
void     sc_codec_zdec_free(void *sp);

/* —— 流式 deflate（簇 4b）：分块喂输入 / 分块取输出（边 filter 边写编码）。 —— */
/* 返回状态结构字节数（供分配）。 */
uint64_t sc_codec_zenc_size(void);
/* 初始化编码器。wrap：0=raw / 1=zlib / 2=gzip；level 预留（当前恒用动态 Huffman）。返回 0。 */
int32_t  sc_codec_zenc_init(void *sp, int32_t wrap, int32_t level);
/* 喂 inlen 字节原文（全部吸入，*consumed 恒为 inlen），产出压缩流到 out[0..cap)，返回产出字节数。
 * out 未抽完时以 inlen=0 反复调用续抽。 */
int64_t  sc_codec_zenc_feed(void *sp, void *in, uint64_t inlen, uint64_t *consumed, uint8_t *out, uint64_t cap);
/* 收尾：发末块 + wrap 尾，产出到 out[0..cap)，返回产出字节数。反复调用直至 sc_codec_zenc_ended。 */
int64_t  sc_codec_zenc_finish(void *sp, uint8_t *out, uint64_t cap);
/* 是否已收尾且产出抽空。 */
int32_t  sc_codec_zenc_ended(void *sp);
/* 释放内部缓冲（用完须调；不释放状态结构本身）。 */
void     sc_codec_zenc_free(void *sp);

/* 编码侧。level：0=stored 仅封装（不压缩、保证不失败），>=1=固定 Huffman + LZ77。 */
uint64_t sc_codec_deflate_bound(uint64_t len);
int64_t  sc_codec_deflate(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level);
int64_t  sc_codec_zlib_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level);
int64_t  sc_codec_gzip_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level);

/* —— Layer 1 · 簇 7：LZW 字典编码（变长码 9..12 位，自描述）—— */
uint64_t sc_codec_lzw_bound(uint64_t len);
int64_t  sc_codec_lzw_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  sc_codec_lzw_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* 裸 LZW 子流（无长度头，供 TIFF/GIF 等图片容器用）。flags 位 0：1=MSB-first(TIFF)，0=LSB-first */
int64_t  sc_codec_lzw_raw_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t flags);
int64_t  sc_codec_lzw_raw_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t flags);

/* —— 工具 · 簇 8：变长整数（无符号 LEB128 + ZigZag）—— */
int32_t  sc_codec_varint_encode(uint64_t value, uint8_t *out);            /* 返回字节数 1..10 */
int32_t  sc_codec_varint_decode(void *src, uint64_t len, uint64_t *value);/* 返回消耗字节数；失败 -1 */
uint64_t sc_codec_zigzag_encode(int64_t v);
int64_t  sc_codec_zigzag_decode(uint64_t v);

#endif /* SC_CODEC_H */
