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
int32_t  codec_huffman_build(uint32_t *freq, int32_t n, int32_t limit, uint8_t *lengths);

/* 一体化 order-0 字节 Huffman 编解码（自带建树 + 码长表序列化）。 */
uint64_t codec_huffman_bound(uint64_t len);                                       /* 输出上界 */
int64_t  codec_huffman_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  codec_huffman_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 0 · 簇 5：ANS 熵编码（静态 rANS）====================== */

/* 频率归一：把原始频次缩放到总和恰为 2^tablelog（每个出现的符号 ≥1）。
 * freq[0..n)：原始频次；tablelog：表对数（8..14）；norm[0..n)：回填归一频次。
 * 返回 tablelog；无任何符号返回 0；distinct>TOTAL 或参数非法返回 -1。 */
int32_t  codec_rans_normalize(uint32_t *freq, int32_t n, int32_t tablelog, uint16_t *norm);

/* 一体化 order-0 字节 rANS 编解码（自带频率统计 + 归一表序列化）。 */
uint64_t codec_rans_bound(uint64_t len);                                       /* 输出上界 */
int64_t  codec_rans_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  codec_rans_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 0 · 簇 6：区间编码（算术编码的字节实现）====================== */

/* Subbotin 无进位区间编码器的 order-0 字节编解码（频率归一复用 codec_rans_normalize）。 */
uint64_t codec_range_bound(uint64_t len);                                       /* 输出上界 */
int64_t  codec_range_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  codec_range_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 1 · 簇 2：校验和 ====================== */

/* CRC-32（IEEE 802.3，反射多项式 0xEDB88320）：zlib crc32() 兼容语义。 */
uint32_t codec_crc32(void *data, uint64_t len);                        /* 一次性 */
uint32_t codec_crc32_update(uint32_t crc, void *data, uint64_t len);   /* 流式，初值 0 */

/* Adler-32（RFC 1950）：zlib 流尾校验。 */
uint32_t codec_adler32(void *data, uint64_t len);                        /* 一次性 */
uint32_t codec_adler32_update(uint32_t adler, void *data, uint64_t len); /* 流式，初值 1 */

/* ====================== Layer 1 · 簇 3：RLE（PackBits）====================== */

/* 编码最坏输出上界：len + len/128 + 1。 */
uint64_t codec_rle_bound(uint64_t len);
/* PackBits 编码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足返回 -1。 */
int64_t  codec_rle_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* PackBits 解码 src[0..len) -> out[0..cap)；返回写入字节数，cap 不足 / 截断返回 -1。 */
int64_t  codec_rle_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* ====================== Layer 1 · 簇 4：DEFLATE / zlib / gzip ====================== */

/* raw DEFLATE 解码（RFC 1951，无封装）。返回输出字节数；失败 / cap 不足返回 -1。 */
int64_t codec_inflate(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* zlib 解封装（RFC 1950）并校验尾部 Adler-32。返回输出字节数；失败返回 -1。 */
int64_t codec_zlib_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* gzip 解封装（RFC 1952）并校验尾部 CRC-32 / ISIZE。返回输出字节数；失败返回 -1。 */
int64_t codec_gzip_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);

/* 编码侧。level：0=stored 仅封装（不压缩、保证不失败），>=1=固定 Huffman + LZ77。 */
uint64_t codec_deflate_bound(uint64_t len);
int64_t  codec_deflate(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level);
int64_t  codec_zlib_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level);
int64_t  codec_gzip_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level);

/* —— Layer 1 · 簇 7：LZW 字典编码（变长码 9..12 位，自描述）—— */
uint64_t codec_lzw_bound(uint64_t len);
int64_t  codec_lzw_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
int64_t  codec_lzw_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap);
/* 裸 LZW 子流（无长度头，供 TIFF/GIF 等图片容器用）。flags 位 0：1=MSB-first(TIFF)，0=LSB-first */
int64_t  codec_lzw_raw_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t flags);
int64_t  codec_lzw_raw_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t flags);

/* —— 工具 · 簇 8：变长整数（无符号 LEB128 + ZigZag）—— */
int32_t  codec_varint_encode(uint64_t value, uint8_t *out);            /* 返回字节数 1..10 */
int32_t  codec_varint_decode(void *src, uint64_t len, uint64_t *value);/* 返回消耗字节数；失败 -1 */
uint64_t codec_zigzag_encode(int64_t v);
int64_t  codec_zigzag_decode(uint64_t v);

#endif /* SC_CODEC_H */
