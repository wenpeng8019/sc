/* crypto_impl.c —— sc crypto 内置模块默认实现（自含、零外部库链接）
 *
 * 第一期：SHA-256（FIPS 180-4）、HMAC-SHA256（RFC 2104）、HKDF-SHA256（RFC 5869）。
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
