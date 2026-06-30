/* crypto_impl.c —— sc crypto 内置模块默认实现（自含、零外部库链接）
 *
 * 第一期：SHA-256（FIPS 180-4）、HMAC-SHA256（RFC 2104）、HKDF-SHA256（RFC 5869）、
 *   SHA-1（FIPS 180-1）、Base64（RFC 4648）。
 * 第二期·批1：ChaCha20 / Poly1305 / ChaCha20-Poly1305 AEAD（RFC 8439）、
 *   X25519（RFC 7748，移植自公有领域 TweetNaCl）。
 * 第二期·批2：SHA-512/384（FIPS 180-4）、HMAC-SHA512、PBKDF2-HMAC-SHA256（RFC 8018）、
 *   SHA3-256/512 与 SHAKE128/256（FIPS 202）。
 * 第二期·批3：AES-128/256 的 CTR/CBC/GCM（FIPS 197、SP 800-38A/38D）、
 *   Ed25519 签名（RFC 8032，复用批1 的 fe 域算术）。
 * 全部按官方规范实现，由 tests/cases/crypto_test.sc 的 NIST/RFC KAT 向量验证。
 * 跨平台基础（stdint/string 等）经 builtins/platform.h 引入。
 */
#include "platform.h"
#include "crypto.h"

/* ====================== SHA-256（FIPS 180-4）====================== */

typedef struct {
    uint32_t h[8];
    uint64_t total;     /* 已吸收的总字节数 */
    uint8_t  buf[64];
    uint32_t n;         /* buf 中暂存字节数 */
} sha256_ctx;

static const uint32_t SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define SC_ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_compress(sha256_ctx *c, const uint8_t *p) {
    uint32_t w[64];
    uint32_t a, b, cc, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16)
             | ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = SC_ROR32(w[i - 15], 7) ^ SC_ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SC_ROR32(w[i - 2], 17) ^ SC_ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = SC_ROR32(e, 6) ^ SC_ROR32(e, 11) ^ SC_ROR32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = SC_ROR32(a, 2) ^ SC_ROR32(a, 13) ^ SC_ROR32(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(sha256_ctx *c) {
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->total = 0; c->n = 0;
}

static void sha256_update(sha256_ctx *c, const uint8_t *p, size_t len) {
    c->total += len;
    while (len) {
        size_t take = 64 - c->n;
        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += (uint32_t)take; p += take; len -= take;
        if (c->n == 64) { sha256_compress(c, c->buf); c->n = 0; }
    }
}

static void sha256_final(sha256_ctx *c, uint8_t *out) {
    uint64_t bits = c->total * 8u;
    int i;
    c->buf[c->n++] = 0x80;
    if (c->n > 56) {
        while (c->n < 64) c->buf[c->n++] = 0;
        sha256_compress(c, c->buf);
        c->n = 0;
    }
    while (c->n < 56) c->buf[c->n++] = 0;
    for (i = 0; i < 8; i++)
        c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_compress(c, c->buf);
    for (i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->h[i]);
    }
}

void crypto_sha256(void *data, uint64_t len, uint8_t *out) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)data, (size_t)len);
    sha256_final(&c, out);
}

/* ====================== HMAC-SHA256（RFC 2104）====================== */

typedef struct {
    sha256_ctx inner;
    uint8_t    opad[64];
} hmac_ctx;

static void hmac_init(hmac_ctx *m, const uint8_t *key, size_t keylen) {
    uint8_t k[64];
    uint8_t ipad[64];
    int i;
    memset(k, 0, 64);
    if (keylen > 64) crypto_sha256((void *)key, keylen, k);
    else             memcpy(k, key, keylen);
    for (i = 0; i < 64; i++) {
        ipad[i]    = k[i] ^ 0x36;
        m->opad[i] = k[i] ^ 0x5c;
    }
    sha256_init(&m->inner);
    sha256_update(&m->inner, ipad, 64);
}

static void hmac_update(hmac_ctx *m, const uint8_t *p, size_t n) {
    sha256_update(&m->inner, p, n);
}

static void hmac_final(hmac_ctx *m, uint8_t *out) {
    uint8_t ih[32];
    sha256_ctx oc;
    sha256_final(&m->inner, ih);
    sha256_init(&oc);
    sha256_update(&oc, m->opad, 64);
    sha256_update(&oc, ih, 32);
    sha256_final(&oc, out);
}

void crypto_hmac_sha256(void *key, uint64_t keylen,
                        void *data, uint64_t len, uint8_t *out) {
    hmac_ctx m;
    hmac_init(&m, (const uint8_t *)key, (size_t)keylen);
    hmac_update(&m, (const uint8_t *)data, (size_t)len);
    hmac_final(&m, out);
}

/* ====================== HKDF-SHA256（RFC 5869）====================== */

void crypto_hkdf_sha256(void *salt, uint64_t saltlen,
                        void *ikm,  uint64_t ikmlen,
                        void *info, uint64_t infolen,
                        uint8_t *out, uint64_t outlen) {
    uint8_t prk[32];
    uint8_t t[32];
    uint8_t zero[32];
    size_t tlen = 0, done = 0;
    uint8_t counter = 1;

    /* Extract：PRK = HMAC(salt, IKM)；salt 缺省为 HashLen 个 0 */
    if (salt == 0 || saltlen == 0) {
        memset(zero, 0, 32);
        crypto_hmac_sha256(zero, 32, ikm, ikmlen, prk);
    } else {
        crypto_hmac_sha256(salt, saltlen, ikm, ikmlen, prk);
    }

    /* Expand：T(i) = HMAC(PRK, T(i-1) | info | i) */
    while (done < outlen) {
        hmac_ctx m;
        size_t take;
        hmac_init(&m, prk, 32);
        if (tlen) hmac_update(&m, t, tlen);
        if (info && infolen) hmac_update(&m, (const uint8_t *)info, (size_t)infolen);
        hmac_update(&m, &counter, 1);
        hmac_final(&m, t);
        tlen = 32;
        take = (size_t)(outlen - done);
        if (take > 32) take = 32;
        memcpy(out + done, t, take);
        done += take;
        counter++;
    }
}

/* ====================== SHA-1（FIPS 180-1）======================
 * 注意：SHA-1 已不具碰撞抗性，仅用于 WebSocket 握手（RFC 6455）等遗留协议
 *   的互通，切勿用于新的安全用途。 */

#define SC_ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_compress(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16)
             | ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = SC_ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);              k = 0x5a827999u; }
        else if (i < 40) { f = b ^ c ^ d;                         k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);       k = 0x8f1bbcdcu; }
        else             { f = b ^ c ^ d;                         k = 0xca62c1d6u; }
        uint32_t t = SC_ROL32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = SC_ROL32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void crypto_sha1(void *data, uint64_t len, uint8_t *out) {
    uint32_t h[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
    const uint8_t *p = (const uint8_t *)data;
    uint64_t total = (uint64_t)len;
    uint8_t block[64];
    uint64_t off = 0;
    uint64_t bits = total * 8u;
    int i;

    while (total - off >= 64) { sha1_compress(h, p + off); off += 64; }

    /* 末块填充：剩余 + 0x80 + 0 + 64-bit 大端长度 */
    {
        size_t rem = (size_t)(total - off);
        memcpy(block, p + off, rem);
        block[rem++] = 0x80;
        if (rem > 56) {
            while (rem < 64) block[rem++] = 0;
            sha1_compress(h, block);
            rem = 0;
        }
        while (rem < 56) block[rem++] = 0;
        for (i = 0; i < 8; i++) block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
        sha1_compress(h, block);
    }
    for (i = 0; i < 5; i++) {
        out[i * 4 + 0] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

/* ====================== Base64 编码（RFC 4648）======================
 * data[0..len) -> out（不补 0），返回写入的字符数（((len+2)/3)*4）。 */

int32_t crypto_base64(void *data, uint64_t len, char *out) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *d = (const uint8_t *)data;
    uint64_t i = 0;
    int32_t o = 0;
    while (i + 3 <= len) {
        uint32_t n = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8) | (uint32_t)d[i + 2];
        out[o++] = tab[(n >> 18) & 0x3F];
        out[o++] = tab[(n >> 12) & 0x3F];
        out[o++] = tab[(n >> 6) & 0x3F];
        out[o++] = tab[n & 0x3F];
        i += 3;
    }
    if (len - i == 1) {
        uint32_t n = (uint32_t)d[i] << 16;
        out[o++] = tab[(n >> 18) & 0x3F];
        out[o++] = tab[(n >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (len - i == 2) {
        uint32_t n = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8);
        out[o++] = tab[(n >> 18) & 0x3F];
        out[o++] = tab[(n >> 12) & 0x3F];
        out[o++] = tab[(n >> 6) & 0x3F];
        out[o++] = '=';
    }
    return o;
}

/* ================================================================
 * 第二期 · 批1：ChaCha20 / Poly1305 / AEAD（RFC 8439）、X25519（RFC 7748）
 * ================================================================ */

/* 小端整数读写助手（ChaCha20 / Poly1305 共用） */
static uint32_t sc_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void sc_st64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* ====================== ChaCha20（RFC 8439 §2.4）====================== */
/* 复用 SHA-1 节定义的 SC_ROL32 作 32 位循环左移。 */

#define SC_QR(a, b, c, d)                       \
    a += b; d ^= a; d = SC_ROL32(d, 16);        \
    c += d; b ^= c; b = SC_ROL32(b, 12);        \
    a += b; d ^= a; d = SC_ROL32(d, 8);         \
    c += d; b ^= c; b = SC_ROL32(b, 7)

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    int i;
    for (i = 0; i < 16; i++) x[i] = in[i];
    for (i = 0; i < 10; i++) {           /* 20 轮 = 10 次双轮 */
        SC_QR(x[0], x[4], x[8],  x[12]);
        SC_QR(x[1], x[5], x[9],  x[13]);
        SC_QR(x[2], x[6], x[10], x[14]);
        SC_QR(x[3], x[7], x[11], x[15]);
        SC_QR(x[0], x[5], x[10], x[15]);
        SC_QR(x[1], x[6], x[11], x[12]);
        SC_QR(x[2], x[7], x[8],  x[13]);
        SC_QR(x[3], x[4], x[9],  x[14]);
    }
    for (i = 0; i < 16; i++) {
        uint32_t v = x[i] + in[i];
        out[i * 4 + 0] = (uint8_t)(v);
        out[i * 4 + 1] = (uint8_t)(v >> 8);
        out[i * 4 + 2] = (uint8_t)(v >> 16);
        out[i * 4 + 3] = (uint8_t)(v >> 24);
    }
}

static void chacha20_init(uint32_t st[16], const uint8_t *key,
                          const uint8_t *nonce, uint32_t counter) {
    int i;
    st[0] = 0x61707865u; st[1] = 0x3320646eu;   /* "expand 32-byte k" */
    st[2] = 0x79622d32u; st[3] = 0x6b206574u;
    for (i = 0; i < 8; i++) st[4 + i] = sc_le32(key + i * 4);
    st[12] = counter;
    st[13] = sc_le32(nonce + 0);
    st[14] = sc_le32(nonce + 4);
    st[15] = sc_le32(nonce + 8);
}

void crypto_chacha20(uint8_t *key, uint8_t *nonce, uint32_t counter,
                     void *data, uint64_t len, uint8_t *out) {
    uint32_t st[16];
    uint8_t  ks[64];
    const uint8_t *in = (const uint8_t *)data;
    uint64_t i = 0;
    chacha20_init(st, key, nonce, counter);
    while (i < len) {
        size_t j;
        size_t take = (size_t)(len - i);
        if (take > 64) take = 64;
        chacha20_block(st, ks);
        for (j = 0; j < take; j++) out[i + j] = in[i + j] ^ ks[j];
        st[12]++;                         /* 块计数自增 */
        i += take;
    }
}

/* ====================== Poly1305（RFC 8439 §2.5）======================
 * 32 位实现：130-bit 累加器以 5×26-bit 肢体表示（poly1305-donna 风格）。 */

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t  buffer[16];
    size_t   leftover;
    uint32_t final;
} poly1305_ctx;

static void poly1305_init(poly1305_ctx *st, const uint8_t key[32]) {
    uint32_t t0 = sc_le32(key + 0);
    uint32_t t1 = sc_le32(key + 4);
    uint32_t t2 = sc_le32(key + 8);
    uint32_t t3 = sc_le32(key + 12);
    /* r &= 0xffffffc0ffffffc0ffffffc0fffffff，按 26-bit 肢体切分 */
    st->r[0] = (t0)                       & 0x3ffffffu;
    st->r[1] = ((t0 >> 26) | (t1 <<  6))  & 0x3ffff03u;
    st->r[2] = ((t1 >> 20) | (t2 << 12))  & 0x3ffc0ffu;
    st->r[3] = ((t2 >> 14) | (t3 << 18))  & 0x3f03fffu;
    st->r[4] = ((t3 >>  8))               & 0x00fffffu;
    st->h[0] = st->h[1] = st->h[2] = st->h[3] = st->h[4] = 0;
    st->pad[0] = sc_le32(key + 16);
    st->pad[1] = sc_le32(key + 20);
    st->pad[2] = sc_le32(key + 24);
    st->pad[3] = sc_le32(key + 28);
    st->leftover = 0;
    st->final = 0;
}

/* 处理整数个 16 字节块（bytes 须为 16 的倍数）。 */
static void poly1305_blocks(poly1305_ctx *st, const uint8_t *m, size_t bytes) {
    const uint32_t hibit = st->final ? 0 : (1u << 24);  /* 2^128 标记位 */
    uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

    while (bytes >= 16) {
        uint64_t d0, d1, d2, d3, d4;
        uint32_t c;
        /* h += m */
        h0 += (sc_le32(m +  0))      & 0x3ffffffu;
        h1 += (sc_le32(m +  3) >> 2) & 0x3ffffffu;
        h2 += (sc_le32(m +  6) >> 4) & 0x3ffffffu;
        h3 += (sc_le32(m +  9) >> 6) & 0x3ffffffu;
        h4 += (sc_le32(m + 12) >> 8) | hibit;
        /* h *= r（mod 2^130-5） */
        d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
        /* 部分归约 */
                     c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffffu;
        d1 += c;     c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffffu;
        d2 += c;     c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffffu;
        d3 += c;     c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffffu;
        d4 += c;     c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffffu;
        h0 += c * 5; c = (h0 >> 26);           h0 = h0 & 0x3ffffffu;
        h1 += c;
        m += 16; bytes -= 16;
    }
    st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

static void poly1305_update(poly1305_ctx *st, const uint8_t *m, size_t bytes) {
    /* 先填满 leftover 暂存 */
    if (st->leftover) {
        size_t want = 16 - st->leftover;
        if (want > bytes) want = bytes;
        memcpy(st->buffer + st->leftover, m, want);
        st->leftover += want; m += want; bytes -= want;
        if (st->leftover < 16) return;
        poly1305_blocks(st, st->buffer, 16);
        st->leftover = 0;
    }
    if (bytes >= 16) {
        size_t want = bytes & ~(size_t)15;
        poly1305_blocks(st, m, want);
        m += want; bytes -= want;
    }
    if (bytes) {
        memcpy(st->buffer + st->leftover, m, bytes);
        st->leftover += bytes;
    }
}

static void poly1305_finish(poly1305_ctx *st, uint8_t mac[16]) {
    uint32_t h0, h1, h2, h3, h4, c;
    uint32_t g0, g1, g2, g3, g4, mask;
    uint64_t f;

    /* 末块（带 0x01 标记）补零后处理 */
    if (st->leftover) {
        size_t i = st->leftover;
        st->buffer[i++] = 1;
        for (; i < 16; i++) st->buffer[i] = 0;
        st->final = 1;
        poly1305_blocks(st, st->buffer, 16);
    }
    /* 完全进位 */
    h0 = st->h[0]; h1 = st->h[1]; h2 = st->h[2]; h3 = st->h[3]; h4 = st->h[4];
                 c = h1 >> 26; h1 &= 0x3ffffffu;
    h2 += c;     c = h2 >> 26; h2 &= 0x3ffffffu;
    h3 += c;     c = h3 >> 26; h3 &= 0x3ffffffu;
    h4 += c;     c = h4 >> 26; h4 &= 0x3ffffffu;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffffu;
    h1 += c;
    /* 计算 h + -p */
    g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffffu;
    g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffffu;
    g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffffu;
    g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffffu;
    g4 = h4 + c - (1u << 26);
    /* 常量时间选择：h < p 取 h，否则取 h+(-p) */
    mask = (g4 >> 31) - 1u;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
    /* h %= 2^128 */
    h0 = ((h0)       | (h1 << 26)) & 0xffffffffu;
    h1 = ((h1 >>  6) | (h2 << 20)) & 0xffffffffu;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffffu;
    h3 = ((h3 >> 18) | (h4 <<  8)) & 0xffffffffu;
    /* mac = (h + pad) % 2^128 */
    f = (uint64_t)h0 + st->pad[0];             h0 = (uint32_t)f;
    f = (uint64_t)h1 + st->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + st->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + st->pad[3] + (f >> 32); h3 = (uint32_t)f;
    mac[0]  = (uint8_t)(h0);       mac[1]  = (uint8_t)(h0 >> 8);
    mac[2]  = (uint8_t)(h0 >> 16); mac[3]  = (uint8_t)(h0 >> 24);
    mac[4]  = (uint8_t)(h1);       mac[5]  = (uint8_t)(h1 >> 8);
    mac[6]  = (uint8_t)(h1 >> 16); mac[7]  = (uint8_t)(h1 >> 24);
    mac[8]  = (uint8_t)(h2);       mac[9]  = (uint8_t)(h2 >> 8);
    mac[10] = (uint8_t)(h2 >> 16); mac[11] = (uint8_t)(h2 >> 24);
    mac[12] = (uint8_t)(h3);       mac[13] = (uint8_t)(h3 >> 8);
    mac[14] = (uint8_t)(h3 >> 16); mac[15] = (uint8_t)(h3 >> 24);
}

void crypto_poly1305(uint8_t *key, void *data, uint64_t len, uint8_t *out) {
    poly1305_ctx st;
    poly1305_init(&st, key);
    poly1305_update(&st, (const uint8_t *)data, (size_t)len);
    poly1305_finish(&st, out);
}

/* ====================== ChaCha20-Poly1305 AEAD（RFC 8439 §2.8）====================== */

/* 一次性 Poly1305 密钥 = ChaCha20 块 0（counter=0）的前 32 字节。 */
static void aead_poly_key(const uint8_t *key, const uint8_t *nonce, uint8_t polykey[32]) {
    uint32_t st[16];
    uint8_t  ks[64];
    chacha20_init(st, key, nonce, 0);
    chacha20_block(st, ks);
    memcpy(polykey, ks, 32);
}

/* 累加 aad/密文及其 16 字节零填充 + le64 长度块到 MAC。 */
static void aead_mac(poly1305_ctx *mac,
                     const uint8_t *aad, uint64_t aadlen,
                     const uint8_t *ct,  uint64_t ctlen) {
    static const uint8_t zeros[16] = { 0 };
    uint8_t lenblock[16];
    if (aad && aadlen) {
        poly1305_update(mac, aad, (size_t)aadlen);
        if (aadlen % 16) poly1305_update(mac, zeros, 16 - (size_t)(aadlen % 16));
    }
    if (ctlen) {
        poly1305_update(mac, ct, (size_t)ctlen);
        if (ctlen % 16) poly1305_update(mac, zeros, 16 - (size_t)(ctlen % 16));
    }
    sc_st64(lenblock + 0, aadlen);
    sc_st64(lenblock + 8, ctlen);
    poly1305_update(mac, lenblock, 16);
}

void crypto_aead_seal(uint8_t *key, uint8_t *nonce,
                      void *aad, uint64_t aadlen,
                      void *plain, uint64_t plen,
                      uint8_t *cipher, uint8_t *tag) {
    uint8_t polykey[32];
    poly1305_ctx mac;
    aead_poly_key(key, nonce, polykey);
    crypto_chacha20(key, nonce, 1, plain, plen, cipher);   /* 数据从 counter=1 起 */
    poly1305_init(&mac, polykey);
    aead_mac(&mac, (const uint8_t *)aad, aadlen, cipher, plen);
    poly1305_finish(&mac, tag);
}

int32_t crypto_aead_open(uint8_t *key, uint8_t *nonce,
                         void *aad, uint64_t aadlen,
                         void *cipher, uint64_t clen,
                         uint8_t *tag, uint8_t *plain) {
    uint8_t polykey[32];
    uint8_t calc[16];
    poly1305_ctx mac;
    uint8_t diff = 0;
    int i;
    aead_poly_key(key, nonce, polykey);
    poly1305_init(&mac, polykey);
    aead_mac(&mac, (const uint8_t *)aad, aadlen, (const uint8_t *)cipher, clen);
    poly1305_finish(&mac, calc);
    /* 常量时间比较，先验证再解密 */
    for (i = 0; i < 16; i++) diff |= (uint8_t)(calc[i] ^ tag[i]);
    if (diff) return -1;
    crypto_chacha20(key, nonce, 1, cipher, clen, plain);
    return 0;
}

/* ====================== X25519（RFC 7748 §5）======================
 * 移植自公有领域 TweetNaCl：素域 GF(2^255-19) 上的 Montgomery 阶梯。
 * 域元素以 16×16-bit 肢体（int64_t）表示。 */

typedef int64_t fe[16];

static const fe fe_121665 = { 0xDB41, 1 };
static const uint8_t x25519_base9[32] = { 9 };

static void fe_carry(fe o) {
    int i;
    int64_t c;
    for (i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void fe_cswap(fe p, fe q, int b) {
    int i;
    int64_t t, c = ~((int64_t)b - 1);
    for (i = 0; i < 16; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void fe_pack(uint8_t *o, const fe n) {
    int i, j, b;
    fe m, t;
    for (i = 0; i < 16; i++) t[i] = n[i];
    fe_carry(t); fe_carry(t); fe_carry(t);
    for (j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        fe_cswap(t, m, 1 - b);
    }
    for (i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void fe_unpack(fe o, const uint8_t *n) {
    int i;
    for (i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void fe_add(fe o, const fe a, const fe b) { int i; for (i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fe_sub(fe o, const fe a, const fe b) { int i; for (i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void fe_mul(fe o, const fe a, const fe b) {
    int64_t i, j, t[31];
    for (i = 0; i < 31; i++) t[i] = 0;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; i++) o[i] = t[i];
    fe_carry(o); fe_carry(o);
}

static void fe_sqr(fe o, const fe a) { fe_mul(o, a, a); }

static void fe_inv(fe o, const fe i) {
    fe c;
    int a;
    for (a = 0; a < 16; a++) c[a] = i[a];
    for (a = 253; a >= 0; a--) {           /* x^(p-2)，p-2 = 2^255-21 */
        fe_sqr(c, c);
        if (a != 2 && a != 4) fe_mul(c, c, i);
    }
    for (a = 0; a < 16; a++) o[a] = c[a];
}

void crypto_x25519(uint8_t *out, uint8_t *scalar, uint8_t *point) {
    uint8_t z[32];
    int64_t x[80], r, i;
    fe a, b, c, d, e, f;
    for (i = 0; i < 31; i++) z[i] = scalar[i];
    z[31] = (scalar[31] & 127) | 64;       /* clamp 高位 */
    z[0] &= 248;                            /* clamp 低 3 位 */
    fe_unpack(x, point);
    for (i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (i = 254; i >= 0; --i) {
        r = (z[i >> 3] >> (i & 7)) & 1;
        fe_cswap(a, b, (int)r); fe_cswap(c, d, (int)r);
        fe_add(e, a, c); fe_sub(a, a, c);
        fe_add(c, b, d); fe_sub(b, b, d);
        fe_sqr(d, e); fe_sqr(f, a);
        fe_mul(a, c, a); fe_mul(c, b, e);
        fe_add(e, a, c); fe_sub(a, a, c);
        fe_sqr(b, a); fe_sub(c, d, f);
        fe_mul(a, c, fe_121665);
        fe_add(a, a, d); fe_mul(c, c, a);
        fe_mul(a, d, f); fe_mul(d, b, x);
        fe_sqr(b, e);
        fe_cswap(a, b, (int)r); fe_cswap(c, d, (int)r);
    }
    for (i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
    fe_inv(x + 32, x + 32);
    fe_mul(x + 16, x + 16, x + 32);
    fe_pack(out, x + 16);
}

void crypto_x25519_base(uint8_t *out, uint8_t *scalar) {
    crypto_x25519(out, scalar, (uint8_t *)x25519_base9);
}

/* ================================================================
 * 第二期 · 批2：SHA-512 家族（FIPS 180-4）/ PBKDF2（RFC 8018）/ SHA-3（FIPS 202）
 * ================================================================ */

/* ====================== SHA-512 / SHA-384（FIPS 180-4）====================== */

#define SC_ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static const uint64_t SHA512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

typedef struct {
    uint64_t h[8];
    uint64_t total;     /* 已吸收字节数（低 64 位，支持至 2^61 字节） */
    uint8_t  buf[128];
    uint32_t n;
} sha512_ctx;

static void sha512_compress(uint64_t h[8], const uint8_t *p) {
    uint64_t w[80];
    uint64_t a, b, c, d, e, f, g, hh;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint64_t)p[i*8]   << 56) | ((uint64_t)p[i*8+1] << 48)
             | ((uint64_t)p[i*8+2] << 40) | ((uint64_t)p[i*8+3] << 32)
             | ((uint64_t)p[i*8+4] << 24) | ((uint64_t)p[i*8+5] << 16)
             | ((uint64_t)p[i*8+6] <<  8) | ((uint64_t)p[i*8+7]);
    for (i = 16; i < 80; i++) {
        uint64_t s0 = SC_ROR64(w[i-15], 1) ^ SC_ROR64(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = SC_ROR64(w[i-2], 19) ^ SC_ROR64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 80; i++) {
        uint64_t S1 = SC_ROR64(e, 14) ^ SC_ROR64(e, 18) ^ SC_ROR64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t t1 = hh + S1 + ch + SHA512_K[i] + w[i];
        uint64_t S0 = SC_ROR64(a, 28) ^ SC_ROR64(a, 34) ^ SC_ROR64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha512_init(sha512_ctx *c, int is384) {
    if (is384) {
        c->h[0] = 0xcbbb9d5dc1059ed8ULL; c->h[1] = 0x629a292a367cd507ULL;
        c->h[2] = 0x9159015a3070dd17ULL; c->h[3] = 0x152fecd8f70e5939ULL;
        c->h[4] = 0x67332667ffc00b31ULL; c->h[5] = 0x8eb44a8768581511ULL;
        c->h[6] = 0xdb0c2e0d64f98fa7ULL; c->h[7] = 0x47b5481dbefa4fa4ULL;
    } else {
        c->h[0] = 0x6a09e667f3bcc908ULL; c->h[1] = 0xbb67ae8584caa73bULL;
        c->h[2] = 0x3c6ef372fe94f82bULL; c->h[3] = 0xa54ff53a5f1d36f1ULL;
        c->h[4] = 0x510e527fade682d1ULL; c->h[5] = 0x9b05688c2b3e6c1fULL;
        c->h[6] = 0x1f83d9abfb41bd6bULL; c->h[7] = 0x5be0cd19137e2179ULL;
    }
    c->total = 0; c->n = 0;
}

static void sha512_update(sha512_ctx *c, const uint8_t *p, size_t len) {
    c->total += len;
    while (len) {
        size_t take = 128 - c->n;
        if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += (uint32_t)take; p += take; len -= take;
        if (c->n == 128) { sha512_compress(c->h, c->buf); c->n = 0; }
    }
}

static void sha512_final(sha512_ctx *c, uint8_t *out, int words) {
    uint64_t bits = c->total * 8u;
    int i;
    c->buf[c->n++] = 0x80;
    if (c->n > 112) {
        while (c->n < 128) c->buf[c->n++] = 0;
        sha512_compress(c->h, c->buf);
        c->n = 0;
    }
    while (c->n < 112) c->buf[c->n++] = 0;
    for (i = 0; i < 8; i++) c->buf[112 + i] = 0;    /* 128 位长度高 64 位为 0 */
    for (i = 0; i < 8; i++) c->buf[120 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha512_compress(c->h, c->buf);
    for (i = 0; i < words; i++) {
        out[i*8+0] = (uint8_t)(c->h[i] >> 56); out[i*8+1] = (uint8_t)(c->h[i] >> 48);
        out[i*8+2] = (uint8_t)(c->h[i] >> 40); out[i*8+3] = (uint8_t)(c->h[i] >> 32);
        out[i*8+4] = (uint8_t)(c->h[i] >> 24); out[i*8+5] = (uint8_t)(c->h[i] >> 16);
        out[i*8+6] = (uint8_t)(c->h[i] >>  8); out[i*8+7] = (uint8_t)(c->h[i]);
    }
}

void crypto_sha512(void *data, uint64_t len, uint8_t *out) {
    sha512_ctx c;
    sha512_init(&c, 0);
    sha512_update(&c, (const uint8_t *)data, (size_t)len);
    sha512_final(&c, out, 8);
}

void crypto_sha384(void *data, uint64_t len, uint8_t *out) {
    sha512_ctx c;
    sha512_init(&c, 1);
    sha512_update(&c, (const uint8_t *)data, (size_t)len);
    sha512_final(&c, out, 6);
}

/* ====================== HMAC-SHA512（RFC 4231）====================== */

void crypto_hmac_sha512(void *key, uint64_t keylen,
                        void *data, uint64_t len, uint8_t *out) {
    uint8_t k[128], ipad[128], ih[64];
    sha512_ctx ic, oc;
    int i;
    memset(k, 0, 128);
    if (keylen > 128) crypto_sha512(key, keylen, k);    /* 长 key 先散列 */
    else              memcpy(k, key, (size_t)keylen);
    for (i = 0; i < 128; i++) ipad[i] = k[i] ^ 0x36;
    sha512_init(&ic, 0);
    sha512_update(&ic, ipad, 128);
    sha512_update(&ic, (const uint8_t *)data, (size_t)len);
    sha512_final(&ic, ih, 8);
    for (i = 0; i < 128; i++) k[i] ^= 0x5c;             /* k 现为 opad */
    sha512_init(&oc, 0);
    sha512_update(&oc, k, 128);
    sha512_update(&oc, ih, 64);
    sha512_final(&oc, out, 8);
}

/* ====================== PBKDF2-HMAC-SHA256（RFC 8018）======================
 * 复用 SHA-256 节的 hmac_ctx / hmac_init/update/final 流式接口，
 * 首块 U1 = HMAC(pw, salt ‖ INT32BE(i)) 无需拼接缓冲。 */

void crypto_pbkdf2_sha256(void *pw, uint64_t pwlen,
                          void *salt, uint64_t saltlen,
                          uint32_t iters, uint8_t *out, uint64_t outlen) {
    uint32_t blocks = (uint32_t)((outlen + 31) / 32);
    uint32_t b;
    uint64_t done = 0;
    for (b = 1; b <= blocks; b++) {
        uint8_t U[32], T[32], ctr[4];
        hmac_ctx hm;
        uint32_t j;
        int kk;
        size_t take;
        ctr[0] = (uint8_t)(b >> 24); ctr[1] = (uint8_t)(b >> 16);
        ctr[2] = (uint8_t)(b >> 8);  ctr[3] = (uint8_t)(b);
        hmac_init(&hm, (const uint8_t *)pw, (size_t)pwlen);
        hmac_update(&hm, (const uint8_t *)salt, (size_t)saltlen);
        hmac_update(&hm, ctr, 4);
        hmac_final(&hm, U);
        memcpy(T, U, 32);
        for (j = 1; j < iters; j++) {
            crypto_hmac_sha256(pw, pwlen, U, 32, U);
            for (kk = 0; kk < 32; kk++) T[kk] ^= U[kk];
        }
        take = (size_t)(outlen - done);
        if (take > 32) take = 32;
        memcpy(out + done, T, take);
        done += take;
    }
}

/* ====================== SHA-3 / SHAKE（FIPS 202）======================
 * Keccak-f[1600] 海绵结构；rate（字节）随变体而定，domain 为填充域分隔符。 */

#define SC_ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

static const uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};
static const int KECCAK_RHO[24] = {
     1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44
};
static const int KECCAK_PI[24] = {
    10,  7, 11, 17, 18,  3,  5, 16,  8, 21, 24,  4,
    15, 23, 19, 13, 12,  2, 20, 14, 22,  9,  6,  1
};

static void keccak_f(uint64_t st[25]) {
    int round, i, j;
    for (round = 0; round < 24; round++) {
        uint64_t bc[5], t;
        /* Theta */
        for (i = 0; i < 5; i++) bc[i] = st[i] ^ st[i+5] ^ st[i+10] ^ st[i+15] ^ st[i+20];
        for (i = 0; i < 5; i++) {
            t = bc[(i+4)%5] ^ SC_ROTL64(bc[(i+1)%5], 1);
            for (j = 0; j < 25; j += 5) st[j+i] ^= t;
        }
        /* Rho + Pi */
        t = st[1];
        for (i = 0; i < 24; i++) {
            j = KECCAK_PI[i];
            bc[0] = st[j];
            st[j] = SC_ROTL64(t, KECCAK_RHO[i]);
            t = bc[0];
        }
        /* Chi */
        for (j = 0; j < 25; j += 5) {
            for (i = 0; i < 5; i++) bc[i] = st[j+i];
            for (i = 0; i < 5; i++) st[j+i] ^= (~bc[(i+1)%5]) & bc[(i+2)%5];
        }
        /* Iota */
        st[0] ^= KECCAK_RC[round];
    }
}

static uint64_t keccak_load64(const uint8_t *p) {
    uint64_t r = 0;
    int i;
    for (i = 0; i < 8; i++) r |= (uint64_t)p[i] << (8 * i);
    return r;
}

static void keccak_sponge(const uint8_t *in, size_t inlen,
                          uint8_t *out, size_t outlen,
                          size_t rate, uint8_t domain) {
    uint64_t st[25];
    uint8_t block[200];
    size_t i;
    memset(st, 0, sizeof st);
    /* 吸收整块 */
    while (inlen >= rate) {
        for (i = 0; i < rate / 8; i++) st[i] ^= keccak_load64(in + i * 8);
        keccak_f(st);
        in += rate; inlen -= rate;
    }
    /* 末块填充：domain ‖ 0* ‖ 0x80（pad10*1） */
    memset(block, 0, rate);
    for (i = 0; i < inlen; i++) block[i] = in[i];
    block[inlen]  = domain;
    block[rate-1] |= 0x80;
    for (i = 0; i < rate / 8; i++) st[i] ^= keccak_load64(block + i * 8);
    keccak_f(st);
    /* 挤出 */
    while (outlen) {
        size_t n = outlen < rate ? outlen : rate;
        for (i = 0; i < n; i++) out[i] = (uint8_t)(st[i / 8] >> (8 * (i % 8)));
        out += n; outlen -= n;
        if (outlen) keccak_f(st);
    }
}

void crypto_sha3_256(void *data, uint64_t len, uint8_t *out) {
    keccak_sponge((const uint8_t *)data, (size_t)len, out, 32, 136, 0x06);
}
void crypto_sha3_512(void *data, uint64_t len, uint8_t *out) {
    keccak_sponge((const uint8_t *)data, (size_t)len, out, 64, 72, 0x06);
}
void crypto_shake128(void *data, uint64_t len, uint8_t *out, uint64_t outlen) {
    keccak_sponge((const uint8_t *)data, (size_t)len, out, (size_t)outlen, 168, 0x1F);
}
void crypto_shake256(void *data, uint64_t len, uint8_t *out, uint64_t outlen) {
    keccak_sponge((const uint8_t *)data, (size_t)len, out, (size_t)outlen, 136, 0x1F);
}

/* ================================================================
 * 第二期 · 批3：AES（CTR/CBC/GCM，FIPS 197 + SP 800-38A/D）/ Ed25519（RFC 8032）
 * ================================================================ */

/* ====================== AES 分组密码核心（FIPS 197）======================
 * 表驱动 SubBytes/逆 SubBytes；支持 AES-128 与 AES-256。
 * 注：S 盒查表非常量时间，存在缓存计时侧信道顾虑；如需抗侧信道请用
 *   ChaCha20-Poly1305。此处提供 AES 主要用于与既有协议互通。 */

static const uint8_t AES_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t AES_ISBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};
static const uint8_t AES_RCON[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static uint8_t aes_xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }

static uint8_t aes_gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        { uint8_t hi = a & 0x80; a = (uint8_t)(a << 1); if (hi) a ^= 0x1b; }
        b >>= 1;
    }
    return p;
}

/* 密钥扩展：rk 须 >= 60 字（AES-256 上限），nr 输出轮数（10 或 14）。 */
static void aes_expand(const uint8_t *key, int keybits, uint32_t *rk, int *nr) {
    int Nk = keybits / 32, Nr = Nk + 6, i, total = 4 * (Nr + 1);
    *nr = Nr;
    for (i = 0; i < Nk; i++)
        rk[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16)
              | ((uint32_t)key[4*i+2] << 8) | (uint32_t)key[4*i+3];
    for (i = Nk; i < total; i++) {
        uint32_t t = rk[i-1];
        if (i % Nk == 0) {
            t = ((uint32_t)AES_SBOX[(t>>16)&0xff] << 24) | ((uint32_t)AES_SBOX[(t>>8)&0xff] << 16)
              | ((uint32_t)AES_SBOX[t&0xff] << 8) | (uint32_t)AES_SBOX[(t>>24)&0xff];
            t ^= (uint32_t)AES_RCON[i/Nk] << 24;
        } else if (Nk > 6 && i % Nk == 4) {
            t = ((uint32_t)AES_SBOX[(t>>24)&0xff] << 24) | ((uint32_t)AES_SBOX[(t>>16)&0xff] << 16)
              | ((uint32_t)AES_SBOX[(t>>8)&0xff] << 8) | (uint32_t)AES_SBOX[t&0xff];
        }
        rk[i] = rk[i-Nk] ^ t;
    }
}

static void aes_add_rk(uint8_t s[16], const uint32_t *rk) {
    int c;
    for (c = 0; c < 4; c++) {
        s[4*c+0] ^= (uint8_t)(rk[c] >> 24); s[4*c+1] ^= (uint8_t)(rk[c] >> 16);
        s[4*c+2] ^= (uint8_t)(rk[c] >> 8);  s[4*c+3] ^= (uint8_t)(rk[c]);
    }
}
static void aes_subbytes(uint8_t s[16])     { int i; for (i = 0; i < 16; i++) s[i] = AES_SBOX[s[i]]; }
static void aes_inv_subbytes(uint8_t s[16]) { int i; for (i = 0; i < 16; i++) s[i] = AES_ISBOX[s[i]]; }

static void aes_shiftrows(uint8_t s[16]) {
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}
static void aes_inv_shiftrows(uint8_t s[16]) {
    uint8_t t;
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}
static void aes_mixcols(uint8_t s[16]) {
    int c;
    for (c = 0; c < 4; c++) {
        uint8_t *col = s + 4*c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        uint8_t t = a0 ^ a1 ^ a2 ^ a3;
        col[0] ^= t ^ aes_xtime(a0 ^ a1);
        col[1] ^= t ^ aes_xtime(a1 ^ a2);
        col[2] ^= t ^ aes_xtime(a2 ^ a3);
        col[3] ^= t ^ aes_xtime(a3 ^ a0);
    }
}
static void aes_inv_mixcols(uint8_t s[16]) {
    int c;
    for (c = 0; c < 4; c++) {
        uint8_t *col = s + 4*c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = aes_gmul(a0,14) ^ aes_gmul(a1,11) ^ aes_gmul(a2,13) ^ aes_gmul(a3,9);
        col[1] = aes_gmul(a0,9)  ^ aes_gmul(a1,14) ^ aes_gmul(a2,11) ^ aes_gmul(a3,13);
        col[2] = aes_gmul(a0,13) ^ aes_gmul(a1,9)  ^ aes_gmul(a2,14) ^ aes_gmul(a3,11);
        col[3] = aes_gmul(a0,11) ^ aes_gmul(a1,13) ^ aes_gmul(a2,9)  ^ aes_gmul(a3,14);
    }
}

static void aes_encrypt_block(const uint32_t *rk, int nr, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    int r;
    memcpy(s, in, 16);
    aes_add_rk(s, rk);
    for (r = 1; r < nr; r++) {
        aes_subbytes(s); aes_shiftrows(s); aes_mixcols(s); aes_add_rk(s, rk + 4*r);
    }
    aes_subbytes(s); aes_shiftrows(s); aes_add_rk(s, rk + 4*nr);
    memcpy(out, s, 16);
}
static void aes_decrypt_block(const uint32_t *rk, int nr, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    int r;
    memcpy(s, in, 16);
    aes_add_rk(s, rk + 4*nr);
    for (r = nr - 1; r >= 1; r--) {
        aes_inv_shiftrows(s); aes_inv_subbytes(s); aes_add_rk(s, rk + 4*r); aes_inv_mixcols(s);
    }
    aes_inv_shiftrows(s); aes_inv_subbytes(s); aes_add_rk(s, rk);
    memcpy(out, s, 16);
}

/* ====================== AES-CTR（SP 800-38A）====================== */
/* iv = 完整 16 字节初始计数块，整块按 128 位大端自增；加解密同一函数。 */
void crypto_aes_ctr(uint8_t *key, uint32_t keybits, uint8_t *iv,
                    void *data, uint64_t len, uint8_t *out) {
    uint32_t rk[60];
    int nr;
    uint8_t ctr[16], ks[16];
    const uint8_t *in = (const uint8_t *)data;
    uint64_t i = 0;
    aes_expand(key, (int)keybits, rk, &nr);
    memcpy(ctr, iv, 16);
    while (i < len) {
        size_t j, take = (size_t)(len - i);
        int k;
        if (take > 16) take = 16;
        aes_encrypt_block(rk, nr, ctr, ks);
        for (j = 0; j < take; j++) out[i + j] = in[i + j] ^ ks[j];
        for (k = 15; k >= 0; k--) { if (++ctr[k]) break; }
        i += take;
    }
}

/* ====================== AES-CBC（SP 800-38A）====================== */
/* len 须为 16 的倍数（不含填充，调用方自理 PKCS#7 等）。 */
void crypto_aes_cbc_encrypt(uint8_t *key, uint32_t keybits, uint8_t *iv,
                            void *data, uint64_t len, uint8_t *out) {
    uint32_t rk[60];
    int nr, j;
    uint8_t prev[16], blk[16];
    const uint8_t *in = (const uint8_t *)data;
    uint64_t off = 0;
    aes_expand(key, (int)keybits, rk, &nr);
    memcpy(prev, iv, 16);
    while (off + 16 <= len) {
        for (j = 0; j < 16; j++) blk[j] = in[off + j] ^ prev[j];
        aes_encrypt_block(rk, nr, blk, out + off);
        memcpy(prev, out + off, 16);
        off += 16;
    }
}
void crypto_aes_cbc_decrypt(uint8_t *key, uint32_t keybits, uint8_t *iv,
                            void *data, uint64_t len, uint8_t *out) {
    uint32_t rk[60];
    int nr, j;
    uint8_t prev[16], cur[16], dec[16];
    const uint8_t *in = (const uint8_t *)data;
    uint64_t off = 0;
    aes_expand(key, (int)keybits, rk, &nr);
    memcpy(prev, iv, 16);
    while (off + 16 <= len) {
        memcpy(cur, in + off, 16);
        aes_decrypt_block(rk, nr, cur, dec);
        for (j = 0; j < 16; j++) out[off + j] = dec[j] ^ prev[j];
        memcpy(prev, cur, 16);
        off += 16;
    }
}

/* ====================== AES-GCM（SP 800-38D）======================
 * GF(2^128) 逐位乘法（非高性能，但正确，KAT 可验）。 */

static void gcm_gfmul(uint8_t *X, const uint8_t *H) {
    uint8_t Z[16], V[16];
    int i, j;
    memset(Z, 0, 16);
    memcpy(V, H, 16);
    for (i = 0; i < 128; i++) {
        if ((X[i >> 3] >> (7 - (i & 7))) & 1)
            for (j = 0; j < 16; j++) Z[j] ^= V[j];
        { int lsb = V[15] & 1;
          for (j = 15; j > 0; j--) V[j] = (uint8_t)((V[j] >> 1) | (V[j-1] << 7));
          V[0] >>= 1; if (lsb) V[0] ^= 0xe1; }
    }
    memcpy(X, Z, 16);
}

static void gcm_ghash_data(const uint8_t *H, uint8_t *Y, const uint8_t *p, uint64_t len) {
    uint64_t i;
    int j;
    for (i = 0; i + 16 <= len; i += 16) {
        for (j = 0; j < 16; j++) Y[j] ^= p[i + j];
        gcm_gfmul(Y, H);
    }
    if (len % 16) {
        uint8_t b[16];
        memset(b, 0, 16);
        for (j = 0; j < (int)(len % 16); j++) b[j] = p[i + j];
        for (j = 0; j < 16; j++) Y[j] ^= b[j];
        gcm_gfmul(Y, H);
    }
}

static void gcm_j0(const uint8_t *H, const uint8_t *iv, uint64_t ivlen, uint8_t *J0) {
    if (ivlen == 12) {
        memcpy(J0, iv, 12);
        J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;
    } else {
        uint8_t lenblock[16];
        uint64_t ib = ivlen * 8;
        int j;
        memset(J0, 0, 16);
        gcm_ghash_data(H, J0, iv, ivlen);
        memset(lenblock, 0, 8);
        for (j = 0; j < 8; j++) lenblock[8 + j] = (uint8_t)(ib >> (56 - 8*j));
        for (j = 0; j < 16; j++) J0[j] ^= lenblock[j];
        gcm_gfmul(J0, H);
    }
}

static void gcm_gctr(const uint32_t *rk, int nr, const uint8_t *icb,
                     const uint8_t *in, uint64_t len, uint8_t *out) {
    uint8_t cb[16], ks[16];
    uint64_t i = 0;
    if (len == 0) return;
    memcpy(cb, icb, 16);
    while (i < len) {
        size_t j, take = (size_t)(len - i);
        int k;
        if (take > 16) take = 16;
        aes_encrypt_block(rk, nr, cb, ks);
        for (j = 0; j < take; j++) out[i + j] = in[i + j] ^ ks[j];
        for (k = 15; k >= 12; k--) { if (++cb[k]) break; }   /* inc32 */
        i += take;
    }
}

static void gcm_tag(const uint32_t *rk, int nr, const uint8_t *H, const uint8_t *J0,
                    const uint8_t *aad, uint64_t aadlen,
                    const uint8_t *ct, uint64_t ctlen, uint8_t *tag) {
    uint8_t S[16], EJ0[16], lenblock[16];
    uint64_t ab = aadlen * 8, cb = ctlen * 8;
    int j;
    memset(S, 0, 16);
    gcm_ghash_data(H, S, aad, aadlen);
    gcm_ghash_data(H, S, ct, ctlen);
    for (j = 0; j < 8; j++) lenblock[j]     = (uint8_t)(ab >> (56 - 8*j));
    for (j = 0; j < 8; j++) lenblock[8 + j] = (uint8_t)(cb >> (56 - 8*j));
    for (j = 0; j < 16; j++) S[j] ^= lenblock[j];
    gcm_gfmul(S, H);
    aes_encrypt_block(rk, nr, J0, EJ0);
    for (j = 0; j < 16; j++) tag[j] = EJ0[j] ^ S[j];
}

void crypto_aes_gcm_seal(uint8_t *key, uint32_t keybits, void *iv, uint64_t ivlen,
                         void *aad, uint64_t aadlen, void *plain, uint64_t plen,
                         uint8_t *cipher, uint8_t *tag) {
    uint32_t rk[60];
    int nr, k;
    uint8_t H[16], J0[16], cb[16];
    static const uint8_t zero[16] = { 0 };
    aes_expand(key, (int)keybits, rk, &nr);
    aes_encrypt_block(rk, nr, zero, H);
    gcm_j0(H, (const uint8_t *)iv, ivlen, J0);
    memcpy(cb, J0, 16);
    for (k = 15; k >= 12; k--) { if (++cb[k]) break; }
    gcm_gctr(rk, nr, cb, (const uint8_t *)plain, plen, cipher);
    gcm_tag(rk, nr, H, J0, (const uint8_t *)aad, aadlen, cipher, plen, tag);
}

int32_t crypto_aes_gcm_open(uint8_t *key, uint32_t keybits, void *iv, uint64_t ivlen,
                            void *aad, uint64_t aadlen, void *cipher, uint64_t clen,
                            uint8_t *tag, uint8_t *plain) {
    uint32_t rk[60];
    int nr, k, j;
    uint8_t H[16], J0[16], cb[16], calc[16], diff = 0;
    static const uint8_t zero[16] = { 0 };
    aes_expand(key, (int)keybits, rk, &nr);
    aes_encrypt_block(rk, nr, zero, H);
    gcm_j0(H, (const uint8_t *)iv, ivlen, J0);
    gcm_tag(rk, nr, H, J0, (const uint8_t *)aad, aadlen, (const uint8_t *)cipher, clen, calc);
    for (j = 0; j < 16; j++) diff |= (uint8_t)(calc[j] ^ tag[j]);
    if (diff) return -1;
    memcpy(cb, J0, 16);
    for (k = 15; k >= 12; k--) { if (++cb[k]) break; }
    gcm_gctr(rk, nr, cb, (const uint8_t *)cipher, clen, plain);
    return 0;
}

/* ====================== Ed25519（RFC 8032）======================
 * 移植自公有领域 TweetNaCl，复用上文 X25519 的 fe 域算术；散列用本模块 SHA-512。
 * 扭曲爱德华兹曲线点以 4 个 fe 坐标 (X,Y,Z,T) 表示（扩展坐标）。 */

static const fe ed_gf0 = { 0 };
static const fe ed_gf1 = { 1 };
static const fe ed_D  = { 0x78a3,0x1359,0x4dca,0x75eb,0xd8ab,0x4141,0x0a4d,0x0070,
                          0xe898,0x7779,0x4079,0x8cc7,0xfe73,0x2b6f,0x6cee,0x5203 };
static const fe ed_D2 = { 0xf159,0x26b2,0x9b94,0xebd6,0xb156,0x8283,0x149a,0x00e0,
                          0xd130,0xeef3,0x80f2,0x198e,0xfce7,0x56df,0xd9dc,0x2406 };
static const fe ed_X  = { 0xd51a,0x8f25,0x2d60,0xc956,0xa7b2,0x9525,0xc760,0x692c,
                          0xdc5c,0xfdd6,0xe231,0xc0a4,0x53fe,0xcd6e,0x36d3,0x2169 };
static const fe ed_Y  = { 0x6658,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,
                          0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666 };
static const fe ed_I  = { 0xa0b0,0x4a0e,0x1b27,0xc4ee,0xe478,0xad2f,0x1806,0x2f43,
                          0xd7a7,0x3dfb,0x0099,0x2b4d,0xdf0b,0x4fc1,0x2480,0x2b83 };

static const int64_t ED_L[32] = {
    0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10
};

static void fe_set(fe r, const fe a) { int i; for (i = 0; i < 16; i++) r[i] = a[i]; }

static void fe_pow2523(fe o, const fe i) {
    fe c;
    int a;
    for (a = 0; a < 16; a++) c[a] = i[a];
    for (a = 250; a >= 0; a--) { fe_sqr(c, c); if (a != 1) fe_mul(c, c, i); }
    for (a = 0; a < 16; a++) o[a] = c[a];
}

static int ct_verify32(const uint8_t *x, const uint8_t *y) {
    uint32_t d = 0;
    int i;
    for (i = 0; i < 32; i++) d |= (uint32_t)(x[i] ^ y[i]);
    return (int)((1 & ((d - 1) >> 8)) - 1);   /* 0=相等，-1=不等 */
}

static int fe_parity(const fe a) {
    uint8_t s[32];
    fe_pack(s, a);
    return s[0] & 1;
}
static int fe_neq(const fe a, const fe b) {
    uint8_t sa[32], sb[32];
    fe_pack(sa, a);
    fe_pack(sb, b);
    return ct_verify32(sa, sb);   /* 0=相等 */
}

/* 扩展坐标点加 p += q（p,q 各为 fe[4]） */
static void ed_add(fe p[4], fe q[4]) {
    fe a, b, c, d, t, e, f, g, h;
    fe_sub(a, p[1], p[0]); fe_sub(t, q[1], q[0]); fe_mul(a, a, t);
    fe_add(b, p[0], p[1]); fe_add(t, q[0], q[1]); fe_mul(b, b, t);
    fe_mul(c, p[3], q[3]); fe_mul(c, c, ed_D2);
    fe_mul(d, p[2], q[2]); fe_add(d, d, d);
    fe_sub(e, b, a); fe_sub(f, d, c); fe_add(g, d, c); fe_add(h, b, a);
    fe_mul(p[0], e, f); fe_mul(p[1], h, g); fe_mul(p[2], g, f); fe_mul(p[3], e, h);
}

static void ed_cswap(fe p[4], fe q[4], uint8_t b) {
    int i;
    for (i = 0; i < 4; i++) fe_cswap(p[i], q[i], b);
}

static void ed_pack(uint8_t *r, fe p[4]) {
    fe tx, ty, zi;
    fe_inv(zi, p[2]);
    fe_mul(tx, p[0], zi);
    fe_mul(ty, p[1], zi);
    fe_pack(r, ty);
    r[31] ^= (uint8_t)(fe_parity(tx) << 7);
}

static void ed_scalarmult(fe p[4], fe q[4], const uint8_t *s) {
    int i;
    fe_set(p[0], ed_gf0); fe_set(p[1], ed_gf1);
    fe_set(p[2], ed_gf1); fe_set(p[3], ed_gf0);
    for (i = 255; i >= 0; --i) {
        uint8_t b = (uint8_t)((s[i >> 3] >> (i & 7)) & 1);
        ed_cswap(p, q, b);
        ed_add(q, p);
        ed_add(p, p);
        ed_cswap(p, q, b);
    }
}

static void ed_scalarbase(fe p[4], const uint8_t *s) {
    fe q[4];
    fe_set(q[0], ed_X); fe_set(q[1], ed_Y);
    fe_set(q[2], ed_gf1); fe_mul(q[3], ed_X, ed_Y);
    ed_scalarmult(p, q, s);
}

static int ed_unpackneg(fe r[4], const uint8_t p[32]) {
    fe t, chk, num, den, den2, den4, den6;
    fe_set(r[2], ed_gf1);
    fe_unpack(r[1], p);
    fe_sqr(num, r[1]);
    fe_mul(den, num, ed_D);
    fe_sub(num, num, r[2]);
    fe_add(den, r[2], den);
    fe_sqr(den2, den);
    fe_sqr(den4, den2);
    fe_mul(den6, den4, den2);
    fe_mul(t, den6, num);
    fe_mul(t, t, den);
    fe_pow2523(t, t);
    fe_mul(t, t, num);
    fe_mul(t, t, den);
    fe_mul(t, t, den);
    fe_mul(r[0], t, den);
    fe_sqr(chk, r[0]);
    fe_mul(chk, chk, den);
    if (fe_neq(chk, num)) fe_mul(r[0], r[0], ed_I);
    fe_sqr(chk, r[0]);
    fe_mul(chk, chk, den);
    if (fe_neq(chk, num)) return -1;
    if (fe_parity(r[0]) == (p[31] >> 7)) fe_sub(r[0], ed_gf0, r[0]);
    fe_mul(r[3], r[0], r[1]);
    return 0;
}

/* 模群阶 L 归约：x[0..64) -> r[0..32) */
static void ed_modL(uint8_t *r, int64_t x[64]) {
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * ED_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * ED_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) x[j] -= carry * ED_L[j];
    for (i = 0; i < 32; ++i) {
        x[i+1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}
static void ed_reduce(uint8_t *r) {
    int64_t x[64];
    int i;
    for (i = 0; i < 64; ++i) x[i] = (int64_t)(uint64_t)r[i];
    for (i = 0; i < 64; ++i) r[i] = 0;
    ed_modL(r, x);
}

void crypto_ed25519_pubkey(uint8_t *pub, uint8_t *seed) {
    uint8_t h[64];
    fe p[4];
    crypto_sha512(seed, 32, h);
    h[0] &= 248; h[31] &= 127; h[31] |= 64;
    ed_scalarbase(p, h);
    ed_pack(pub, p);
}

void crypto_ed25519_sign(uint8_t *sig, void *msg, uint64_t mlen,
                         uint8_t *seed, uint8_t *pub) {
    uint8_t h[64], r[64], hram[64];
    fe p[4];
    int64_t x[64];
    int i, j;
    sha512_ctx ctx;
    crypto_sha512(seed, 32, h);
    h[0] &= 248; h[31] &= 127; h[31] |= 64;
    /* r = SHA512(prefix ‖ msg) mod L */
    sha512_init(&ctx, 0);
    sha512_update(&ctx, h + 32, 32);
    sha512_update(&ctx, (const uint8_t *)msg, (size_t)mlen);
    sha512_final(&ctx, r, 8);
    ed_reduce(r);
    ed_scalarbase(p, r);
    ed_pack(sig, p);                 /* R = sig[0..32) */
    /* hram = SHA512(R ‖ A ‖ msg) mod L */
    sha512_init(&ctx, 0);
    sha512_update(&ctx, sig, 32);
    sha512_update(&ctx, pub, 32);
    sha512_update(&ctx, (const uint8_t *)msg, (size_t)mlen);
    sha512_final(&ctx, hram, 8);
    ed_reduce(hram);
    /* S = (r + hram·a) mod L */
    for (i = 0; i < 64; i++) x[i] = 0;
    for (i = 0; i < 32; i++) x[i] = (int64_t)(uint64_t)r[i];
    for (i = 0; i < 32; i++)
        for (j = 0; j < 32; j++)
            x[i + j] += (int64_t)hram[i] * (int64_t)h[j];
    ed_modL(sig + 32, x);            /* S = sig[32..64) */
}

int32_t crypto_ed25519_verify(uint8_t *sig, void *msg, uint64_t mlen, uint8_t *pub) {
    uint8_t hram[64], t[32];
    fe p[4], q[4];
    sha512_ctx ctx;
    if (ed_unpackneg(q, pub)) return -1;
    sha512_init(&ctx, 0);
    sha512_update(&ctx, sig, 32);
    sha512_update(&ctx, pub, 32);
    sha512_update(&ctx, (const uint8_t *)msg, (size_t)mlen);
    sha512_final(&ctx, hram, 8);
    ed_reduce(hram);
    ed_scalarmult(p, q, hram);
    ed_scalarbase(q, sig + 32);
    ed_add(p, q);
    ed_pack(t, p);
    if (ct_verify32(sig, t)) return -1;
    return 0;
}
