/* codec_impl.c —— sc codec 内置模块默认实现
 *
 * 唯一事实源：C ABI 契约见同目录 codec.h；跨平台基础（stdint 等）经 builtins/platform.h 引入。
 * 算法均按权威规范实现，由 tests/cases/codec_test.sc 的 KAT 向量 / round-trip 验证。
 */
#include "platform.h"
#include "codec.h"

/* ====================== Layer 1 · 簇 2：校验和 ====================== */

/* CRC-32 查表（反射多项式 0xEDB88320）。惰性初始化：计算确定性、幂等，
 *   多线程下的良性竞争只会重复算出同一张表，无需加锁。 */
static uint32_t codec__crc32_tab[256];
static int      codec__crc32_ready = 0;

static void codec__crc32_build(void) {
    uint32_t i, c;
    int k;
    for (i = 0; i < 256; i++) {
        c = i;
        for (k = 0; k < 8; k++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        codec__crc32_tab[i] = c;
    }
    codec__crc32_ready = 1;
}

uint32_t codec_crc32_update(uint32_t crc, void *data, uint64_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t i;
    if (!codec__crc32_ready) codec__crc32_build();
    crc = ~crc;
    for (i = 0; i < len; i++)
        crc = codec__crc32_tab[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

uint32_t codec_crc32(void *data, uint64_t len) {
    return codec_crc32_update(0u, data, len);
}

/* Adler-32：a = 1 + Σbytes (mod 65521)，b = Σa (mod 65521)，结果 (b<<16)|a。
 *   每 5552 字节取模一次（uint32 不溢出的最大安全分块）。 */
uint32_t codec_adler32_update(uint32_t adler, void *data, uint64_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFFu, b = (adler >> 16) & 0xFFFFu;
    const uint32_t MOD = 65521u;
    uint64_t i = 0;
    while (i < len) {
        uint64_t n = len - i;
        uint64_t j;
        if (n > 5552u) n = 5552u;
        for (j = 0; j < n; j++) { a += p[i + j]; b += a; }
        a %= MOD; b %= MOD;
        i += n;
    }
    return (b << 16) | a;
}

uint32_t codec_adler32(void *data, uint64_t len) {
    return codec_adler32_update(1u, data, len);
}

/* ====================== Layer 1 · 簇 3：RLE（PackBits）====================== */

uint64_t codec_rle_bound(uint64_t len) {
    return len + len / 128u + 1u;
}

int64_t codec_rle_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint64_t i = 0, o = 0;
    while (i < len) {
        /* 统计自 i 起的等值行程（上限 128）。 */
        uint64_t run = 1;
        while (i + run < len && run < 128u && p[i + run] == p[i]) run++;
        if (run >= 2u) {
            /* 重复块：控制字节 (257-run) 即 (uint8_t)(1-run)，随后 1 个被重复字节。 */
            if (o + 2u > cap) return -1;
            out[o++] = (uint8_t)(257u - run);
            out[o++] = p[i];
            i += run;
        } else {
            /* 字面块：收集直到下一处出现 >=2 的行程或满 128。 */
            uint64_t lit = i;
            uint64_t litlen, k;
            while (lit < len && (lit - i) < 128u) {
                uint64_t r = 1;
                while (lit + r < len && r < 128u && p[lit + r] == p[lit]) r++;
                if (r >= 2u) break;
                lit++;
            }
            litlen = lit - i;
            if (litlen > 128u) litlen = 128u;
            if (o + 1u + litlen > cap) return -1;
            out[o++] = (uint8_t)(litlen - 1u);   /* 控制字节 0..127 */
            for (k = 0; k < litlen; k++) out[o++] = p[i + k];
            i += litlen;
        }
    }
    return (int64_t)o;
}

int64_t codec_rle_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint64_t i = 0, o = 0;
    while (i < len) {
        int n = (int)(int8_t)p[i++];
        if (n >= 0) {
            /* 字面：复制 n+1 字节。 */
            uint64_t cnt = (uint64_t)n + 1u, k;
            if (i + cnt > len) return -1;          /* 截断 */
            if (o + cnt > cap) return -1;
            for (k = 0; k < cnt; k++) out[o++] = p[i++];
        } else if (n != -128) {
            /* 重复：紧随字节重复 1-n 次。 */
            uint64_t cnt = (uint64_t)(1 - n), k;
            uint8_t b;
            if (i >= len) return -1;
            b = p[i++];
            if (o + cnt > cap) return -1;
            for (k = 0; k < cnt; k++) out[o++] = b;
        }
        /* n == -128：空操作。 */
    }
    return (int64_t)o;
}

/* ====================== Layer 1 · 簇 4：DEFLATE / zlib / gzip ======================
 *
 * 自实现 DEFLATE 解码（RFC 1951），结构借鉴 Mark Adler 的教学实现 puff.c（public domain）
 *   的清晰分层，但改为返回码错误处理（不用 longjmp）：
 *     - 规范 Huffman 解码（cd_construct 建表 / cd_decode 逐位判定）
 *     - 三种块：stored（type 0）/ 固定 Huffman（type 1）/ 动态 Huffman（type 2）
 *     - LZ77 长度/距离码 + 回溯复制
 *   位流 LSB-first；Huffman 码逐位左移累积（与 DEFLATE 的码字打包方向一致）。
 */

typedef struct {
    const uint8_t *in;
    uint64_t inlen, incnt;
    uint8_t *out;
    uint64_t outlen, outcnt;
    int bitbuf, bitcnt;
    int err;
} cd_state;

/* 取 need 位（LSB-first）。输入不足置 err 并返回 0。need 上限 13（距离 extra）。 */
static int cd_bits(cd_state *s, int need) {
    int val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        val |= (int)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return val & ((1 << need) - 1);
}

/* 由各符号码长构建规范 Huffman 表（count[1..15] + 按长度排序的 symbol[]）。
 * 返回 left：0=完整码，>0=不完整（distance 单码等良性情形），<0=过订（非法）。 */
static int cd_construct(short *count, short *symbol, const short *length, int n) {
    int sym, len, left;
    short offs[16];
    for (len = 0; len <= 15; len++) count[len] = 0;
    for (sym = 0; sym < n; sym++) count[length[sym]]++;
    if (count[0] == n) return 0;            /* 无任何码 */
    left = 1;
    for (len = 1; len <= 15; len++) { left <<= 1; left -= count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = offs[len] + count[len];
    for (sym = 0; sym < n; sym++) if (length[sym]) symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

/* 逐位解出一个符号；失败返回负。 */
static int cd_decode(cd_state *s, const short *count, const short *symbol) {
    int len, code = 0, first = 0, cnt, index = 0;
    for (len = 1; len <= 15; len++) {
        code |= cd_bits(s, 1);
        if (s->err) return -2;
        cnt = count[len];
        if (code - first < cnt) return symbol[index + (code - first)];
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return -9;
}

/* 长度/距离码：基值 + 附加位表（RFC 1951 §3.2.5）。 */
static const short cd_lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short cd_lext[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short cd_dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short cd_dext[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

/* 解一个 Huffman 块的码流：字面字节 / 结束符 256 / 长度+距离回溯。 */
static int cd_codes(cd_state *s, const short *lc, const short *ls, const short *dc, const short *ds) {
    int sym, length;
    uint64_t dist;
    do {
        sym = cd_decode(s, lc, ls);
        if (sym < 0) return sym;
        if (sym < 256) {
            if (s->outcnt >= s->outlen) return -1;
            s->out[s->outcnt++] = (uint8_t)sym;
        } else if (sym > 256) {
            sym -= 257;
            if (sym >= 29) return -10;
            length = cd_lbase[sym] + cd_bits(s, cd_lext[sym]);
            sym = cd_decode(s, dc, ds);
            if (sym < 0) return sym;
            if (sym >= 30) return -11;
            dist = (uint64_t)cd_dbase[sym] + (uint64_t)cd_bits(s, cd_dext[sym]);
            if (s->err) return -1;
            if (dist > s->outcnt) return -12;                 /* 无预置字典 */
            if (s->outcnt + (uint64_t)length > s->outlen) return -1;
            while (length-- > 0) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    } while (sym != 256);
    return 0;
}

/* stored 块（type 0）：对齐字节边界后原样复制 LEN 字节。 */
static int cd_stored(cd_state *s) {
    unsigned len;
    s->bitbuf = 0; s->bitcnt = 0;                 /* 丢弃到字节边界 */
    if (s->incnt + 4 > s->inlen) return -2;
    len = s->in[s->incnt++];
    len |= (unsigned)s->in[s->incnt++] << 8;
    s->incnt += 2;                                /* 跳过 NLEN（补码）*/
    if (s->incnt + len > s->inlen) return -2;
    if (s->outcnt + len > s->outlen) return -1;
    while (len-- > 0) s->out[s->outcnt++] = s->in[s->incnt++];
    return 0;
}

/* 固定 Huffman 表（type 1）：按 RFC 1951 §3.2.6 一次性构建后复用。 */
static short cd_fix_lcount[16], cd_fix_lsym[288];
static short cd_fix_dcount[16], cd_fix_dsym[30];
static int   cd_fix_ready = 0;

static void cd_build_fixed(void) {
    short lengths[288];
    int i;
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    cd_construct(cd_fix_lcount, cd_fix_lsym, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    cd_construct(cd_fix_dcount, cd_fix_dsym, lengths, 30);
    cd_fix_ready = 1;
}

static int cd_fixed(cd_state *s) {
    if (!cd_fix_ready) cd_build_fixed();
    return cd_codes(s, cd_fix_lcount, cd_fix_lsym, cd_fix_dcount, cd_fix_dsym);
}

/* 动态 Huffman 块（type 2）：先读“码长码”，再解出字面/距离码长，建表后解码。 */
static int cd_dynamic(cd_state *s) {
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    short lengths[320];
    short lcount[16], lsym[288];
    short dcount[16], dsym[30];
    int nlit, ndist, ncode, index, err, sym;
    nlit  = 257 + cd_bits(s, 5);
    ndist = 1   + cd_bits(s, 5);
    ncode = 4   + cd_bits(s, 4);
    if (s->err) return -1;
    if (nlit > 286 || ndist > 30) return -3;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)cd_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    err = cd_construct(lcount, lsym, lengths, 19);    /* 码长码表 */
    if (err < 0) return -4;
    /* 用码长码表解出 nlit+ndist 个码长（含 16/17/18 重复码）。 */
    index = 0;
    while (index < nlit + ndist) {
        sym = cd_decode(s, lcount, lsym);
        if (sym < 0) return sym;
        if (sym < 16) {
            lengths[index++] = (short)sym;
        } else {
            short val = 0;
            int rep;
            if (sym == 16) { if (index == 0) return -5; val = lengths[index - 1]; rep = 3 + cd_bits(s, 2); }
            else if (sym == 17) { rep = 3 + cd_bits(s, 3); }
            else { rep = 11 + cd_bits(s, 7); }            /* sym == 18 */
            if (index + rep > nlit + ndist) return -6;
            while (rep-- > 0) lengths[index++] = val;
        }
        if (s->err) return -1;
    }
    err = cd_construct(lcount, lsym, lengths, nlit);
    if (err < 0) return -7;
    err = cd_construct(dcount, dsym, lengths + nlit, ndist);
    if (err < 0) return -8;
    return cd_codes(s, lcount, lsym, dcount, dsym);
}

int64_t codec_inflate(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    cd_state s;
    int last, type, err;
    s.in = (const uint8_t *)src; s.inlen = len; s.incnt = 0;
    s.out = out; s.outlen = cap; s.outcnt = 0;
    s.bitbuf = 0; s.bitcnt = 0; s.err = 0;
    do {
        last = cd_bits(&s, 1);
        type = cd_bits(&s, 2);
        if (s.err) return -1;
        if (type == 0)      err = cd_stored(&s);
        else if (type == 1) err = cd_fixed(&s);
        else if (type == 2) err = cd_dynamic(&s);
        else                return -1;               /* type 3 保留 */
        if (err) return -1;
    } while (!last);
    return (int64_t)s.outcnt;
}

int64_t codec_zlib_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    int cmf, flg;
    int64_t n;
    uint32_t want;
    if (len < 6) return -1;
    cmf = p[0]; flg = p[1];
    if ((cmf & 0x0f) != 8) return -1;               /* CM=8 deflate */
    if (((cmf << 8) | flg) % 31 != 0) return -1;    /* 头校验 */
    if (flg & 0x20) return -1;                      /* 不支持预置字典 */
    n = codec_inflate((void *)(p + 2), len - 2, out, cap);
    if (n < 0) return n;
    want = ((uint32_t)p[len - 4] << 24) | ((uint32_t)p[len - 3] << 16)
         | ((uint32_t)p[len - 2] << 8)  |  (uint32_t)p[len - 1];
    if (codec_adler32(out, (uint64_t)n) != want) return -1;
    return n;
}

int64_t codec_gzip_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint64_t off = 10;
    int flg;
    int64_t n;
    uint32_t crc, isize;
    if (len < 18) return -1;
    if (p[0] != 0x1f || p[1] != 0x8b) return -1;    /* 魔数 */
    if (p[2] != 8) return -1;                       /* CM=deflate */
    flg = p[3];
    if (flg & 0x04) {                               /* FEXTRA */
        uint64_t xlen;
        if (off + 2 > len) return -1;
        xlen = (uint64_t)p[off] | ((uint64_t)p[off + 1] << 8);
        off += 2 + xlen;
    }
    if (flg & 0x08) { while (off < len && p[off] != 0) off++; off++; }  /* FNAME */
    if (flg & 0x10) { while (off < len && p[off] != 0) off++; off++; }  /* FCOMMENT */
    if (flg & 0x02) off += 2;                        /* FHCRC */
    if (off >= len) return -1;
    n = codec_inflate((void *)(p + off), len - off, out, cap);
    if (n < 0) return n;
    crc   = (uint32_t)p[len - 8] | ((uint32_t)p[len - 7] << 8) | ((uint32_t)p[len - 6] << 16) | ((uint32_t)p[len - 5] << 24);
    isize = (uint32_t)p[len - 4] | ((uint32_t)p[len - 3] << 8) | ((uint32_t)p[len - 2] << 16) | ((uint32_t)p[len - 1] << 24);
    if (codec_crc32(out, (uint64_t)n) != crc) return -1;
    if ((uint32_t)((uint64_t)n & 0xffffffffu) != isize) return -1;
    return n;
}

/* ─────────── DEFLATE 编码（固定 Huffman + 贪心 LZ77，或 stored 直通）───────────
 *
 * level 0：stored 块直通（不压缩，保证不失败，作为 incompressible 兜底）。
 * level ≥1：固定 Huffman（RFC 1951 §3.2.6）+ 哈希头贪心匹配 LZ77。
 *   匹配以 3 字节哈希定位最近候选并向后扩展（≤258），距离 ≤32768；命中则发长度/距离对，
 *   否则发字面字节。固定 Huffman 码字以 MSB-first 写入 LSB-first 位流（符合 DEFLATE 打包）。
 *   输出可被任何标准 zlib/gzip 解码器还原（已与本模块 inflate round-trip 验证）。
 */

uint64_t codec_deflate_bound(uint64_t len) {
    /* 取固定 Huffman 膨胀上界（len*9/8）与 stored 开销之较大者，再留富余。 */
    return len + len / 8u + (len / 65535u + 1u) * 5u + 64u;
}

typedef struct {
    uint8_t *out;
    uint64_t cap, cnt;
    int bitbuf, bitcnt;
    int err;
} cd_wr;

static void cd_putbit(cd_wr *w, int bit) {
    w->bitbuf |= (bit & 1) << w->bitcnt;
    w->bitcnt++;
    if (w->bitcnt == 8) {
        if (w->cnt < w->cap) w->out[w->cnt] = (uint8_t)w->bitbuf; else w->err = 1;
        w->cnt++;
        w->bitbuf = 0; w->bitcnt = 0;
    }
}

/* 写 n 位，LSB-first（用于附加位与 2 位块类型）。 */
static void cd_putbits(cd_wr *w, int code, int n) {
    int i;
    for (i = 0; i < n; i++) cd_putbit(w, (code >> i) & 1);
}

/* 写 n 位 Huffman 码，MSB-first。 */
static void cd_huffbits(cd_wr *w, int code, int n) {
    int i;
    for (i = n - 1; i >= 0; i--) cd_putbit(w, (code >> i) & 1);
}

static void cd_flush(cd_wr *w) {
    if (w->bitcnt > 0) {
        if (w->cnt < w->cap) w->out[w->cnt] = (uint8_t)w->bitbuf; else w->err = 1;
        w->cnt++;
        w->bitbuf = 0; w->bitcnt = 0;
    }
}

/* 固定 Huffman 字面/长度码的码字与位数。 */
static void cd_litlen_code(int sym, int *code, int *nbits) {
    if (sym <= 143)      { *code = 0x30 + sym;          *nbits = 8; }
    else if (sym <= 255) { *code = 0x190 + (sym - 144); *nbits = 9; }
    else if (sym <= 279) { *code = sym - 256;           *nbits = 7; }
    else                 { *code = 0xC0 + (sym - 280);  *nbits = 8; }
}

static void cd_emit_lit(cd_wr *w, int byte) {
    int code, nbits;
    cd_litlen_code(byte, &code, &nbits);
    cd_huffbits(w, code, nbits);
}

static void cd_emit_match(cd_wr *w, int length, int dist) {
    int i, lsym = 285, dsym = 29, code, nbits, ebits = 0, eval = 0;
    /* 长度码（257..285）。 */
    for (i = 28; i >= 0; i--) if (length >= cd_lbase[i]) { lsym = 257 + i; ebits = cd_lext[i]; eval = length - cd_lbase[i]; break; }
    cd_litlen_code(lsym, &code, &nbits);
    cd_huffbits(w, code, nbits);
    if (ebits) cd_putbits(w, eval, ebits);
    /* 距离码（0..29，固定 5 位）。 */
    for (i = 29; i >= 0; i--) if (dist >= cd_dbase[i]) { dsym = i; ebits = cd_dext[i]; eval = dist - cd_dbase[i]; break; }
    cd_huffbits(w, dsym, 5);
    if (ebits) cd_putbits(w, eval, ebits);
}

/* stored 块直通（level 0）。 */
static int64_t cd_deflate_stored(const uint8_t *src, uint64_t len, uint8_t *out, uint64_t cap) {
    uint64_t o = 0, i = 0;
    do {
        uint64_t block = len - i, k;
        int last;
        if (block > 65535u) block = 65535u;
        last = (i + block == len) ? 1 : 0;
        if (o + 5u + block > cap) return -1;
        out[o++] = (uint8_t)last;                       /* BFINAL=last, BTYPE=00, 5 位填充 */
        out[o++] = (uint8_t)(block & 0xff);
        out[o++] = (uint8_t)((block >> 8) & 0xff);
        out[o++] = (uint8_t)((~block) & 0xff);
        out[o++] = (uint8_t)(((~block) >> 8) & 0xff);
        for (k = 0; k < block; k++) out[o++] = src[i + k];
        i += block;
    } while (i < len);
    return (int64_t)o;
}

#define CD_HASH_BITS 15
#define CD_HASH_SIZE (1 << CD_HASH_BITS)

static int cd_hash3(const uint8_t *p) {
    uint32_t h = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    h = (h * 2654435761u) >> (32 - CD_HASH_BITS);
    return (int)(h & (CD_HASH_SIZE - 1));
}

/* 长度 → 长度码符号(257..285) + 附加位（RFC 1951 §3.2.5）。 */
static void cd_len_sym(int length, int *sym, int *ebits, int *eval) {
    int i;
    for (i = 28; i >= 0; i--) if (length >= cd_lbase[i]) { *sym = 257 + i; *ebits = cd_lext[i]; *eval = length - cd_lbase[i]; return; }
    *sym = 257; *ebits = 0; *eval = 0;
}

/* 距离 → 距离码符号(0..29) + 附加位。 */
static void cd_dist_sym(int dist, int *sym, int *ebits, int *eval) {
    int i;
    for (i = 29; i >= 0; i--) if (dist >= cd_dbase[i]) { *sym = i; *ebits = cd_dext[i]; *eval = dist - cd_dbase[i]; return; }
    *sym = 0; *ebits = 0; *eval = 0;
}

/* 贪心 LZ77（hash 头部，窗口 32K，最短匹配 3）：对每个 token 触发回调。head 由本函数初始化。
 * 算法确定性：同一输入两遍调用产生完全一致的 token 流（动态块据此先统计频率再写码）。 */
typedef void (*cd_lit_cb)(void *ctx, int byte);
typedef void (*cd_match_cb)(void *ctx, int length, int dist);

static void cd_lz77(const uint8_t *src, uint64_t len, int *head, cd_lit_cb on_lit, cd_match_cb on_match, void *ctx) {
    uint64_t i = 0;
    int hi;
    for (hi = 0; hi < CD_HASH_SIZE; hi++) head[hi] = -1;
    while (i < len) {
        int best_len = 0, best_dist = 0;
        if (i + 3 <= len) {
            int h = cd_hash3(src + i);
            int j = head[h];
            if (j >= 0 && (i - (uint64_t)j) <= 32768u) {
                uint64_t maxlen = len - i, l = 0;
                if (maxlen > 258u) maxlen = 258u;
                while (l < maxlen && src[(uint64_t)j + l] == src[i + l]) l++;
                if (l >= 3u) { best_len = (int)l; best_dist = (int)(i - (uint64_t)j); }
            }
            head[h] = (int)i;
        }
        if (best_len >= 3) {
            uint64_t k;
            on_match(ctx, best_len, best_dist);
            for (k = 1; k < (uint64_t)best_len && i + k + 3 <= len; k++)
                head[cd_hash3(src + i + k)] = (int)(i + k);
            i += (uint64_t)best_len;
        } else {
            on_lit(ctx, src[i]);
            i++;
        }
    }
}

/* —— 固定 Huffman（type 1）回调 —— */
static void cd_fix_lit(void *ctx, int byte) { cd_emit_lit((cd_wr *)ctx, byte); }
static void cd_fix_match(void *ctx, int length, int dist) { cd_emit_match((cd_wr *)ctx, length, dist); }

/* 固定 Huffman + 贪心 LZ77（level == 1）。 */
static int64_t cd_deflate_fixed(const uint8_t *src, uint64_t len, uint8_t *out, uint64_t cap) {
    cd_wr w;
    int *head;
    head = (int *)malloc(sizeof(int) * CD_HASH_SIZE);
    if (!head) return -1;
    w.out = out; w.cap = cap; w.cnt = 0; w.bitbuf = 0; w.bitcnt = 0; w.err = 0;
    cd_putbit(&w, 1);          /* BFINAL=1（单块） */
    cd_putbits(&w, 1, 2);      /* BTYPE=01 固定 Huffman */
    cd_lz77(src, len, head, cd_fix_lit, cd_fix_match, &w);
    { int code, nbits; cd_litlen_code(256, &code, &nbits); cd_huffbits(&w, code, nbits); }  /* 块结束符 */
    cd_flush(&w);
    free(head);
    if (w.err) return -1;
    return (int64_t)w.cnt;
}

/* —— 动态 Huffman（type 2）回调 —— */
/* 前向声明：规范 Huffman 建树/赋码在下文「熵编码原子」节定义，此处先用于动态块。 */
static int  cd_huffman_lengths(const uint32_t *freq, int n, int limit, uint8_t *len);
static void cd_huffman_codes(const uint8_t *len, int n, int maxlen, uint32_t *codes);

typedef struct { uint32_t *fll; uint32_t *fd; } cd_cntctx;       /* 第一遍：统计 litlen/dist 频率 */
static void cd_cnt_lit(void *ctx, int byte) { ((cd_cntctx *)ctx)->fll[byte]++; }
static void cd_cnt_match(void *ctx, int length, int dist) {
    cd_cntctx *c = (cd_cntctx *)ctx;
    int sym, eb, ev;
    cd_len_sym(length, &sym, &eb, &ev);  c->fll[sym]++;
    cd_dist_sym(dist, &sym, &eb, &ev);   c->fd[sym]++;
}

typedef struct {                                                 /* 第二遍：用定制码字写数据 */
    cd_wr *w;
    const uint32_t *ll_code; const uint8_t *ll_len;
    const uint32_t *d_code;  const uint8_t *d_len;
} cd_emitctx;
static void cd_dyn_lit(void *ctx, int byte) {
    cd_emitctx *e = (cd_emitctx *)ctx;
    cd_huffbits(e->w, (int)e->ll_code[byte], e->ll_len[byte]);
}
static void cd_dyn_match(void *ctx, int length, int dist) {
    cd_emitctx *e = (cd_emitctx *)ctx;
    int sym, eb, ev;
    cd_len_sym(length, &sym, &eb, &ev);
    cd_huffbits(e->w, (int)e->ll_code[sym], e->ll_len[sym]);
    if (eb) cd_putbits(e->w, ev, eb);
    cd_dist_sym(dist, &sym, &eb, &ev);
    cd_huffbits(e->w, (int)e->d_code[sym], e->d_len[sym]);
    if (eb) cd_putbits(e->w, ev, eb);
}

/* 把 litlen+dist 码长序列做 RFC 1951 §3.2.7 行程编码，产出码长码符号(0..18)及其附加位。
 * clsym[k] / clxbits[k] / clxval[k] 分别为第 k 个码长码符号、附加位数、附加值。返回符号个数。 */
static int cd_rle_lengths(const short *lens, int n, uint8_t *clsym, uint8_t *clxbits, uint8_t *clxval) {
    int i = 0, m = 0;
    while (i < n) {
        int val = lens[i], run = 1;
        while (i + run < n && lens[i + run] == val) run++;
        if (val == 0) {
            while (run >= 11) { int r = run > 138 ? 138 : run; clsym[m] = 18; clxbits[m] = 7; clxval[m] = (uint8_t)(r - 11); m++; run -= r; i += r; }
            while (run >= 3)  { int r = run > 10  ? 10  : run; clsym[m] = 17; clxbits[m] = 3; clxval[m] = (uint8_t)(r - 3);  m++; run -= r; i += r; }
            while (run > 0)   { clsym[m] = 0; clxbits[m] = 0; clxval[m] = 0; m++; run--; i++; }
        } else {
            clsym[m] = (uint8_t)val; clxbits[m] = 0; clxval[m] = 0; m++; i++; run--;   /* 先发一次本值 */
            while (run >= 3) { int r = run > 6 ? 6 : run; clsym[m] = 16; clxbits[m] = 2; clxval[m] = (uint8_t)(r - 3); m++; run -= r; i += r; }
            while (run > 0)  { clsym[m] = (uint8_t)val; clxbits[m] = 0; clxval[m] = 0; m++; run--; i++; }
        }
    }
    return m;
}

/* 动态 Huffman + 贪心 LZ77（level ≥ 2）：两遍 LZ77（先统计频率建树，再写码流）。 */
static int64_t cd_deflate_dynamic(const uint8_t *src, uint64_t len, uint8_t *out, uint64_t cap) {
    static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    uint32_t fll[286], fd[30], fcl[19];
    uint8_t  ll_len[286], d_len[30], cl_len[19];
    uint32_t ll_code[286], d_code[30], cl_code[19];
    short    lens[320];
    uint8_t  clsym[320], clxb[320], clxv[320];
    int     *head;
    cd_wr    w;
    cd_cntctx cnt;
    cd_emitctx em;
    int i, nlit, ndist, ncode, m, ll_max, d_max, cl_max;

    head = (int *)malloc(sizeof(int) * CD_HASH_SIZE);
    if (!head) return -1;

    /* 第一遍：统计频率 */
    for (i = 0; i < 286; i++) fll[i] = 0;
    for (i = 0; i < 30; i++)  fd[i] = 0;
    cnt.fll = fll; cnt.fd = fd;
    cd_lz77(src, len, head, cd_cnt_lit, cd_cnt_match, &cnt);
    fll[256] += 1;                                  /* 块结束符必出现一次 */

    /* 建 litlen / dist 树（限长 15） */
    ll_max = cd_huffman_lengths(fll, 286, 15, ll_len);
    d_max  = cd_huffman_lengths(fd, 30, 15, d_len);
    if (ll_max < 0 || d_max < 0) { free(head); return -1; }
    if (d_max == 0) { d_len[0] = 1; d_max = 1; }    /* 无匹配：仍需至少一个距离码 */
    cd_huffman_codes(ll_len, 286, ll_max, ll_code);
    cd_huffman_codes(d_len, 30, d_max, d_code);

    nlit = 286;  while (nlit > 257 && ll_len[nlit - 1] == 0) nlit--;
    ndist = 30;  while (ndist > 1 && d_len[ndist - 1] == 0) ndist--;

    /* 拼接码长序列并行程编码 */
    for (i = 0; i < nlit; i++)  lens[i] = (short)ll_len[i];
    for (i = 0; i < ndist; i++) lens[nlit + i] = (short)d_len[i];
    m = cd_rle_lengths(lens, nlit + ndist, clsym, clxb, clxv);

    /* 建码长码树（限长 7） */
    for (i = 0; i < 19; i++) fcl[i] = 0;
    for (i = 0; i < m; i++) fcl[clsym[i]]++;
    cl_max = cd_huffman_lengths(fcl, 19, 7, cl_len);
    if (cl_max < 0) { free(head); return -1; }
    cd_huffman_codes(cl_len, 19, cl_max, cl_code);
    ncode = 19;  while (ncode > 4 && cl_len[order[ncode - 1]] == 0) ncode--;

    /* —— 写块头 —— */
    w.out = out; w.cap = cap; w.cnt = 0; w.bitbuf = 0; w.bitcnt = 0; w.err = 0;
    cd_putbit(&w, 1);                       /* BFINAL=1（单块） */
    cd_putbits(&w, 2, 2);                   /* BTYPE=10 动态 Huffman */
    cd_putbits(&w, nlit - 257, 5);          /* HLIT */
    cd_putbits(&w, ndist - 1, 5);           /* HDIST */
    cd_putbits(&w, ncode - 4, 4);           /* HCLEN */
    for (i = 0; i < ncode; i++) cd_putbits(&w, cl_len[order[i]], 3);
    for (i = 0; i < m; i++) {               /* 码长码流（码字 MSB-first + 附加位 LSB-first） */
        cd_huffbits(&w, (int)cl_code[clsym[i]], cl_len[clsym[i]]);
        if (clxb[i]) cd_putbits(&w, clxv[i], clxb[i]);
    }

    /* —— 第二遍：写数据 —— */
    em.w = &w; em.ll_code = ll_code; em.ll_len = ll_len; em.d_code = d_code; em.d_len = d_len;
    cd_lz77(src, len, head, cd_dyn_lit, cd_dyn_match, &em);
    cd_huffbits(&w, (int)ll_code[256], ll_len[256]);   /* 块结束符 */
    cd_flush(&w);
    free(head);
    if (w.err) return -1;
    return (int64_t)w.cnt;
}

int64_t codec_deflate(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level) {
    if (level <= 0) return cd_deflate_stored((const uint8_t *)src, len, out, cap);
    if (level == 1) return cd_deflate_fixed((const uint8_t *)src, len, out, cap);
    {   /* level ≥ 2：动态 Huffman；失败（容量不足等）回退固定块。 */
        int64_t n = cd_deflate_dynamic((const uint8_t *)src, len, out, cap);
        if (n < 0) return cd_deflate_fixed((const uint8_t *)src, len, out, cap);
        return n;
    }
}

int64_t codec_zlib_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level) {
    uint64_t o;
    int64_t n;
    uint32_t ad;
    if (cap < 6) return -1;
    out[0] = 0x78;             /* CM=8, CINFO=7（32K 窗口） */
    out[1] = 0x9c;             /* FLG：(0x789c) % 31 == 0 */
    o = 2;
    n = codec_deflate(src, len, out + o, cap - o - 4, level);
    if (n < 0) return -1;
    o += (uint64_t)n;
    ad = codec_adler32(src, len);
    out[o++] = (uint8_t)((ad >> 24) & 0xff);
    out[o++] = (uint8_t)((ad >> 16) & 0xff);
    out[o++] = (uint8_t)((ad >> 8) & 0xff);
    out[o++] = (uint8_t)(ad & 0xff);
    return (int64_t)o;
}

int64_t codec_gzip_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t level) {
    uint64_t o;
    int64_t n;
    uint32_t crc, isize;
    if (cap < 18) return -1;
    out[0] = 0x1f; out[1] = 0x8b; out[2] = 8; out[3] = 0;   /* 魔数, CM=deflate, FLG=0 */
    out[4] = 0; out[5] = 0; out[6] = 0; out[7] = 0;          /* MTIME=0 */
    out[8] = 0; out[9] = 0xff;                               /* XFL=0, OS=unknown */
    o = 10;
    n = codec_deflate(src, len, out + o, cap - o - 8, level);
    if (n < 0) return -1;
    o += (uint64_t)n;
    crc = codec_crc32(src, len);
    out[o++] = (uint8_t)(crc & 0xff);
    out[o++] = (uint8_t)((crc >> 8) & 0xff);
    out[o++] = (uint8_t)((crc >> 16) & 0xff);
    out[o++] = (uint8_t)((crc >> 24) & 0xff);
    isize = (uint32_t)(len & 0xffffffffu);
    out[o++] = (uint8_t)(isize & 0xff);
    out[o++] = (uint8_t)((isize >> 8) & 0xff);
    out[o++] = (uint8_t)((isize >> 16) & 0xff);
    out[o++] = (uint8_t)((isize >> 24) & 0xff);
    return (int64_t)o;
}

/* ====================== Layer 0 · 簇 1：熵编码原子（规范 Huffman）======================
 *
 * Huffman 是无损压缩的最底层积木：DEFLATE 的动态块、JPEG 的 DC/AC 码表都构建于其上。
 * 本节把「频率 → 建树 → 限长码长 → 规范码字」做成独立可复用的原子，并配一体化的
 * order-0 字节 Huffman 编解码器。位读写与解码复用上文 DEFLATE 的 cd_state / cd_construct /
 * cd_decode（LSB-first 打包、MSB-first 码字），编码复用 cd_wr / cd_huffbits / cd_flush，
 * 故码字布局与 DEFLATE 完全一致。
 *
 * 建树：O(active^2) 反复取两最小并记父指针（active ≤ 288，开销可忽略），由叶深度得原始码长；
 * 限长：libjpeg 风格的 bits[] 直方图折叠（保持 Kraft 完备），把超过 limit 的码长压回 ≤ limit；
 * 赋长：按频率降序把最短码长发给最高频符号（重排不等式，给定长度多重集下最优）。
 */

#define CD_HUFF_MAXSYM 288        /* 覆盖 DEFLATE litlen(286)/dist(30)/cl(19) 与 JPEG(≤256) */

/* 由符号频率构建长度受限的规范 Huffman 码长。
 *   freq[0..n)：各符号频次；limit：最大码长（1..15）；len[0..n)：回填码长（0=该符号未出现）。
 * 返回实际最大码长（≥1）；无任何符号返回 0；参数非法 / limit 过小（无法容纳）返回 -1。 */
static int cd_huffman_lengths(const uint32_t *freq, int n, int limit, uint8_t *len) {
    uint64_t nfreq[2 * CD_HUFF_MAXSYM];
    int nparent[2 * CD_HUFF_MAXSYM], nsym[2 * CD_HUFF_MAXSYM];
    int order[CD_HUFF_MAXSYM];
    int bits[64];
    int i, nc = 0, active = 0, only = -1, remaining, maxraw = 0, l, idx, maxlen;
    if (n <= 0 || n > CD_HUFF_MAXSYM || limit < 1 || limit > 15) return -1;
    for (i = 0; i < n; i++) len[i] = 0;
    for (i = 0; i < n; i++) if (freq[i] > 0) { active++; only = i; }
    if (active == 0) return 0;
    if (active == 1) { len[only] = 1; return 1; }
    /* 叶节点 */
    for (i = 0; i < n; i++) if (freq[i] > 0) {
        nfreq[nc] = freq[i]; nparent[nc] = -1; nsym[nc] = i; nc++;
    }
    /* 反复合并两最小频节点 */
    remaining = active;
    while (remaining > 1) {
        int a = -1, b = -1, j;
        for (j = 0; j < nc; j++) if (nparent[j] == -1) {
            if (a < 0 || nfreq[j] < nfreq[a]) { b = a; a = j; }
            else if (b < 0 || nfreq[j] < nfreq[b]) { b = j; }
        }
        nfreq[nc] = nfreq[a] + nfreq[b]; nparent[nc] = -1; nsym[nc] = -1;
        nparent[a] = nc; nparent[b] = nc; nc++;
        remaining--;
    }
    /* 叶深度 = 原始码长 */
    for (l = 0; l < 64; l++) bits[l] = 0;
    for (i = 0; i < nc; i++) if (nsym[i] >= 0) {
        int depth = 0, k = i;
        while (nparent[k] != -1) { k = nparent[k]; depth++; }
        if (depth > 63) depth = 63;
        len[nsym[i]] = (uint8_t)depth;
        bits[depth]++;
        if (depth > maxraw) maxraw = depth;
    }
    /* 限长：把 > limit 的码长折叠回 ≤ limit（libjpeg 风格，保持完备） */
    for (l = maxraw; l > limit; l--) {
        while (bits[l] > 0) {
            int j = l - 2;
            while (j >= 1 && bits[j] == 0) j--;
            if (j < 1) return -1;                 /* limit 过小，容纳不下该符号数 */
            bits[l]     -= 2;
            bits[l - 1] += 1;
            bits[j + 1] += 2;
            bits[j]     -= 1;
        }
    }
    /* 活跃符号按频率降序（频率相同则符号序升序）排序 */
    idx = 0;
    for (i = 0; i < n; i++) if (freq[i] > 0) order[idx++] = i;
    for (i = 0; i < active; i++) {
        int j, best = i;
        for (j = i + 1; j < active; j++)
            if (freq[order[j]] > freq[order[best]] ||
                (freq[order[j]] == freq[order[best]] && order[j] < order[best]))
                best = j;
        if (best != i) { int t = order[i]; order[i] = order[best]; order[best] = t; }
    }
    /* 最短码长发给最高频符号 */
    idx = 0; maxlen = 0;
    for (l = 1; l <= limit; l++) {
        int c;
        for (c = 0; c < bits[l]; c++) { len[order[idx++]] = (uint8_t)l; maxlen = l; }
    }
    return maxlen;
}

/* 由码长生成规范 Huffman 码字（RFC 1951 §3.2.2，与 cd_construct/cd_decode 同序）。 */
static void cd_huffman_codes(const uint8_t *len, int n, int maxlen, uint32_t *codes) {
    int blcount[16], bl;
    uint32_t nextcode[16], code = 0;
    for (bl = 0; bl <= 15; bl++) blcount[bl] = 0;
    { int i; for (i = 0; i < n; i++) blcount[len[i]]++; }
    blcount[0] = 0;
    for (bl = 1; bl <= maxlen; bl++) { code = (code + (uint32_t)blcount[bl - 1]) << 1; nextcode[bl] = code; }
    { int i; for (i = 0; i < n; i++) { int l = len[i]; codes[i] = l ? nextcode[l]++ : 0u; } }
}

/* 公开原子：频率 → 限长规范码长。见 codec.sc 的 @fnc codec_huffman_build。 */
int32_t codec_huffman_build(uint32_t *freq, int32_t n, int32_t limit, uint8_t *lengths) {
    return cd_huffman_lengths(freq, n, limit, lengths);
}

/* order-0 字节 Huffman 编码输出上界：8(原长) + 128(码长表) + 每符号≤15 位 + 收尾。 */
uint64_t codec_huffman_bound(uint64_t len) {
    return 8u + 128u + len * 2u + 16u;
}

/* order-0 字节 Huffman 编码。布局：原长(8,LE) + 256 符号码长(128,半字节打包) + 位流。
 * 返回写入字节数；cap 不足返回 -1。len==0 仅写 8 字节头。 */
int64_t codec_huffman_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint32_t freq[256], codes[256];
    uint8_t lengths[256];
    cd_wr w;
    uint64_t i;
    int maxlen, s;
    if (cap < 8) return -1;
    for (i = 0; i < 8; i++) out[i] = (uint8_t)((len >> (i * 8)) & 0xff);
    if (len == 0) return 8;
    for (s = 0; s < 256; s++) freq[s] = 0;
    for (i = 0; i < len; i++) freq[p[i]]++;
    maxlen = cd_huffman_lengths(freq, 256, 15, lengths);
    if (maxlen < 0) return -1;
    cd_huffman_codes(lengths, 256, maxlen, codes);
    if (cap < 8u + 128u) return -1;
    for (s = 0; s < 256; s += 2)
        out[8 + s / 2] = (uint8_t)((lengths[s] & 0x0f) | ((lengths[s + 1] & 0x0f) << 4));
    w.out = out; w.cap = cap; w.cnt = 8u + 128u; w.bitbuf = 0; w.bitcnt = 0; w.err = 0;
    for (i = 0; i < len; i++) cd_huffbits(&w, (int)codes[p[i]], lengths[p[i]]);
    cd_flush(&w);
    if (w.err) return -1;
    return (int64_t)w.cnt;
}

/* order-0 字节 Huffman 解码（codec_huffman_encode 的逆）。返回原始字节数；失败 / cap 不足 -1。 */
int64_t codec_huffman_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    short length[256], count[16], symbol[256];
    cd_state s;
    uint64_t origlen = 0, i;
    int sb, k;
    if (len < 8) return -1;
    for (i = 0; i < 8; i++) origlen |= (uint64_t)p[i] << (i * 8);
    if (origlen == 0) return 0;
    if (len < 8u + 128u) return -1;
    for (k = 0; k < 256; k += 2) {
        uint8_t byte = p[8 + k / 2];
        length[k] = (short)(byte & 0x0f);
        length[k + 1] = (short)((byte >> 4) & 0x0f);
    }
    if (cd_construct(count, symbol, length, 256) < 0) return -1;
    s.in = p + 8u + 128u; s.inlen = len - (8u + 128u); s.incnt = 0;
    s.out = NULL; s.outlen = 0; s.outcnt = 0;
    s.bitbuf = 0; s.bitcnt = 0; s.err = 0;
    for (i = 0; i < origlen; i++) {
        sb = cd_decode(&s, count, symbol);
        if (sb < 0) return -1;
        if (i >= cap) return -1;
        out[i] = (uint8_t)sb;
    }
    return (int64_t)origlen;
}

/* ====================== Layer 0 · 簇 5：ANS 熵编码（静态 rANS）======================
 *
 * rANS（range Asymmetric Numeral Systems，Jarek Duda；编码内核取 ryg rans_byte.h 方案）。
 * 与 Huffman 并列的熵编码原子，但以单个 32 位状态寄存器逼近【分数比特】，故压缩率更接近熵。
 * 核心三步：① 频率归一到 2^tablelog；② 编码（逆序处理符号，状态 renorm 时吐字节，末尾 flush 状态）；
 * ③ 解码（正序：状态低位定位符号 → 还原状态 → renorm 吸字节）。位流方向与 ryg 一致。
 *
 * 字节流自描述布局：原长(8,LE) + tablelog(1) + 256×归一频次(u2,LE=512) + rANS 负载。
 * tablelog 固定 12（TOTAL=4096），对 256 元字母表足够细且 x_max 不溢出 32 位。
 */

#define CD_RANS_L      (1u << 23)        /* 状态下界；renorm 以字节为单位 */
#define CD_RANS_TLOG   12                /* 表对数，TOTAL = 1<<12 = 4096 */
#define CD_RANS_HDR    (9u + 512u)       /* 8 原长 + 1 tablelog + 512 归一表 */

/* 频率归一：缩放到总和恰为 2^tablelog，出现的符号至少为 1。见 codec.sc 的 @fnc。 */
int32_t codec_rans_normalize(uint32_t *freq, int32_t n, int32_t tablelog, uint16_t *norm) {
    uint32_t total = 0, TOTAL, sum = 0;
    int i, distinct = 0;
    if (n <= 0 || tablelog < 1 || tablelog > 16) return -1;
    TOTAL = 1u << tablelog;
    for (i = 0; i < n; i++) { total += freq[i]; if (freq[i]) distinct++; }
    if (total == 0) return 0;
    if ((uint32_t)distinct > TOTAL) return -1;
    for (i = 0; i < n; i++) {
        if (freq[i] == 0) { norm[i] = 0; continue; }
        {   uint64_t v = (uint64_t)freq[i] * TOTAL / total;
            if (v < 1) v = 1;
            if (v > TOTAL) v = TOTAL;
            norm[i] = (uint16_t)v; sum += (uint32_t)v;
        }
    }
    while (sum > TOTAL) {                 /* 收缩：从最大的（>1）扣 */
        int best = -1;
        for (i = 0; i < n; i++) if (norm[i] > 1 && (best < 0 || norm[i] > norm[best])) best = i;
        if (best < 0) return -1;
        norm[best]--; sum--;
    }
    while (sum < TOTAL) {                 /* 扩张：给最大的加 */
        int best = -1;
        for (i = 0; i < n; i++) if (norm[i] > 0 && (best < 0 || norm[i] > norm[best])) best = i;
        if (best < 0) return -1;
        norm[best]++; sum++;
    }
    return tablelog;
}

/* order-0 字节 rANS 编码输出上界：头(521) + 负载(≤ 原长 + 状态 4) + 富余。 */
uint64_t codec_rans_bound(uint64_t len) {
    return CD_RANS_HDR + len + (len >> 1) + 64u;
}

/* order-0 字节 rANS 编码。布局见上。逆序编码、负载在 scratch 内自高向低生长后顺序拷出。 */
int64_t codec_rans_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint32_t freq[256], cum[256];
    uint16_t norm[256];
    uint8_t *scratch, *ptr, *base;
    uint32_t x, c;
    uint64_t i, scap, paylen;
    int s;
    if (cap < 8) return -1;
    for (i = 0; i < 8; i++) out[i] = (uint8_t)((len >> (i * 8)) & 0xff);
    if (len == 0) return 8;
    if (cap < CD_RANS_HDR) return -1;
    for (s = 0; s < 256; s++) freq[s] = 0;
    for (i = 0; i < len; i++) freq[p[i]]++;
    if (codec_rans_normalize(freq, 256, CD_RANS_TLOG, norm) <= 0) return -1;
    out[8] = (uint8_t)CD_RANS_TLOG;
    c = 0;
    for (s = 0; s < 256; s++) {           /* 累计起点 + 序列化归一表 */
        cum[s] = c; c += norm[s];
        out[9 + s * 2]     = (uint8_t)(norm[s] & 0xff);
        out[9 + s * 2 + 1] = (uint8_t)((norm[s] >> 8) & 0xff);
    }
    scap = len * 2u + 1024u;              /* rANS 负载 ≤ 原长+4，scratch 远富余 */
    scratch = (uint8_t *)malloc(scap);
    if (!scratch) return -1;
    base = scratch; ptr = scratch + scap;
    x = CD_RANS_L;
    for (i = len; i > 0; i--) {           /* 逆序：RansEncPut */
        uint32_t f, start, xmax;
        s = p[i - 1];
        f = norm[s]; start = cum[s];
        xmax = ((CD_RANS_L >> CD_RANS_TLOG) << 8) * f;
        while (x >= xmax) {
            if (ptr <= base) { free(scratch); return -1; }
            *(--ptr) = (uint8_t)(x & 0xff); x >>= 8;
        }
        x = ((x / f) << CD_RANS_TLOG) + (x % f) + start;
    }
    if (ptr - base < 4) { free(scratch); return -1; }   /* flush 状态（4 字节，LE） */
    *(--ptr) = (uint8_t)((x >> 24) & 0xff);
    *(--ptr) = (uint8_t)((x >> 16) & 0xff);
    *(--ptr) = (uint8_t)((x >> 8) & 0xff);
    *(--ptr) = (uint8_t)(x & 0xff);
    paylen = (uint64_t)((scratch + scap) - ptr);
    if (CD_RANS_HDR + paylen > cap) { free(scratch); return -1; }
    for (i = 0; i < paylen; i++) out[CD_RANS_HDR + i] = ptr[i];
    free(scratch);
    return (int64_t)(CD_RANS_HDR + paylen);
}

/* order-0 字节 rANS 解码（codec_rans_encode 的逆）。返回原始字节数；失败 / cap 不足 -1。 */
int64_t codec_rans_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint32_t cum[256];
    uint16_t norm[256];
    uint8_t *slot;
    const uint8_t *pp, *pend;
    uint64_t origlen = 0, i;
    uint32_t TOTAL, x, c;
    int tablelog, s;
    if (len < 8) return -1;
    for (i = 0; i < 8; i++) origlen |= (uint64_t)p[i] << (i * 8);
    if (origlen == 0) return 0;
    if (len < CD_RANS_HDR + 4u) return -1;
    tablelog = p[8];
    if (tablelog < 1 || tablelog > 16) return -1;
    TOTAL = 1u << tablelog;
    c = 0;
    for (s = 0; s < 256; s++) {
        norm[s] = (uint16_t)(p[9 + s * 2] | ((uint16_t)p[9 + s * 2 + 1] << 8));
        cum[s] = c; c += norm[s];
    }
    if (c != TOTAL) return -1;            /* 归一表须恰好填满 TOTAL */
    if (origlen > cap) return -1;
    slot = (uint8_t *)malloc(TOTAL);      /* slot → symbol 查找表 */
    if (!slot) return -1;
    for (s = 0; s < 256; s++) {
        uint32_t k;
        for (k = 0; k < norm[s]; k++) slot[cum[s] + k] = (uint8_t)s;
    }
    pp = p + CD_RANS_HDR; pend = p + len;
    x = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) | ((uint32_t)pp[2] << 16) | ((uint32_t)pp[3] << 24);
    pp += 4;
    for (i = 0; i < origlen; i++) {
        uint32_t sl = x & (TOTAL - 1);
        int sym = slot[sl];
        out[i] = (uint8_t)sym;
        x = (uint32_t)norm[sym] * (x >> tablelog) + sl - cum[sym];
        while (x < CD_RANS_L) {
            if (pp >= pend) { free(slot); return -1; }
            x = (x << 8) | (uint32_t)(*pp++);
        }
    }
    free(slot);
    return (int64_t)origlen;
}

/* ====================== Layer 0 · 簇 6：区间编码（算术编码的字节实现）======================
 *
 * Subbotin 无进位(carryless)区间编码器：算术编码家族的第三个原子，与 Huffman（整数比特）、
 * rANS（分数比特、逆序状态）并列。区间编码以 32 位 [low, low+range) 区间逐符号细分逼近熵，
 * 正序流式、单次扫描即可编/解。low/range/code 全用 uint32_t，溢出回绕即「无进位」技巧。
 * 频率归一复用簇5 的 codec_rans_normalize（总和 = 2^14 < BOT，保证 range/total ≥ 1）。
 * 自描述布局与 rANS 相同：原长(8,LE) + tablelog(1) + 256×归一频次(u2,LE=512) + 区间码负载。
 */

#define CD_RC_TOP   (1u << 24)
#define CD_RC_BOT   (1u << 16)
#define CD_RC_TLOG  14                    /* TOTAL = 2^14 = 16384 < BOT */

typedef struct {
    uint32_t low, range, code;
    uint8_t *out; uint64_t cap, pos;      /* 编码：正序写出 */
    const uint8_t *in; uint64_t inlen, inpos;  /* 解码：正序读入 */
    int err;
} cd_rc;

static void cd_rc_put(cd_rc *rc, uint32_t b) {
    if (rc->pos < rc->cap) rc->out[rc->pos] = (uint8_t)(b & 0xff); else rc->err = 1;
    rc->pos++;
}
static uint32_t cd_rc_take(cd_rc *rc) {
    if (rc->inpos < rc->inlen) return rc->in[rc->inpos++];
    rc->inpos++; return 0;                /* 越界读返回 0（合法流不会触发） */
}

/* 编码归一：settle 已定的高字节并吐出；range 过小时用无进位修正。 */
static void cd_rc_enc_norm(cd_rc *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < CD_RC_TOP ||
           (rc->range < CD_RC_BOT && ((rc->range = (0u - rc->low) & (CD_RC_BOT - 1)), 1))) {
        cd_rc_put(rc, rc->low >> 24);
        rc->low <<= 8; rc->range <<= 8;
    }
}
static void cd_rc_encode_sym(cd_rc *rc, uint32_t cum, uint32_t freq, uint32_t tot) {
    rc->range /= tot;
    rc->low += cum * rc->range;
    rc->range *= freq;
    cd_rc_enc_norm(rc);
}
static void cd_rc_enc_flush(cd_rc *rc) {
    int i;
    for (i = 0; i < 4; i++) { cd_rc_put(rc, rc->low >> 24); rc->low <<= 8; }
}

/* 解码归一：与编码对称，吸入字节。 */
static void cd_rc_dec_norm(cd_rc *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < CD_RC_TOP ||
           (rc->range < CD_RC_BOT && ((rc->range = (0u - rc->low) & (CD_RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | cd_rc_take(rc);
        rc->low <<= 8; rc->range <<= 8;
    }
}
static uint32_t cd_rc_dec_getfreq(cd_rc *rc, uint32_t tot) {
    rc->range /= tot;
    return (rc->code - rc->low) / rc->range;
}
static void cd_rc_dec_update(cd_rc *rc, uint32_t cum, uint32_t freq) {
    rc->low += cum * rc->range;
    rc->range *= freq;
    cd_rc_dec_norm(rc);
}

/* order-0 字节区间编码。布局见上。正序编码，直接写入 out 的负载区。 */
int64_t codec_range_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint32_t freq[256], cum[256];
    uint16_t norm[256];
    cd_rc rc;
    uint64_t i;
    uint32_t c, TOTAL;
    int s;
    if (cap < 8) return -1;
    for (i = 0; i < 8; i++) out[i] = (uint8_t)((len >> (i * 8)) & 0xff);
    if (len == 0) return 8;
    if (cap < CD_RANS_HDR) return -1;
    for (s = 0; s < 256; s++) freq[s] = 0;
    for (i = 0; i < len; i++) freq[p[i]]++;
    if (codec_rans_normalize(freq, 256, CD_RC_TLOG, norm) <= 0) return -1;
    TOTAL = 1u << CD_RC_TLOG;
    out[8] = (uint8_t)CD_RC_TLOG;
    c = 0;
    for (s = 0; s < 256; s++) {
        cum[s] = c; c += norm[s];
        out[9 + s * 2]     = (uint8_t)(norm[s] & 0xff);
        out[9 + s * 2 + 1] = (uint8_t)((norm[s] >> 8) & 0xff);
    }
    rc.low = 0; rc.range = 0xFFFFFFFFu; rc.code = 0;
    rc.out = out; rc.cap = cap; rc.pos = CD_RANS_HDR;
    rc.in = NULL; rc.inlen = 0; rc.inpos = 0; rc.err = 0;
    for (i = 0; i < len; i++) {
        s = p[i];
        cd_rc_encode_sym(&rc, cum[s], norm[s], TOTAL);
    }
    cd_rc_enc_flush(&rc);
    if (rc.err) return -1;
    return (int64_t)rc.pos;
}

/* order-0 字节区间解码（codec_range_encode 的逆）。返回原始字节数；失败 / cap 不足 -1。 */
int64_t codec_range_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint32_t cum[256];
    uint16_t norm[256];
    uint8_t *slot;
    cd_rc rc;
    uint64_t origlen = 0, i;
    uint32_t TOTAL, c;
    int tablelog, s;
    if (len < 8) return -1;
    for (i = 0; i < 8; i++) origlen |= (uint64_t)p[i] << (i * 8);
    if (origlen == 0) return 0;
    if (len < CD_RANS_HDR + 4u) return -1;
    tablelog = p[8];
    if (tablelog < 1 || tablelog > 16) return -1;
    TOTAL = 1u << tablelog;
    c = 0;
    for (s = 0; s < 256; s++) {
        norm[s] = (uint16_t)(p[9 + s * 2] | ((uint16_t)p[9 + s * 2 + 1] << 8));
        cum[s] = c; c += norm[s];
    }
    if (c != TOTAL) return -1;
    if (origlen > cap) return -1;
    slot = (uint8_t *)malloc(TOTAL);
    if (!slot) return -1;
    for (s = 0; s < 256; s++) {
        uint32_t k;
        for (k = 0; k < norm[s]; k++) slot[cum[s] + k] = (uint8_t)s;
    }
    rc.low = 0; rc.range = 0xFFFFFFFFu; rc.code = 0;
    rc.out = NULL; rc.cap = 0; rc.pos = 0;
    rc.in = p + CD_RANS_HDR; rc.inlen = len - CD_RANS_HDR; rc.inpos = 0; rc.err = 0;
    for (i = 0; i < 4; i++) rc.code = (rc.code << 8) | cd_rc_take(&rc);
    for (i = 0; i < origlen; i++) {
        uint32_t v = cd_rc_dec_getfreq(&rc, TOTAL);
        if (v >= TOTAL) v = TOTAL - 1;    /* 防御损坏输入 */
        s = slot[v];
        out[i] = (uint8_t)s;
        cd_rc_dec_update(&rc, cum[s], norm[s]);
    }
    free(slot);
    return (int64_t)origlen;
}

/* order-0 字节区间编码输出上界：与 rANS 同量级（头 521 + 负载 ≈ 原长 + 收尾）。 */
uint64_t codec_range_bound(uint64_t len) {
    return CD_RANS_HDR + len + (len >> 1) + 64u;
}

/* ====================== Layer 1 · 簇 7：LZW 字典编码 ======================
 *
 * 变长码 LZW（与 GIF/Unix compress 同族）：与 DEFLATE 并列的字典编码原子。
 * 码宽自 9 位起、随字典增长至 12 位（4096 项），满表时发 CLEAR(256) 复位重学；
 * EOI(257) 标识流尾。位流 LSB-first。沿用 compress 的码宽切换纪律——编码侧在
 * 新增词条后按 freeent>maxcode 升宽，解码侧滞后一码、故读码前按 freeent>=maxcode
 * 预升宽，两者逐码同步。自描述布局：原长(8,LE) + LZW 位流。仅本模块自洽。
 */

#define CD_LZW_CLR     256
#define CD_LZW_EOI     257
#define CD_LZW_FIRST   258
#define CD_LZW_MINBITS 9
#define CD_LZW_MAXBITS 12
#define CD_LZW_MAXENT  4096            /* 1 << CD_LZW_MAXBITS */
#define CD_LZW_HSIZE   9001            /* 开放寻址哈希表大小（素数 > MAXENT） */

/* LZW 编码输出上界：每符号至多 1 个 ≤12 位码（≤1.5 字节），len*2 留足余量。 */
uint64_t codec_lzw_bound(uint64_t len) {
    return 8u + len * 2u + 16u;
}

/* LZW 编码。布局：原长(8,LE) + LZW 变长位流（首发 CLEAR、尾发 EOI）。
 * 返回写入字节数；cap 不足返回 -1。len==0 仅写 8 字节头。 */
int64_t codec_lzw_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    int32_t *htab = NULL, *codetab = NULL;
    uint64_t i, o = 8;
    uint32_t acc = 0;
    int nbits = 0;
    int width = CD_LZW_MINBITS, maxcode = (1 << CD_LZW_MINBITS) - 1, freeent = CD_LZW_FIRST;
    int32_t ent;
    int j;
    int64_t ret = -1;
    if (cap < 8) return -1;
    for (i = 0; i < 8; i++) out[i] = (uint8_t)((len >> (i * 8)) & 0xff);
    if (len == 0) return 8;
    htab    = (int32_t *)malloc(sizeof(int32_t) * CD_LZW_HSIZE);
    codetab = (int32_t *)malloc(sizeof(int32_t) * CD_LZW_HSIZE);
    if (!htab || !codetab) goto done;

#define LZW_PUT(code) do { \
        acc |= (uint32_t)(code) << nbits; nbits += width; \
        while (nbits >= 8) { if (o >= cap) goto done; out[o++] = (uint8_t)(acc & 0xff); acc >>= 8; nbits -= 8; } \
    } while (0)

    for (j = 0; j < CD_LZW_HSIZE; j++) htab[j] = -1;
    LZW_PUT(CD_LZW_CLR);
    ent = p[0];
    for (i = 1; i < len; i++) {
        int k = p[i];
        int32_t fcode = ((int32_t)k << 12) | ent;       /* ent<4096 → 键唯一 */
        uint32_t h = ((uint32_t)fcode * 2654435761u) % CD_LZW_HSIZE;
        while (htab[h] != -1) {
            if (htab[h] == fcode) { ent = codetab[h]; goto matched; }
            if (++h == CD_LZW_HSIZE) h = 0;
        }
        LZW_PUT(ent);
        if (freeent < CD_LZW_MAXENT) {
            htab[h] = fcode; codetab[h] = freeent; freeent++;
            if (freeent > maxcode && width < CD_LZW_MAXBITS) { width++; maxcode = (1 << width) - 1; }
        } else {
            LZW_PUT(CD_LZW_CLR);
            for (j = 0; j < CD_LZW_HSIZE; j++) htab[j] = -1;
            width = CD_LZW_MINBITS; maxcode = (1 << CD_LZW_MINBITS) - 1; freeent = CD_LZW_FIRST;
        }
        ent = k;
      matched: ;
    }
    LZW_PUT(ent);
    LZW_PUT(CD_LZW_EOI);
    while (nbits > 0) { if (o >= cap) goto done; out[o++] = (uint8_t)(acc & 0xff); acc >>= 8; nbits -= 8; }
    ret = (int64_t)o;
#undef LZW_PUT
done:
    free(htab); free(codetab);
    return ret;
}

/* LZW 解码（codec_lzw_encode 的逆）。返回原始字节数；失败 / cap 不足返回 -1。 */
int64_t codec_lzw_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap) {
    const uint8_t *p = (const uint8_t *)src;
    uint16_t *pfx = NULL;
    uint8_t  *sfx = NULL, *stk = NULL;
    uint64_t origlen = 0, i, o = 0, ip = 8;
    uint32_t acc = 0;
    int nbits = 0;
    int width = CD_LZW_MINBITS, maxcode = (1 << CD_LZW_MINBITS) - 1, freeent = CD_LZW_FIRST;
    int oldcode = -1, firstByte = 0;
    int64_t ret = -1;
    if (len < 8) return -1;
    for (i = 0; i < 8; i++) origlen |= (uint64_t)p[i] << (i * 8);
    if (origlen == 0) return 0;
    if (origlen > cap) return -1;
    pfx = (uint16_t *)malloc(sizeof(uint16_t) * CD_LZW_MAXENT);
    sfx = (uint8_t  *)malloc(CD_LZW_MAXENT);
    stk = (uint8_t  *)malloc(CD_LZW_MAXENT + 1);
    if (!pfx || !sfx || !stk) goto done;
    for (;;) {
        int code, sp, cur, incode;
        /* getcode：滞后一码，读码前预升宽（freeent>=maxcode） */
        if (freeent >= maxcode && width < CD_LZW_MAXBITS) { width++; maxcode = (1 << width) - 1; }
        while (nbits < width) {
            if (ip >= len) { code = -1; goto gotcode; }
            acc |= (uint32_t)p[ip++] << nbits; nbits += 8;
        }
        code = (int)(acc & ((1u << width) - 1)); acc >>= width; nbits -= width;
      gotcode:
        if (code < 0 || code == CD_LZW_EOI) break;
        if (code == CD_LZW_CLR) {
            width = CD_LZW_MINBITS; maxcode = (1 << CD_LZW_MINBITS) - 1; freeent = CD_LZW_FIRST;
            oldcode = -1;
            continue;
        }
        if (oldcode < 0) {                       /* CLEAR 后首码：必为字面 */
            if (code >= 256) goto done;
            if (o >= cap) goto done;
            out[o++] = (uint8_t)code; firstByte = code; oldcode = code;
            continue;
        }
        incode = code; sp = 0;
        if (code >= freeent) {                   /* KwKwK 特例 */
            if (code > freeent) goto done;       /* 损坏：仅允许 == freeent */
            stk[sp++] = (uint8_t)firstByte;
            cur = oldcode;
        } else cur = code;
        while (cur >= 256) {
            if (sp > CD_LZW_MAXENT) goto done;
            stk[sp++] = sfx[cur]; cur = pfx[cur];
        }
        firstByte = cur;
        stk[sp++] = (uint8_t)cur;
        if (o + (uint64_t)sp > cap) goto done;
        while (sp > 0) out[o++] = stk[--sp];
        if (freeent < CD_LZW_MAXENT) {           /* 新增词条 oldcode→(oldcode,firstByte) */
            pfx[freeent] = (uint16_t)oldcode; sfx[freeent] = (uint8_t)firstByte; freeent++;
        }
        oldcode = incode;
    }
    if (o != origlen) goto done;                 /* 长度自检 */
    ret = (int64_t)o;
done:
    free(pfx); free(sfx); free(stk);
    return ret;
}

/* ---- 裸 LZW 子流（无长度头，码宽 / 字典约定由调用方按图片容器给定）----
 *
 * 上面的 codec_lzw_* 自带 8 字节原长头、只能本模块自洽编解；图片容器（TIFF、GIF）
 * 里的 LZW 没有这个头，靠容器另给的行/条信息定边界。本对函数即为容器准备：
 * 不写长度头，解码读到 EOI(257) 或输入耗尽即停、产物以 cap 封顶。
 *
 * flags 位 0：位流字节序——1 = MSB-first（TIFF 习惯），0 = LSB-first。
 * 其余约定与本模块 LZW 相同：码宽 9→12、CLEAR(256) 满表复位、EOI(257) 收尾，
 * 编码侧新增词条后按 freeent>maxcode 升宽、解码侧滞后一码读码前预升宽。
 * MSB-first + 此升宽纪律已对真实 libtiff（Pillow）输出双向验证通过。 */
int64_t codec_lzw_raw_encode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t flags) {
    const uint8_t *p = (const uint8_t *)src;
    int msb = flags & 1;
    int32_t *htab = NULL, *codetab = NULL;
    uint64_t i, o = 0;
    uint32_t acc = 0;
    int nbits = 0;
    int width = CD_LZW_MINBITS, maxcode = (1 << CD_LZW_MINBITS) - 1, freeent = CD_LZW_FIRST;
    int32_t ent;
    int j;
    int64_t ret = -1;
    if (len == 0) return 0;
    htab    = (int32_t *)malloc(sizeof(int32_t) * CD_LZW_HSIZE);
    codetab = (int32_t *)malloc(sizeof(int32_t) * CD_LZW_HSIZE);
    if (!htab || !codetab) goto done;

#define LZWR_PUT(code) do { \
        if (msb) { \
            acc |= (uint32_t)(code) << (32 - nbits - width); nbits += width; \
            while (nbits >= 8) { if (o >= cap) goto done; out[o++] = (uint8_t)(acc >> 24); acc <<= 8; nbits -= 8; } \
        } else { \
            acc |= (uint32_t)(code) << nbits; nbits += width; \
            while (nbits >= 8) { if (o >= cap) goto done; out[o++] = (uint8_t)(acc & 0xff); acc >>= 8; nbits -= 8; } \
        } \
    } while (0)

    for (j = 0; j < CD_LZW_HSIZE; j++) htab[j] = -1;
    LZWR_PUT(CD_LZW_CLR);
    ent = p[0];
    for (i = 1; i < len; i++) {
        int k = p[i];
        int32_t fcode = ((int32_t)k << 12) | ent;
        uint32_t h = ((uint32_t)fcode * 2654435761u) % CD_LZW_HSIZE;
        while (htab[h] != -1) {
            if (htab[h] == fcode) { ent = codetab[h]; goto matched; }
            if (++h == CD_LZW_HSIZE) h = 0;
        }
        LZWR_PUT(ent);
        if (freeent < CD_LZW_MAXENT - 1) {              /* 留出 CLEAR/EOI，满表前发 CLEAR */
            htab[h] = fcode; codetab[h] = freeent; freeent++;
            if (freeent > maxcode && width < CD_LZW_MAXBITS) { width++; maxcode = (1 << width) - 1; }
        } else {
            LZWR_PUT(CD_LZW_CLR);
            for (j = 0; j < CD_LZW_HSIZE; j++) htab[j] = -1;
            width = CD_LZW_MINBITS; maxcode = (1 << CD_LZW_MINBITS) - 1; freeent = CD_LZW_FIRST;
        }
        ent = k;
      matched: ;
    }
    LZWR_PUT(ent);
    LZWR_PUT(CD_LZW_EOI);
    if (msb) { while (nbits > 0) { if (o >= cap) goto done; out[o++] = (uint8_t)(acc >> 24); acc <<= 8; nbits -= 8; } }
    else     { while (nbits > 0) { if (o >= cap) goto done; out[o++] = (uint8_t)(acc & 0xff); acc >>= 8; nbits -= 8; } }
    ret = (int64_t)o;
#undef LZWR_PUT
done:
    free(htab); free(codetab);
    return ret;
}

/* 裸 LZW 解码（codec_lzw_raw_encode 的逆）。读到 EOI 或输入耗尽即停。
 * 返回写入字节数；cap 不足 / 码流损坏返回 -1。 */
int64_t codec_lzw_raw_decode(void *src, uint64_t len, uint8_t *out, uint64_t cap, int32_t flags) {
    const uint8_t *p = (const uint8_t *)src;
    int msb = flags & 1;
    uint16_t *pfx = NULL;
    uint8_t  *sfx = NULL, *stk = NULL;
    uint64_t o = 0, ip = 0;
    uint32_t acc = 0;
    int nbits = 0;
    int width = CD_LZW_MINBITS, maxcode = (1 << CD_LZW_MINBITS) - 1, freeent = CD_LZW_FIRST;
    int oldcode = -1, firstByte = 0;
    int64_t ret = -1;
    pfx = (uint16_t *)malloc(sizeof(uint16_t) * CD_LZW_MAXENT);
    sfx = (uint8_t  *)malloc(CD_LZW_MAXENT);
    stk = (uint8_t  *)malloc(CD_LZW_MAXENT + 1);
    if (!pfx || !sfx || !stk) goto done;
    for (;;) {
        int code, sp, cur, incode;
        if (freeent >= maxcode && width < CD_LZW_MAXBITS) { width++; maxcode = (1 << width) - 1; }
        if (msb) {
            while (nbits < width) {
                if (ip >= len) { code = -1; goto gotcode; }
                acc = (acc << 8) | p[ip++]; nbits += 8;
            }
            nbits -= width;
            code = (int)((acc >> nbits) & ((1u << width) - 1));
            acc &= (nbits > 0) ? ((1u << nbits) - 1) : 0u;
        } else {
            while (nbits < width) {
                if (ip >= len) { code = -1; goto gotcode; }
                acc |= (uint32_t)p[ip++] << nbits; nbits += 8;
            }
            code = (int)(acc & ((1u << width) - 1)); acc >>= width; nbits -= width;
        }
      gotcode:
        if (code < 0 || code == CD_LZW_EOI) break;
        if (code == CD_LZW_CLR) {
            width = CD_LZW_MINBITS; maxcode = (1 << CD_LZW_MINBITS) - 1; freeent = CD_LZW_FIRST;
            oldcode = -1;
            continue;
        }
        if (oldcode < 0) {
            if (code >= 256) goto done;
            if (o >= cap) goto done;
            out[o++] = (uint8_t)code; firstByte = code; oldcode = code;
            continue;
        }
        incode = code; sp = 0;
        if (code >= freeent) {
            if (code > freeent) goto done;
            stk[sp++] = (uint8_t)firstByte;
            cur = oldcode;
        } else cur = code;
        while (cur >= 256) {
            if (sp > CD_LZW_MAXENT) goto done;
            stk[sp++] = sfx[cur]; cur = pfx[cur];
        }
        firstByte = cur;
        stk[sp++] = (uint8_t)cur;
        if (o + (uint64_t)sp > cap) goto done;
        while (sp > 0) out[o++] = stk[--sp];
        if (freeent < CD_LZW_MAXENT) {
            pfx[freeent] = (uint16_t)oldcode; sfx[freeent] = (uint8_t)firstByte; freeent++;
        }
        oldcode = incode;
    }
    ret = (int64_t)o;
done:
    free(pfx); free(sfx); free(stk);
    return ret;
}

/* ====================== 工具 · 簇 8：变长整数（LEB128 / ZigZag）======================
 *
 * 无符号 LEB128：每字节低 7 位载荷、高位为续接标志，小端序；u64 至多 10 字节。
 * ZigZag：把有符号整数折叠为无符号（小绝对值→小编码），与 LEB128 配合编码有符号量。
 * 用途：protobuf / DWARF / WebAssembly / SQLite 等的紧凑整数原语。
 */

/* 无符号 LEB128 编码。out 须有 ≥10 字节余量；返回写入字节数（1..10）。 */
int32_t codec_varint_encode(uint64_t value, uint8_t *out) {
    int32_t n = 0;
    do {
        uint8_t b = (uint8_t)(value & 0x7f);
        value >>= 7;
        if (value) b |= 0x80;
        out[n++] = b;
    } while (value);
    return n;
}

/* 无符号 LEB128 解码。读 src[0..len) 的一个变长整数到 *value；
 * 返回消耗字节数（1..10）；截断 / 超过 10 字节返回 -1。 */
int32_t codec_varint_decode(void *src, uint64_t len, uint64_t *value) {
    const uint8_t *p = (const uint8_t *)src;
    uint64_t v = 0;
    int32_t n = 0, shift = 0;
    while ((uint64_t)n < len && n < 10) {
        uint8_t b = p[n];
        v |= (uint64_t)(b & 0x7f) << shift;
        n++;
        if (!(b & 0x80)) { *value = v; return n; }
        shift += 7;
    }
    return -1;
}

/* ZigZag 编码：有符号 → 无符号（0,-1,1,-2,2 → 0,1,2,3,4）。 */
uint64_t codec_zigzag_encode(int64_t v) {
    return ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
}

/* ZigZag 解码：无符号 → 有符号（codec_zigzag_encode 的逆）。 */
int64_t codec_zigzag_decode(uint64_t v) {
    return (int64_t)(v >> 1) ^ -(int64_t)(v & 1);
}
