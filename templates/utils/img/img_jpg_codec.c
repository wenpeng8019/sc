/* img_jpg_codec.c —— JPEG 熵扫描流式编解码（sc↔C 胶水实现）。
 *
 * 由 img_jpg_codec.sc 的 `add img_jpg_codec.c` 现场编译并链接（相对本文件目录解析）。
 * 契约与 @fnc 声明逐字对应：& -> void* / u1& -> uint8_t* / i2& -> int16_t* /
 *   i4 -> int32_t / i4& -> int32_t* / u8 -> uint64_t / i8 -> int64_t。
 *
 * 定位：JPEG 专属熵层（非通用 codec）。只做位 I/O + Huffman + 逐块熵解码/编码 + 重启标记；
 *   MCU 几何 / IDCT / FDCT / 量化 / 上采样 / 颜色变换全归 img_jpg。原从 builtins/codec 迁出。
 * 参考 stb_image.h / stb_image_write.h 的 JPEG 熵层，改造为流式 feed/drain + 块级回滚。
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ====================== 簇 9：JPEG 熵扫描流式编解码 ======================
 *
 * 参考 stb_image.h / stb_image_write.h 的 JPEG 熵层。codec 只做位 I/O + Huffman +
 * 逐块熵解码/编码 + 重启标记；MCU 几何 / IDCT / FDCT / 量化 / 上采样 / 颜色变换全归 img_jpg。
 *
 * 解码流式：逐 block「块级 checkpoint / 回滚」。cj_grow 从内部 inbuf 取字节，字节不足且
 *   未见 marker → need_more 挂起；block 入口存位态快照（code_buffer/code_bits/bytepos/
 *   marker/nomore/dc_pred），中途触 need_more 即回滚返回 0，img 喂更多字节后重解整块。
 *   baseline 块为「只写」故回滚安全；progressive 精修块含读改写，故 img 必须一次喂满整个
 *   scan（need_more 不触发）。codec 只产【自然序原始未去量化系数 int16[64]】。
 * 编码流式：同 zenc 的 feed/drain。img 侧 FDCT+量化+zigzag 出 DU[64]（zigzag 序、DU[0]=DC），
 *   codec 熵编码（DC 差分 + AC 游程 Huffman + 位写 0xFF 塞）产出扫描字节，drain 抽走。 */

#define CJ_FAST_BITS 9
#define CJ_MARKER_NONE 0xff

/* (1<<n)-1 */
static const uint32_t cj_bmask[17] = {0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};
/* bias[n] = (-1<<n)+1 */
static const int32_t cj_jbias[16] = {0,-1,-3,-7,-15,-31,-63,-127,-255,-511,-1023,-2047,-4095,-8191,-16383,-32767};
/* zigzag 位置 → 8x8 行主序位置（末尾 15 个 63 容错越界） */
static const uint8_t cj_dezigzag[64+15] = {
    0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
   12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
   35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
   58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63,
   63,63,63,63,63,63,63,63, 63,63,63,63,63,63,63 };

#define cj_lrot(x,y) (((x) << (y)) | ((x) >> (-(y) & 31)))

typedef struct cj_huff {
    uint8_t  fast[1 << CJ_FAST_BITS];
    uint16_t code[256];
    uint8_t  values[256];
    uint8_t  size[257];
    uint32_t maxcode[18];
    int32_t  delta[17];
} cj_huff;

typedef struct codec_jdec {
    cj_huff huff_dc[4];
    cj_huff huff_ac[4];
    int16_t fast_ac[4][1 << CJ_FAST_BITS];
    /* 位读态 */
    uint32_t code_buffer;
    int32_t  code_bits;
    uint8_t  marker;     /* 遇到的 marker XX（CJ_MARKER_NONE=无） */
    int32_t  nomore;     /* 已见 marker：后续 grow 填 0 */
    int32_t  need_more;  /* 喂入字节不足（可回滚） */
    int32_t  eob_run;    /* progressive AC 的 end-of-band 游程 */
    /* 内部输入缓冲 */
    uint8_t *inbuf;
    uint64_t incap, inlen, bytepos;
} codec_jdec;

uint64_t codec_jdec_size(void) { return sizeof(codec_jdec); }

int32_t codec_jdec_init(void *sp) {
    codec_jdec *j = (codec_jdec *)sp;
    memset(j, 0, sizeof(*j));
    j->marker = CJ_MARKER_NONE;
    return 0;
}

void codec_jdec_free(void *sp) {
    codec_jdec *j = (codec_jdec *)sp;
    if (j->inbuf) { free(j->inbuf); j->inbuf = 0; j->incap = 0; j->inlen = 0; j->bytepos = 0; }
}

/* 建 JPEG Huffman 表（JPEG 规范：size 表 = 各码长符号数展开；code 递增；fast 加速表）。 */
static int cj_build_huffman(cj_huff *h, const int *count) {
    int i, j, k = 0;
    unsigned int code;
    for (i = 0; i < 16; ++i)
        for (j = 0; j < count[i]; ++j) {
            h->size[k++] = (uint8_t)(i + 1);
            if (k >= 257) return -1;
        }
    h->size[k] = 0;
    code = 0; k = 0;
    for (j = 1; j <= 16; ++j) {
        h->delta[j] = k - (int)code;
        if (h->size[k] == j) {
            while (h->size[k] == j) h->code[k++] = (uint16_t)(code++);
            if (code - 1 >= (1u << j)) return -1;
        }
        h->maxcode[j] = code << (16 - j);
        code <<= 1;
    }
    h->maxcode[17] = 0xffffffff;
    memset(h->fast, 255, 1 << CJ_FAST_BITS);
    for (i = 0; i < k; ++i) {
        int s = h->size[i];
        if (s <= CJ_FAST_BITS) {
            int c = h->code[i] << (CJ_FAST_BITS - s);
            int m = 1 << (CJ_FAST_BITS - s);
            for (j = 0; j < m; ++j) h->fast[c + j] = (uint8_t)i;
        }
    }
    return 0;
}

/* AC 小值一次解出加速表（run/mag 合并进 fast_ac）。 */
static void cj_build_fast_ac(int16_t *fast_ac, cj_huff *h) {
    int i;
    for (i = 0; i < (1 << CJ_FAST_BITS); ++i) {
        uint8_t fast = h->fast[i];
        fast_ac[i] = 0;
        if (fast < 255) {
            int rs = h->values[fast];
            int run = (rs >> 4) & 15;
            int magbits = rs & 15;
            int len = h->size[fast];
            if (magbits && len + magbits <= CJ_FAST_BITS) {
                int k = ((i << len) & ((1 << CJ_FAST_BITS) - 1)) >> (CJ_FAST_BITS - magbits);
                int m = 1 << (magbits - 1);
                if (k < m) k += (~0u << magbits) + 1;
                if (k >= -128 && k <= 127)
                    fast_ac[i] = (int16_t)((k * 256) + (run * 16) + (len + magbits));
            }
        }
    }
}

/* img 交表：tc=0 DC / 1 AC；th 0..3；counts[16] 各码长符号数；values 符号表。返回 0/-1。 */
int32_t codec_jdec_dht(void *sp, int32_t tc, int32_t th, uint8_t *counts, uint8_t *values) {
    codec_jdec *j = (codec_jdec *)sp;
    cj_huff *h;
    int cc[16], i, n = 0;
    if (tc < 0 || tc > 1 || th < 0 || th > 3) return -1;
    for (i = 0; i < 16; ++i) { cc[i] = counts[i]; n += cc[i]; }
    if (n > 256) return -1;
    h = tc == 0 ? &j->huff_dc[th] : &j->huff_ac[th];
    if (cj_build_huffman(h, cc) < 0) return -1;
    for (i = 0; i < n; ++i) h->values[i] = values[i];
    if (tc == 1) cj_build_fast_ac(j->fast_ac[th], h);
    return 0;
}

/* 喂熵字节：append 进 inbuf，先压实已提交 [0,bytepos)。返回吸入字节数（恒 inlen）。 */
int64_t codec_jdec_feed(void *sp, void *in, uint64_t inlen) {
    codec_jdec *j = (codec_jdec *)sp;
    /* 压实：丢弃已消费前缀（block 间 bytepos 恒为已提交位，回滚不越界） */
    if (j->bytepos > 0) {
        uint64_t rem = j->inlen - j->bytepos;
        if (rem) memmove(j->inbuf, j->inbuf + j->bytepos, rem);
        j->inlen = rem; j->bytepos = 0;
    }
    if (inlen) {
        if (j->inlen + inlen > j->incap) {
            uint64_t nc = j->incap ? j->incap : 4096;
            while (nc < j->inlen + inlen) nc *= 2;
            uint8_t *nb = (uint8_t *)realloc(j->inbuf, nc);
            if (!nb) return -1;
            j->inbuf = nb; j->incap = nc;
        }
        memcpy(j->inbuf + j->inlen, in, inlen);
        j->inlen += inlen;
    }
    return (int64_t)inlen;
}

/* 从 inbuf 拉字节进 code_buffer（0xFF 塞字节 destuff + marker 检测）。 */
static void cj_grow(codec_jdec *j) {
    do {
        unsigned int b;
        if (j->nomore) b = 0;
        else {
            if (j->bytepos >= j->inlen) { j->need_more = 1; return; }
            b = j->inbuf[j->bytepos++];
            if (b == 0xff) {
                int c;
                if (j->bytepos >= j->inlen) { j->need_more = 1; return; }
                c = j->inbuf[j->bytepos++];
                while (c == 0xff) {
                    if (j->bytepos >= j->inlen) { j->need_more = 1; return; }
                    c = j->inbuf[j->bytepos++];
                }
                if (c != 0) { j->marker = (uint8_t)c; j->nomore = 1; return; }
                /* 0xff00：塞字节，b 保持 0xff */
            }
        }
        j->code_buffer |= b << (24 - j->code_bits);
        j->code_bits += 8;
    } while (j->code_bits <= 24);
}

/* 解一个 JPEG Huffman 值。need_more 由调用方在返回后检查。 */
static int cj_huff_decode(codec_jdec *j, cj_huff *h) {
    unsigned int temp;
    int c, k;
    if (j->code_bits < 16) { cj_grow(j); if (j->need_more) return -1; }
    c = (j->code_buffer >> (32 - CJ_FAST_BITS)) & ((1 << CJ_FAST_BITS) - 1);
    k = h->fast[c];
    if (k < 255) {
        int s = h->size[k];
        if (s > j->code_bits) return -1;
        j->code_buffer <<= s; j->code_bits -= s;
        return h->values[k];
    }
    temp = j->code_buffer >> 16;
    for (k = CJ_FAST_BITS + 1; ; ++k)
        if (temp < h->maxcode[k]) break;
    if (k == 17) { j->code_bits -= 16; return -1; }
    if (k > j->code_bits) return -1;
    c = ((j->code_buffer >> (32 - k)) & cj_bmask[k]) + h->delta[k];
    if (c < 0 || c >= 256) return -1;
    j->code_bits -= k; j->code_buffer <<= k;
    return h->values[c];
}

/* 收 n bit 并符号扩展（JPEG receive+extend）。 */
static int cj_extend_receive(codec_jdec *j, int n) {
    unsigned int k;
    int sgn;
    if (n == 0) return 0;
    if (j->code_bits < n) { cj_grow(j); if (j->need_more) return 0; }
    if (j->code_bits < n) return 0;   /* nomore：末尾补 0 */
    sgn = j->code_buffer >> 31;
    k = cj_lrot(j->code_buffer, n);
    j->code_buffer = k & ~cj_bmask[n];
    k &= cj_bmask[n];
    j->code_bits -= n;
    return k + (cj_jbias[n] & (sgn - 1));
}

static int cj_get_bits(codec_jdec *j, int n) {
    unsigned int k;
    if (n == 0) return 0;
    if (j->code_bits < n) { cj_grow(j); if (j->need_more) return 0; }
    if (j->code_bits < n) return 0;
    k = cj_lrot(j->code_buffer, n);
    j->code_buffer = k & ~cj_bmask[n];
    k &= cj_bmask[n];
    j->code_bits -= n;
    return k;
}

static int cj_get_bit(codec_jdec *j) {
    unsigned int k;
    if (j->code_bits < 1) { cj_grow(j); if (j->need_more) return 0; }
    if (j->code_bits < 1) return 0;
    k = j->code_buffer;
    j->code_buffer <<= 1;
    --j->code_bits;
    return k & 0x80000000;
}

/* baseline：解一块到 data[64]（自然序，未去量化）。返回 1 完成 / 0 缺字节(已回滚) / -1 错。 */
int32_t codec_jdec_block(void *sp, int16_t *data, int32_t dc_tbl, int32_t ac_tbl, int32_t *dc_pred) {
    codec_jdec *j = (codec_jdec *)sp;
    cj_huff *hdc = &j->huff_dc[dc_tbl];
    cj_huff *hac = &j->huff_ac[ac_tbl];
    int16_t *fac = j->fast_ac[ac_tbl];
    uint32_t sv_cb = j->code_buffer; int32_t sv_bits = j->code_bits;
    uint64_t sv_pos = j->bytepos; uint8_t sv_mk = j->marker; int32_t sv_nm = j->nomore;
    int32_t sv_dc = *dc_pred;
    int diff, dc, k, t;
    j->need_more = 0;
    if (j->code_bits < 16) { cj_grow(j); if (j->need_more) goto suspend; }
    t = cj_huff_decode(j, hdc);
    if (j->need_more) goto suspend;
    if (t < 0 || t > 15) return -1;
    memset(data, 0, 64 * sizeof(int16_t));
    diff = t ? cj_extend_receive(j, t) : 0;
    if (j->need_more) goto suspend;
    dc = sv_dc + diff; *dc_pred = dc;
    data[0] = (int16_t)dc;
    k = 1;
    do {
        unsigned int zig;
        int c, r, s;
        if (j->code_bits < 16) { cj_grow(j); if (j->need_more) goto suspend; }
        c = (j->code_buffer >> (32 - CJ_FAST_BITS)) & ((1 << CJ_FAST_BITS) - 1);
        r = fac[c];
        if (r) {
            k += (r >> 4) & 15;
            s = r & 15;
            if (s > j->code_bits) return -1;
            j->code_buffer <<= s; j->code_bits -= s;
            zig = cj_dezigzag[k++];
            data[zig] = (int16_t)(r >> 8);
        } else {
            int rs = cj_huff_decode(j, hac);
            if (j->need_more) goto suspend;
            if (rs < 0) return -1;
            s = rs & 15; r = rs >> 4;
            if (s == 0) {
                if (rs != 0xf0) break;
                k += 16;
            } else {
                int val;
                k += r;
                zig = cj_dezigzag[k++];
                val = cj_extend_receive(j, s);
                if (j->need_more) goto suspend;
                data[zig] = (int16_t)val;
            }
        }
    } while (k < 64);
    return 1;
suspend:
    j->code_buffer = sv_cb; j->code_bits = sv_bits; j->bytepos = sv_pos;
    j->marker = sv_mk; j->nomore = sv_nm; *dc_pred = sv_dc; j->need_more = 0;
    return 0;
}

/* progressive DC 块（ah=succ_high, al=succ_low）。首扫描 ah==0 写 dc<<al；精修 ah!=0 读 1bit 加权。 */
int32_t codec_jdec_block_prog_dc(void *sp, int16_t *data, int32_t dc_tbl, int32_t *dc_pred, int32_t ah, int32_t al) {
    codec_jdec *j = (codec_jdec *)sp;
    cj_huff *hdc = &j->huff_dc[dc_tbl];
    uint32_t sv_cb = j->code_buffer; int32_t sv_bits = j->code_bits;
    uint64_t sv_pos = j->bytepos; uint8_t sv_mk = j->marker; int32_t sv_nm = j->nomore;
    int32_t sv_dc = *dc_pred;
    j->need_more = 0;
    if (j->code_bits < 16) { cj_grow(j); if (j->need_more) goto suspend; }
    if (ah == 0) {
        int t, diff, dc;
        memset(data, 0, 64 * sizeof(int16_t));
        t = cj_huff_decode(j, hdc);
        if (j->need_more) goto suspend;
        if (t < 0 || t > 15) return -1;
        diff = t ? cj_extend_receive(j, t) : 0;
        if (j->need_more) goto suspend;
        dc = sv_dc + diff; *dc_pred = dc;
        data[0] = (int16_t)(dc * (1 << al));
    } else {
        int b = cj_get_bit(j);
        if (j->need_more) goto suspend;
        if (b) data[0] += (int16_t)(1 << al);
    }
    return 1;
suspend:
    j->code_buffer = sv_cb; j->code_bits = sv_bits; j->bytepos = sv_pos;
    j->marker = sv_mk; j->nomore = sv_nm; *dc_pred = sv_dc; j->need_more = 0;
    return 0;
}

/* progressive AC 块（ss=spec_start, se=spec_end, ah, al）。内部维护 eob_run。 */
int32_t codec_jdec_block_prog_ac(void *sp, int16_t *data, int32_t ac_tbl, int32_t ss, int32_t se, int32_t ah, int32_t al) {
    codec_jdec *j = (codec_jdec *)sp;
    cj_huff *hac = &j->huff_ac[ac_tbl];
    int16_t *fac = j->fast_ac[ac_tbl];
    uint32_t sv_cb = j->code_buffer; int32_t sv_bits = j->code_bits;
    uint64_t sv_pos = j->bytepos; uint8_t sv_mk = j->marker; int32_t sv_nm = j->nomore;
    int32_t sv_eob = j->eob_run;
    int k;
    j->need_more = 0;
    if (ah == 0) {
        int shift = al;
        if (j->eob_run) { --j->eob_run; return 1; }
        k = ss;
        do {
            unsigned int zig;
            int c, r, s;
            if (j->code_bits < 16) { cj_grow(j); if (j->need_more) goto suspend; }
            c = (j->code_buffer >> (32 - CJ_FAST_BITS)) & ((1 << CJ_FAST_BITS) - 1);
            r = fac[c];
            if (r) {
                k += (r >> 4) & 15;
                s = r & 15;
                if (s > j->code_bits) return -1;
                j->code_buffer <<= s; j->code_bits -= s;
                zig = cj_dezigzag[k++];
                data[zig] = (int16_t)((r >> 8) * (1 << shift));
            } else {
                int rs = cj_huff_decode(j, hac);
                if (j->need_more) goto suspend;
                if (rs < 0) return -1;
                s = rs & 15; r = rs >> 4;
                if (s == 0) {
                    if (r < 15) {
                        j->eob_run = (1 << r);
                        if (r) j->eob_run += cj_get_bits(j, r);
                        if (j->need_more) goto suspend;
                        --j->eob_run;
                        break;
                    }
                    k += 16;
                } else {
                    int val;
                    k += r;
                    zig = cj_dezigzag[k++];
                    val = cj_extend_receive(j, s);
                    if (j->need_more) goto suspend;
                    data[zig] = (int16_t)(val * (1 << shift));
                }
            }
        } while (k <= se);
    } else {
        int16_t bit = (int16_t)(1 << al);
        if (j->eob_run) {
            --j->eob_run;
            for (k = ss; k <= se; ++k) {
                int16_t *p = &data[cj_dezigzag[k]];
                if (*p != 0) {
                    int b = cj_get_bit(j);
                    if (j->need_more) goto suspend;
                    if (b) if ((*p & bit) == 0) { if (*p > 0) *p += bit; else *p -= bit; }
                }
            }
        } else {
            k = ss;
            do {
                int r, s, rs, b;
                rs = cj_huff_decode(j, hac);
                if (j->need_more) goto suspend;
                if (rs < 0) return -1;
                s = rs & 15; r = rs >> 4;
                if (s == 0) {
                    if (r < 15) {
                        j->eob_run = (1 << r) - 1;
                        if (r) j->eob_run += cj_get_bits(j, r);
                        if (j->need_more) goto suspend;
                        r = 64;
                    }
                } else {
                    if (s != 1) return -1;
                    b = cj_get_bit(j);
                    if (j->need_more) goto suspend;
                    if (b) s = bit; else s = -bit;
                }
                while (k <= se) {
                    int16_t *p = &data[cj_dezigzag[k++]];
                    if (*p != 0) {
                        b = cj_get_bit(j);
                        if (j->need_more) goto suspend;
                        if (b) if ((*p & bit) == 0) { if (*p > 0) *p += bit; else *p -= bit; }
                    } else {
                        if (r == 0) { *p = (int16_t)s; break; }
                        --r;
                    }
                }
            } while (k <= se);
        }
    }
    return 1;
suspend:
    /* 注：progressive 精修含读改写；img 须一次喂满整 scan 使此路不触发。回滚仅还原位态。 */
    j->code_buffer = sv_cb; j->code_bits = sv_bits; j->bytepos = sv_pos;
    j->marker = sv_mk; j->nomore = sv_nm; j->eob_run = sv_eob; j->need_more = 0;
    return 0;
}

/* 重启标记复位（RSTn）：清位缓冲 + eob_run；吞掉已被 cj_grow 消费的 marker，续解。 */
void codec_jdec_reset(void *sp) {
    codec_jdec *j = (codec_jdec *)sp;
    j->code_buffer = 0; j->code_bits = 0;
    j->marker = CJ_MARKER_NONE; j->nomore = 0;
    j->eob_run = 0; j->need_more = 0;
}

/* 返回 cj_grow 遇到的 marker XX（CJ_MARKER_NONE=无）。 */
int32_t codec_jdec_marker(void *sp) {
    codec_jdec *j = (codec_jdec *)sp;
    return j->marker;
}

/* 取回未消费的内部缓冲字节（marker 之后的残留）到 out[0..cap)，返回拷贝字节数并推进。 */
int64_t codec_jdec_pending(void *sp, uint8_t *out, uint64_t cap) {
    codec_jdec *j = (codec_jdec *)sp;
    uint64_t avail = j->inlen - j->bytepos, take = avail < cap ? avail : cap;
    if (take) memcpy(out, j->inbuf + j->bytepos, take);
    j->bytepos += take;
    return (int64_t)take;
}

/* 未消费字节数（供 img 分配 pending 缓冲）。 */
uint64_t codec_jdec_pending_len(void *sp) {
    codec_jdec *j = (codec_jdec *)sp;
    return j->inlen - j->bytepos;
}

/* ---- JPEG 熵编码（baseline）：img 出 DU[64]（zigzag 序，量化后，DU[0]=DC），codec 熵编码 ---- */

typedef struct cj_ehuff {
    uint16_t code[256];
    uint8_t  len[256];
} cj_ehuff;

typedef struct codec_jenc {
    cj_ehuff edc[4];
    cj_ehuff eac[4];
    int32_t  bitBuf, bitCnt;
    uint8_t *ob;
    uint64_t obcap, oblen, obpos;
    int32_t  oom;
} codec_jenc;

uint64_t codec_jenc_size(void) { return sizeof(codec_jenc); }

int32_t codec_jenc_init(void *sp) {
    codec_jenc *z = (codec_jenc *)sp;
    memset(z, 0, sizeof(*z));
    return 0;
}

void codec_jenc_free(void *sp) {
    codec_jenc *z = (codec_jenc *)sp;
    if (z->ob) { free(z->ob); z->ob = 0; z->obcap = 0; z->oblen = 0; z->obpos = 0; }
}

/* img 交编码表：tc=0 DC / 1 AC；counts[16]/values 为标准表（img 亦据此写 DHT 段）。 */
int32_t codec_jenc_dht(void *sp, int32_t tc, int32_t th, uint8_t *counts, uint8_t *values) {
    codec_jenc *z = (codec_jenc *)sp;
    cj_ehuff *e;
    int i, j, k = 0;
    unsigned int code = 0;
    if (tc < 0 || tc > 1 || th < 0 || th > 3) return -1;
    e = tc == 0 ? &z->edc[th] : &z->eac[th];
    memset(e->len, 0, sizeof(e->len));
    for (i = 0; i < 16; ++i) {
        for (j = 0; j < counts[i]; ++j) {
            int sym = values[k];
            e->code[sym] = (uint16_t)code;
            e->len[sym] = (uint8_t)(i + 1);
            ++code; ++k;
            if (k > 256) return -1;
        }
        code <<= 1;
    }
    return 0;
}

static int ejw_grow(codec_jenc *z, uint64_t need) {
    if (z->oblen + need > z->obcap) {
        uint64_t nc = z->obcap ? z->obcap : 4096;
        uint8_t *nb;
        while (nc < z->oblen + need) nc *= 2;
        nb = (uint8_t *)realloc(z->ob, nc);
        if (!nb) { z->oom = 1; return -1; }
        z->ob = nb; z->obcap = nc;
    }
    return 0;
}

/* 位写 + 0xFF→0xFF00 塞字节。产出追加进 ob。 */
static void cj_writebits(codec_jenc *z, int code, int len) {
    z->bitCnt += len;
    z->bitBuf |= code << (24 - z->bitCnt);
    while (z->bitCnt >= 8) {
        unsigned char c;
        if (ejw_grow(z, 2) < 0) return;
        c = (unsigned char)((z->bitBuf >> 16) & 255);
        z->ob[z->oblen++] = c;
        if (c == 255) z->ob[z->oblen++] = 0;
        z->bitBuf <<= 8;
        z->bitCnt -= 8;
    }
}

/* 熵编码一块。返回 1 完成 / -1 内存错。 */
int32_t codec_jenc_block(void *sp, int32_t *du, int32_t dc_tbl, int32_t ac_tbl, int32_t *dc_pred) {
    codec_jenc *z = (codec_jenc *)sp;
    cj_ehuff *hdc = &z->edc[dc_tbl];
    cj_ehuff *hac = &z->eac[ac_tbl];
    int diff, end0pos, i;
    z->oom = 0;
    /* DC 差分 */
    diff = du[0] - *dc_pred;
    *dc_pred = du[0];
    if (diff == 0) {
        cj_writebits(z, hdc->code[0], hdc->len[0]);
    } else {
        int t1 = diff < 0 ? -diff : diff;
        int v = diff < 0 ? diff - 1 : diff;
        int blen = 1;
        while (t1 >>= 1) ++blen;
        cj_writebits(z, hdc->code[blen], hdc->len[blen]);
        cj_writebits(z, v & ((1 << blen) - 1), blen);
    }
    /* AC 游程 */
    end0pos = 63;
    while (end0pos > 0 && du[end0pos] == 0) --end0pos;
    if (end0pos == 0) {
        cj_writebits(z, hac->code[0x00], hac->len[0x00]);
        return z->oom ? -1 : 1;
    }
    for (i = 1; i <= end0pos; ++i) {
        int startpos = i, nrzeroes;
        int t1, v, blen;
        for (; du[i] == 0 && i <= end0pos; ++i) {}
        nrzeroes = i - startpos;
        if (nrzeroes >= 16) {
            int lng = nrzeroes >> 4, m;
            for (m = 1; m <= lng; ++m) cj_writebits(z, hac->code[0xF0], hac->len[0xF0]);
            nrzeroes &= 15;
        }
        t1 = du[i] < 0 ? -du[i] : du[i];
        v = du[i] < 0 ? du[i] - 1 : du[i];
        blen = 1;
        while (t1 >>= 1) ++blen;
        cj_writebits(z, hac->code[(nrzeroes << 4) + blen], hac->len[(nrzeroes << 4) + blen]);
        cj_writebits(z, v & ((1 << blen) - 1), blen);
    }
    if (end0pos != 63) cj_writebits(z, hac->code[0x00], hac->len[0x00]);
    return z->oom ? -1 : 1;
}

/* 抽产出 ob[obpos..oblen) 到 out[0..cap)，返回抽出字节数。 */
int64_t codec_jenc_drain(void *sp, uint8_t *out, uint64_t cap) {
    codec_jenc *z = (codec_jenc *)sp;
    uint64_t avail = z->oblen - z->obpos, take = avail < cap ? avail : cap;
    if (take) memcpy(out, z->ob + z->obpos, take);
    z->obpos += take;
    if (z->obpos >= z->oblen) { z->obpos = 0; z->oblen = 0; }
    return (int64_t)take;
}

/* 收尾：末尾 fillBits(0x7F,7) 位对齐（EOI 由 img 写）。 */
int32_t codec_jenc_flush(void *sp) {
    codec_jenc *z = (codec_jenc *)sp;
    z->oom = 0;
    cj_writebits(z, 0x7F, 7);
    return z->oom ? -1 : 0;
}
