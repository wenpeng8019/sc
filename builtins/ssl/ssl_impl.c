/* ssl_impl.c —— ssl.sc 的默认实现（双后端 mbedTLS / OpenSSL，编译期 ifdef 选择）
 *
 * 拼接：inc ssl.sc 时本文件并入生成单元（同 TU），复用 ssl.h 头。
 * 后端固化于构建 scc 时（SCC_WITH_MBEDTLS / SCC_WITH_OPENSSL，参考 async_impl.c 的 SCC_WITH_UV）。
 * 三分支：mbedTLS / OpenSSL / none（皆未定义时安全失败，零外部依赖，默认构建即此分支）。
 */
#include "ssl.h"

#include <stdlib.h>
#include <string.h>

#if defined(SCC_WITH_MBEDTLS)
/* ===================== mbedTLS 后端（vendor 源码内置，静态烘进二进制） =====================
 * 与 OpenSSL 不同：mbedTLS 用回调式 BIO —— mbedtls_ssl_set_bio 直接挂两个收发回调，
 * 它们即是「传输桥」（缝到 ssl_set_transport 注入的 send/recv），无需中间内存 BIO。
 * 故本层同样不碰 socket：底层传输（socket / sc com 设备）由调用方注入。
 */
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/net_sockets.h>   /* MBEDTLS_ERR_NET_{SEND,RECV}_FAILED 常量 */

struct ssl_conn {
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context  entropy;
    mbedtls_x509_crt         ca;       /* CA 链（verify 时加载系统信任库） */
    ssl_send_fn              send;
    ssl_recv_fn              recv;
    void                    *tctx;
    char                    *host;
    int                      verify;
};

int32_t  ssl_backend(void)      { return SSL_BACKEND_MBEDTLS; }
char    *ssl_backend_name(void) { return (char *)"mbedtls"; }

/* 传输桥回调（p_bio = struct ssl_conn*）：缝到注入的 send/recv。
 * recv 返回 0（暂无数据/EOF）→ WANT_READ：com 适配层经 tx_eof 标志识别真 EOF。 */
static int mbed_bio_send(void *ctx, const unsigned char *buf, size_t len) {
    struct ssl_conn *s = (struct ssl_conn *)ctx;
    int n = s->send(s->tctx, buf, (uint32_t)len);
    if (n < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    if (n == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return n;
}
static int mbed_bio_recv(void *ctx, unsigned char *buf, size_t len) {
    struct ssl_conn *s = (struct ssl_conn *)ctx;
    int n = s->recv(s->tctx, buf, (uint32_t)len);
    if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    if (n == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    return n;
}

void *ssl_client_new(char *host, int32_t verify) {
    struct ssl_conn *s = (struct ssl_conn *)calloc(1, sizeof *s);
    if (!s) return NULL;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_ctr_drbg_init(&s->drbg);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_x509_crt_init(&s->ca);

    const char *pers = "sc-ssl-client";
    if (mbedtls_ctr_drbg_seed(&s->drbg, mbedtls_entropy_func, &s->entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0)
        goto fail;
    if (mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        goto fail;
    mbedtls_ssl_conf_min_tls_version(&s->conf, MBEDTLS_SSL_VERSION_TLS1_2);   /* 禁 TLS1.0/1.1 */
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->drbg);

    if (verify) {
        /* mbedTLS 无内置系统信任库：从常见路径加载 CA bundle（PEM）。SSL_CERT_FILE 可显式覆盖。 */
        static const char *const ca_paths[] = {
            "/etc/ssl/cert.pem",                  /* macOS / 部分 BSD */
            "/etc/ssl/certs/ca-certificates.crt", /* Debian/Ubuntu */
            "/etc/pki/tls/certs/ca-bundle.crt",   /* RHEL/Fedora */
            "/etc/ssl/ca-bundle.pem",             /* SUSE */
            NULL
        };
        int loaded = 0;
        const char *env = getenv("SSL_CERT_FILE");
        if (env && *env && mbedtls_x509_crt_parse_file(&s->ca, env) == 0) loaded = 1;
        for (int i = 0; !loaded && ca_paths[i]; i++)
            if (mbedtls_x509_crt_parse_file(&s->ca, ca_paths[i]) == 0) loaded = 1;
        if (!loaded) goto fail;   /* 要求 verify 却无信任库：安全失败（不静默降级） */
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, NULL);
    } else {
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    if (mbedtls_ssl_setup(&s->ssl, &s->conf) != 0) goto fail;
    if (host && *host) {
        if (mbedtls_ssl_set_hostname(&s->ssl, host) != 0) goto fail;   /* SNI + 域名校验 */
        s->host = strdup(host);
    }
    s->verify = verify;
    return s;   /* bio 待 ssl_set_transport 注入 send/recv 后再挂 */
fail:
    ssl_free(s);
    return NULL;
}

void ssl_set_transport(void *sv, void *send, void *recv, void *ctx) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return;
    s->send = (ssl_send_fn)send;
    s->recv = (ssl_recv_fn)recv;
    s->tctx = ctx;
    mbedtls_ssl_set_bio(&s->ssl, s, mbed_bio_send, mbed_bio_recv, NULL);
}

void ssl_free(void *sv) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return;
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_ctr_drbg_free(&s->drbg);
    mbedtls_entropy_free(&s->entropy);
    mbedtls_x509_crt_free(&s->ca);
    free(s->host);
    free(s);
}

int32_t ssl_handshake(void *sv) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return SSL_ERR;
    int r = mbedtls_ssl_handshake(&s->ssl);
    if (r == 0) return SSL_OK;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return SSL_WANT_IO;
    return SSL_ERR;
}

int32_t ssl_read(void *sv, void *buf, uint32_t len) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return SSL_ERR;
    int r = mbedtls_ssl_read(&s->ssl, (unsigned char *)buf, (size_t)len);
    if (r > 0) return r;
    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;   /* 对端关闭 */
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return SSL_WANT_IO;
    return SSL_ERR;
}

int32_t ssl_write(void *sv, void *buf, uint32_t len) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return SSL_ERR;
    int r = mbedtls_ssl_write(&s->ssl, (const unsigned char *)buf, (size_t)len);
    if (r > 0) return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return SSL_WANT_IO;
    return SSL_ERR;
}

/* ===================== RSA 代理实现（覆盖 crypto 弱桩） =====================
 * 同 OpenSSL 分支：提供 crypto_rsa_* 强符号，链接期覆盖 crypto 的弱桩。
 * 不透明句柄 = mbedtls_pk_context*（内含 RSA）；fmt：0=DER 1=PEM；hash_id 见
 * crypto.h SC_HASH_*。私钥运算/keygen 需 RNG —— 懒初始化一个进程级 ctr_drbg。 */
#include <mbedtls/rsa.h>
#include <mbedtls/pk.h>
#include <mbedtls/md.h>

static mbedtls_ctr_drbg_context sc_rsa_drbg;
static mbedtls_entropy_context  sc_rsa_entropy;
static int sc_rsa_rng_ready = 0;

static int sc_rsa_rng_init(void) {
    const char *pers = "sc-rsa-proxy";
    if (sc_rsa_rng_ready) return 0;
    mbedtls_ctr_drbg_init(&sc_rsa_drbg);
    mbedtls_entropy_init(&sc_rsa_entropy);
    if (mbedtls_ctr_drbg_seed(&sc_rsa_drbg, mbedtls_entropy_func, &sc_rsa_entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0)
        return -1;
    sc_rsa_rng_ready = 1;
    return 0;
}

static mbedtls_md_type_t sc_rsa_mdtype(int32_t hash_id) {
    switch (hash_id) {
        case 1: return MBEDTLS_MD_SHA1;
        case 3: return MBEDTLS_MD_SHA224;
        case 4: return MBEDTLS_MD_SHA256;
        case 5: return MBEDTLS_MD_SHA384;
        case 6: return MBEDTLS_MD_SHA512;
        default: return MBEDTLS_MD_NONE;
    }
}

int32_t crypto_rsa_backend(void) { return SSL_BACKEND_MBEDTLS; }

void *crypto_rsa_keygen(uint32_t bits, uint32_t pub_e) {
    mbedtls_pk_context *pk;
    if (sc_rsa_rng_init() != 0) return NULL;
    pk = (mbedtls_pk_context *)calloc(1, sizeof *pk);
    if (!pk) return NULL;
    mbedtls_pk_init(pk);
    if (mbedtls_pk_setup(pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0 ||
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(*pk), mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                            (unsigned)bits, (int)(pub_e ? pub_e : 65537)) != 0) {
        mbedtls_pk_free(pk);
        free(pk);
        return NULL;
    }
    return pk;
}

void crypto_rsa_free(void *k) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)k;
    if (!pk) return;
    mbedtls_pk_free(pk);
    free(pk);
}

void *crypto_rsa_import(void *buf, uint64_t len, int32_t is_private, int32_t fmt) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)calloc(1, sizeof *pk);
    unsigned char *tmp = NULL;
    const unsigned char *key = (const unsigned char *)buf;
    size_t klen = (size_t)len;
    int rc;
    if (!pk) return NULL;
    mbedtls_pk_init(pk);
    if (fmt == 1) {   /* PEM：mbedTLS 要求含结尾 NUL，长度计入 NUL */
        tmp = (unsigned char *)malloc((size_t)len + 1);
        if (!tmp) { free(pk); return NULL; }
        memcpy(tmp, buf, (size_t)len);
        tmp[len] = 0;
        key = tmp;
        klen = (size_t)len + 1;
    }
    if (is_private) {
        if (sc_rsa_rng_init() != 0) { free(tmp); mbedtls_pk_free(pk); free(pk); return NULL; }
        rc = mbedtls_pk_parse_key(pk, key, klen, NULL, 0,
                                  mbedtls_ctr_drbg_random, &sc_rsa_drbg);
    } else {
        rc = mbedtls_pk_parse_public_key(pk, key, klen);
    }
    free(tmp);
    if (rc != 0) { mbedtls_pk_free(pk); free(pk); return NULL; }
    return pk;
}

int32_t crypto_rsa_export(void *k, int32_t is_private, int32_t fmt, uint8_t *out, uint64_t cap) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)k;
    int n;
    if (!pk) return -1;
    if (fmt == 1) {   /* PEM：写入 NUL 结尾字符串，返回成功=0 */
        n = is_private ? mbedtls_pk_write_key_pem(pk, out, (size_t)cap)
                       : mbedtls_pk_write_pubkey_pem(pk, out, (size_t)cap);
        if (n != 0) return -1;
        return (int32_t)strlen((char *)out);
    }
    /* DER：写在缓冲末尾，返回长度，需前移到首部 */
    n = is_private ? mbedtls_pk_write_key_der(pk, out, (size_t)cap)
                   : mbedtls_pk_write_pubkey_der(pk, out, (size_t)cap);
    if (n < 0) return -1;
    memmove(out, out + (size_t)cap - n, (size_t)n);
    return n;
}

static int32_t sc_rsa_do_sign(mbedtls_pk_context *pk, int32_t hash_id, const void *digest,
                              uint64_t dlen, uint8_t *sig, uint64_t sigcap, int pss) {
    mbedtls_rsa_context *rsa;
    mbedtls_md_type_t md = sc_rsa_mdtype(hash_id);
    size_t klen;
    int rc;
    if (!pk || md == MBEDTLS_MD_NONE) return -1;
    if (sc_rsa_rng_init() != 0) return -1;
    rsa = mbedtls_pk_rsa(*pk);
    klen = mbedtls_rsa_get_len(rsa);
    if (klen > sigcap) return -1;
    if (pss) {
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, md);
        rc = mbedtls_rsa_rsassa_pss_sign(rsa, mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                                         md, (unsigned)dlen, (const unsigned char *)digest, sig);
    } else {
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
        rc = mbedtls_rsa_rsassa_pkcs1_v15_sign(rsa, mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                                               md, (unsigned)dlen, (const unsigned char *)digest, sig);
    }
    return rc == 0 ? (int32_t)klen : -1;
}

static int32_t sc_rsa_do_verify(mbedtls_pk_context *pk, int32_t hash_id, const void *digest,
                                uint64_t dlen, const void *sig, int pss) {
    mbedtls_rsa_context *rsa;
    mbedtls_md_type_t md = sc_rsa_mdtype(hash_id);
    int rc;
    if (!pk || md == MBEDTLS_MD_NONE) return -1;
    rsa = mbedtls_pk_rsa(*pk);
    if (pss) {
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, md);
        rc = mbedtls_rsa_rsassa_pss_verify(rsa, md, (unsigned)dlen,
                                           (const unsigned char *)digest, (const unsigned char *)sig);
    } else {
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
        rc = mbedtls_rsa_rsassa_pkcs1_v15_verify(rsa, md, (unsigned)dlen,
                                                 (const unsigned char *)digest, (const unsigned char *)sig);
    }
    return rc == 0 ? 0 : -1;
}

int32_t crypto_rsa_sign_pkcs1(void *k, int32_t hash_id, void *digest, uint64_t dlen, uint8_t *sig, uint64_t sigcap) {
    return sc_rsa_do_sign((mbedtls_pk_context *)k, hash_id, digest, dlen, sig, sigcap, 0);
}
int32_t crypto_rsa_verify_pkcs1(void *k, int32_t hash_id, void *digest, uint64_t dlen, void *sig, uint64_t siglen) {
    (void)siglen;
    return sc_rsa_do_verify((mbedtls_pk_context *)k, hash_id, digest, dlen, sig, 0);
}
int32_t crypto_rsa_sign_pss(void *k, int32_t hash_id, void *digest, uint64_t dlen, uint8_t *sig, uint64_t sigcap) {
    return sc_rsa_do_sign((mbedtls_pk_context *)k, hash_id, digest, dlen, sig, sigcap, 1);
}
int32_t crypto_rsa_verify_pss(void *k, int32_t hash_id, void *digest, uint64_t dlen, void *sig, uint64_t siglen) {
    (void)siglen;
    return sc_rsa_do_verify((mbedtls_pk_context *)k, hash_id, digest, dlen, sig, 1);
}

int32_t crypto_rsa_encrypt_oaep(void *k, int32_t hash_id, void *plain, uint64_t plen, uint8_t *out, uint64_t cap) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)k;
    mbedtls_rsa_context *rsa;
    mbedtls_md_type_t md = sc_rsa_mdtype(hash_id);
    size_t klen;
    if (!pk || md == MBEDTLS_MD_NONE) return -1;
    if (sc_rsa_rng_init() != 0) return -1;
    rsa = mbedtls_pk_rsa(*pk);
    klen = mbedtls_rsa_get_len(rsa);
    if (klen > cap) return -1;
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, md);
    if (mbedtls_rsa_rsaes_oaep_encrypt(rsa, mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                                       NULL, 0, (size_t)plen, (const unsigned char *)plain, out) != 0)
        return -1;
    return (int32_t)klen;
}
int32_t crypto_rsa_decrypt_oaep(void *k, int32_t hash_id, void *cipher, uint64_t clen, uint8_t *out, uint64_t cap) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)k;
    mbedtls_rsa_context *rsa;
    mbedtls_md_type_t md = sc_rsa_mdtype(hash_id);
    size_t olen = 0;
    if (!pk || md == MBEDTLS_MD_NONE) return -1;
    if (sc_rsa_rng_init() != 0) return -1;
    rsa = mbedtls_pk_rsa(*pk);
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, md);
    if (mbedtls_rsa_rsaes_oaep_decrypt(rsa, mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                                       NULL, 0, &olen, (const unsigned char *)cipher, out, (size_t)cap) != 0)
        return -1;
    return (int32_t)olen;
}

int32_t crypto_rsa_encrypt_pkcs1(void *k, void *plain, uint64_t plen, uint8_t *out, uint64_t cap) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)k;
    mbedtls_rsa_context *rsa;
    size_t klen;
    if (!pk) return -1;
    if (sc_rsa_rng_init() != 0) return -1;
    rsa = mbedtls_pk_rsa(*pk);
    klen = mbedtls_rsa_get_len(rsa);
    if (klen > cap) return -1;
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
    if (mbedtls_rsa_rsaes_pkcs1_v15_encrypt(rsa, mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                                            (size_t)plen, (const unsigned char *)plain, out) != 0)
        return -1;
    return (int32_t)klen;
}
int32_t crypto_rsa_decrypt_pkcs1(void *k, void *cipher, uint64_t clen, uint8_t *out, uint64_t cap) {
    mbedtls_pk_context *pk = (mbedtls_pk_context *)k;
    mbedtls_rsa_context *rsa;
    size_t olen = 0;
    if (!pk) return -1;
    if (sc_rsa_rng_init() != 0) return -1;
    rsa = mbedtls_pk_rsa(*pk);
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
    if (mbedtls_rsa_rsaes_pkcs1_v15_decrypt(rsa, mbedtls_ctr_drbg_random, &sc_rsa_drbg,
                                            &olen, (const unsigned char *)cipher, out, (size_t)cap) != 0)
        return -1;
    return (int32_t)olen;
}

#elif defined(SCC_WITH_OPENSSL)
/* ===================== OpenSSL 后端（系统库；内存 BIO 非阻塞客户端） =====================
 * 经传输回调缝解耦：SSL 读写密文走两个内存 BIO——
 *   rbio：网络→SSL，收到的密文 BIO_write 进来供 SSL 解密；
 *   wbio：SSL→网络，SSL 写出的密文从 BIO_read 取出经 send 发出。
 * 故本层不直接碰 socket：底层传输（socket / sc com 设备）由调用方经 ssl_set_transport 注入。
 */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/x509v3.h>

struct ssl_conn {
    SSL_CTX     *ctx;
    SSL         *ssl;
    BIO         *rbio;     /* 网络→SSL（我方写入收到的密文） */
    BIO         *wbio;     /* SSL→网络（我方读出待发的密文） */
    ssl_send_fn  send;
    ssl_recv_fn  recv;
    void        *tctx;
    char        *host;
    int          verify;
};

int32_t  ssl_backend(void)      { return SSL_BACKEND_OPENSSL; }
char    *ssl_backend_name(void) { return (char *)"openssl"; }

/* 把 wbio 里 SSL 写出的密文全部经 send 发到底层。SSL_OK / SSL_WANT_IO（传输忙）/ SSL_ERR。 */
static int ssl_flush_out(struct ssl_conn *s) {
    unsigned char buf[4096];
    int n;
    while ((n = BIO_read(s->wbio, buf, (int)sizeof buf)) > 0) {
        int off = 0;
        while (off < n) {
            int w = s->send(s->tctx, buf + off, (uint32_t)(n - off));
            if (w < 0) return SSL_ERR;
            if (w == 0) return SSL_WANT_IO;   /* 传输暂不可写：剩余下次再发 */
            off += w;
        }
    }
    return SSL_OK;
}

/* 从底层经 recv 取一批密文喂进 rbio。SSL_OK（有数据）/ SSL_WANT_IO（暂无）/ SSL_ERR。 */
static int ssl_pump_in(struct ssl_conn *s) {
    unsigned char buf[4096];
    int n = s->recv(s->tctx, buf, (int)sizeof buf);
    if (n < 0) return SSL_ERR;
    if (n == 0) return SSL_WANT_IO;   /* 暂无数据 / EOF */
    BIO_write(s->rbio, buf, n);
    return SSL_OK;
}

void *ssl_client_new(char *host, int32_t verify) {
    struct ssl_conn *s = (struct ssl_conn *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->ctx = SSL_CTX_new(TLS_client_method());
    if (!s->ctx) { free(s); return NULL; }
    SSL_CTX_set_min_proto_version(s->ctx, TLS1_2_VERSION);   /* 禁用 TLS1.0/1.1 */
    if (verify) {
        SSL_CTX_set_verify(s->ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(s->ctx);            /* 系统信任根 */
    }
    s->ssl = SSL_new(s->ctx);
    if (!s->ssl) { SSL_CTX_free(s->ctx); free(s); return NULL; }
    s->rbio = BIO_new(BIO_s_mem());
    s->wbio = BIO_new(BIO_s_mem());
    if (!s->rbio || !s->wbio) {
        if (s->rbio) BIO_free(s->rbio);
        if (s->wbio) BIO_free(s->wbio);
        SSL_free(s->ssl); SSL_CTX_free(s->ctx); free(s); return NULL;
    }
    SSL_set_bio(s->ssl, s->rbio, s->wbio);   /* SSL 接管 BIO 所有权（free 时一并释放） */
    SSL_set_connect_state(s->ssl);
    if (host && *host) {
        SSL_set_tlsext_host_name(s->ssl, host);   /* SNI */
        if (verify) {
            X509_VERIFY_PARAM *p = SSL_get0_param(s->ssl);
            X509_VERIFY_PARAM_set_hostflags(p, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            SSL_set1_host(s->ssl, host);          /* 证书域名校验 */
        }
        s->host = strdup(host);
    }
    s->verify = verify;
    return s;
}

void ssl_set_transport(void *sv, void *send, void *recv, void *ctx) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return;
    s->send = (ssl_send_fn)send;
    s->recv = (ssl_recv_fn)recv;
    s->tctx = ctx;
}

void ssl_free(void *sv) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return;
    if (s->ssl) SSL_free(s->ssl);        /* 连带释放 rbio/wbio */
    else { if (s->rbio) BIO_free(s->rbio); if (s->wbio) BIO_free(s->wbio); }
    if (s->ctx) SSL_CTX_free(s->ctx);
    free(s->host);
    free(s);
}

int32_t ssl_handshake(void *sv) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s || !s->send || !s->recv) return SSL_ERR;
    int r = SSL_do_handshake(s->ssl);
    if (ssl_flush_out(s) == SSL_ERR) return SSL_ERR;   /* 先把握手报文发出 */
    if (r == 1) return SSL_OK;
    int err = SSL_get_error(s->ssl, r);
    if (err == SSL_ERROR_WANT_READ) {
        if (ssl_pump_in(s) == SSL_ERR) return SSL_ERR;
        return SSL_WANT_IO;   /* 调用方在传输就绪后再驱动 */
    }
    if (err == SSL_ERROR_WANT_WRITE) return SSL_WANT_IO;
    return SSL_ERR;
}

int32_t ssl_read(void *sv, void *buf, uint32_t len) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return SSL_ERR;
    int r = SSL_read(s->ssl, buf, (int)len);
    if (r > 0) return r;
    int err = SSL_get_error(s->ssl, r);
    if (err == SSL_ERROR_WANT_READ) {
        int p = ssl_pump_in(s);
        if (p == SSL_ERR) return SSL_ERR;
        return SSL_WANT_IO;   /* 已喂入新密文（或暂无）：调用方再调一次 */
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        if (ssl_flush_out(s) == SSL_ERR) return SSL_ERR;
        return SSL_WANT_IO;   /* 重协商写出 */
    }
    if (err == SSL_ERROR_ZERO_RETURN) return 0;   /* 对端干净关闭 */
    return SSL_ERR;
}

int32_t ssl_write(void *sv, void *buf, uint32_t len) {
    struct ssl_conn *s = (struct ssl_conn *)sv;
    if (!s) return SSL_ERR;
    int r = SSL_write(s->ssl, buf, (int)len);
    if (r > 0) {
        if (ssl_flush_out(s) == SSL_ERR) return SSL_ERR;
        return r;
    }
    int err = SSL_get_error(s->ssl, r);
    if (err == SSL_ERROR_WANT_READ) {
        if (ssl_pump_in(s) == SSL_ERR) return SSL_ERR;
        return SSL_WANT_IO;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        if (ssl_flush_out(s) == SSL_ERR) return SSL_ERR;
        return SSL_WANT_IO;
    }
    return SSL_ERR;
}

/* ===================== RSA 代理实现（覆盖 crypto 弱桩） =====================
 * crypto 模块声明 crypto_rsa_*（见 builtins/crypto/crypto.sc），自身只给
 * __attribute__((weak)) 安全失败桩。本文件在编译进 OpenSSL 后端时提供同名
 * 强符号，链接期覆盖弱桩；故仅当工程同时 inc crypto.sc + inc ssl.sc 且 scc
 * 以 -DSCC_SSL_BACKEND=openssl 构建时，RSA 才真正可用。
 * 不透明句柄 = EVP_PKEY*；fmt：0=DER 1=PEM；hash_id 见 crypto.h SC_HASH_*。 */
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/x509.h>

static const EVP_MD *sc_rsa_md(int32_t hash_id) {
    switch (hash_id) {
        case 1: return EVP_sha1();
        case 3: return EVP_sha224();
        case 4: return EVP_sha256();
        case 5: return EVP_sha384();
        case 6: return EVP_sha512();
        default: return NULL;
    }
}

int32_t crypto_rsa_backend(void) { return SSL_BACKEND_OPENSSL; }

void *crypto_rsa_keygen(uint32_t bits, uint32_t pub_e) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY *pkey = NULL;
    BIGNUM *e = NULL;
    if (!ctx) return NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0) goto done;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, (int)bits) <= 0) goto done;
    e = BN_new();
    if (!e || !BN_set_word(e, pub_e ? pub_e : 65537)) goto done;
    if (EVP_PKEY_CTX_set1_rsa_keygen_pubexp(ctx, e) <= 0) goto done;  /* set1：内部复制 e */
    EVP_PKEY_keygen(ctx, &pkey);
done:
    if (e) BN_free(e);
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

void crypto_rsa_free(void *k) { if (k) EVP_PKEY_free((EVP_PKEY *)k); }

void *crypto_rsa_import(void *buf, uint64_t len, int32_t is_private, int32_t fmt) {
    EVP_PKEY *pkey = NULL;
    BIO *bio = BIO_new_mem_buf(buf, (int)len);
    if (!bio) return NULL;
    if (fmt == 1) {   /* PEM */
        if (is_private) pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        else            pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    } else {          /* DER */
        if (is_private) pkey = d2i_PrivateKey_bio(bio, NULL);
        else            pkey = d2i_PUBKEY_bio(bio, NULL);
    }
    BIO_free(bio);
    return pkey;
}

int32_t crypto_rsa_export(void *k, int32_t is_private, int32_t fmt, uint8_t *out, uint64_t cap) {
    EVP_PKEY *pkey = (EVP_PKEY *)k;
    BIO *bio;
    int ok = 0, n = -1;
    if (!pkey) return -1;
    bio = BIO_new(BIO_s_mem());
    if (!bio) return -1;
    if (fmt == 1) {   /* PEM */
        if (is_private) ok = PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL);
        else            ok = PEM_write_bio_PUBKEY(bio, pkey);
    } else {          /* DER */
        if (is_private) ok = i2d_PrivateKey_bio(bio, pkey);
        else            ok = i2d_PUBKEY_bio(bio, pkey);
    }
    if (ok) {
        BUF_MEM *mem = NULL;
        BIO_get_mem_ptr(bio, &mem);
        if (mem && (uint64_t)mem->length <= cap) {
            memcpy(out, mem->data, mem->length);
            n = (int)mem->length;
        }
    }
    BIO_free(bio);
    return n;
}

/* 通用签名：padding=RSA_PKCS1_PADDING（PKCS1v1.5）或 RSA_PKCS1_PSS_PADDING（PSS）。 */
static int32_t sc_rsa_sign(EVP_PKEY *pkey, int32_t hash_id, const void *digest, uint64_t dlen,
                           uint8_t *sig, uint64_t sigcap, int padding) {
    const EVP_MD *md = sc_rsa_md(hash_id);
    EVP_PKEY_CTX *ctx;
    size_t siglen = (size_t)sigcap;
    int rc = -1;
    if (!pkey || !md) return -1;
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx) return -1;
    if (EVP_PKEY_sign_init(ctx) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, padding) > 0 &&
        EVP_PKEY_CTX_set_signature_md(ctx, md) > 0 &&
        (padding != RSA_PKCS1_PSS_PADDING ||
         EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, RSA_PSS_SALTLEN_DIGEST) > 0) &&
        EVP_PKEY_sign(ctx, sig, &siglen, (const unsigned char *)digest, (size_t)dlen) > 0)
        rc = (int32_t)siglen;
    EVP_PKEY_CTX_free(ctx);
    return rc;
}

static int32_t sc_rsa_verify(EVP_PKEY *pkey, int32_t hash_id, const void *digest, uint64_t dlen,
                             const void *sig, uint64_t siglen, int padding) {
    const EVP_MD *md = sc_rsa_md(hash_id);
    EVP_PKEY_CTX *ctx;
    int rc = -1;
    if (!pkey || !md) return -1;
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx) return -1;
    if (EVP_PKEY_verify_init(ctx) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, padding) > 0 &&
        EVP_PKEY_CTX_set_signature_md(ctx, md) > 0 &&
        (padding != RSA_PKCS1_PSS_PADDING ||
         EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, RSA_PSS_SALTLEN_DIGEST) > 0) &&
        EVP_PKEY_verify(ctx, (const unsigned char *)sig, (size_t)siglen,
                        (const unsigned char *)digest, (size_t)dlen) == 1)
        rc = 0;
    EVP_PKEY_CTX_free(ctx);
    return rc;
}

int32_t crypto_rsa_sign_pkcs1(void *k, int32_t hash_id, void *digest, uint64_t dlen, uint8_t *sig, uint64_t sigcap) {
    return sc_rsa_sign((EVP_PKEY *)k, hash_id, digest, dlen, sig, sigcap, RSA_PKCS1_PADDING);
}
int32_t crypto_rsa_verify_pkcs1(void *k, int32_t hash_id, void *digest, uint64_t dlen, void *sig, uint64_t siglen) {
    return sc_rsa_verify((EVP_PKEY *)k, hash_id, digest, dlen, sig, siglen, RSA_PKCS1_PADDING);
}
int32_t crypto_rsa_sign_pss(void *k, int32_t hash_id, void *digest, uint64_t dlen, uint8_t *sig, uint64_t sigcap) {
    return sc_rsa_sign((EVP_PKEY *)k, hash_id, digest, dlen, sig, sigcap, RSA_PKCS1_PSS_PADDING);
}
int32_t crypto_rsa_verify_pss(void *k, int32_t hash_id, void *digest, uint64_t dlen, void *sig, uint64_t siglen) {
    return sc_rsa_verify((EVP_PKEY *)k, hash_id, digest, dlen, sig, siglen, RSA_PKCS1_PSS_PADDING);
}

int32_t crypto_rsa_encrypt_oaep(void *k, int32_t hash_id, void *plain, uint64_t plen, uint8_t *out, uint64_t cap) {
    EVP_PKEY *pkey = (EVP_PKEY *)k;
    const EVP_MD *md = sc_rsa_md(hash_id);
    EVP_PKEY_CTX *ctx;
    size_t outlen = (size_t)cap;
    int rc = -1;
    if (!pkey || !md) return -1;
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx) return -1;
    if (EVP_PKEY_encrypt_init(ctx) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0 &&
        EVP_PKEY_CTX_set_rsa_oaep_md(ctx, md) > 0 &&
        EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, md) > 0 &&
        EVP_PKEY_encrypt(ctx, out, &outlen, (const unsigned char *)plain, (size_t)plen) > 0)
        rc = (int32_t)outlen;
    EVP_PKEY_CTX_free(ctx);
    return rc;
}
int32_t crypto_rsa_decrypt_oaep(void *k, int32_t hash_id, void *cipher, uint64_t clen, uint8_t *out, uint64_t cap) {
    EVP_PKEY *pkey = (EVP_PKEY *)k;
    const EVP_MD *md = sc_rsa_md(hash_id);
    EVP_PKEY_CTX *ctx;
    size_t outlen = (size_t)cap;
    int rc = -1;
    if (!pkey || !md) return -1;
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx) return -1;
    if (EVP_PKEY_decrypt_init(ctx) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0 &&
        EVP_PKEY_CTX_set_rsa_oaep_md(ctx, md) > 0 &&
        EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, md) > 0 &&
        EVP_PKEY_decrypt(ctx, out, &outlen, (const unsigned char *)cipher, (size_t)clen) > 0)
        rc = (int32_t)outlen;
    EVP_PKEY_CTX_free(ctx);
    return rc;
}

int32_t crypto_rsa_encrypt_pkcs1(void *k, void *plain, uint64_t plen, uint8_t *out, uint64_t cap) {
    EVP_PKEY *pkey = (EVP_PKEY *)k;
    EVP_PKEY_CTX *ctx;
    size_t outlen = (size_t)cap;
    int rc = -1;
    if (!pkey) return -1;
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx) return -1;
    if (EVP_PKEY_encrypt_init(ctx) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) > 0 &&
        EVP_PKEY_encrypt(ctx, out, &outlen, (const unsigned char *)plain, (size_t)plen) > 0)
        rc = (int32_t)outlen;
    EVP_PKEY_CTX_free(ctx);
    return rc;
}
int32_t crypto_rsa_decrypt_pkcs1(void *k, void *cipher, uint64_t clen, uint8_t *out, uint64_t cap) {
    EVP_PKEY *pkey = (EVP_PKEY *)k;
    EVP_PKEY_CTX *ctx;
    size_t outlen = (size_t)cap;
    int rc = -1;
    if (!pkey) return -1;
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!ctx) return -1;
    if (EVP_PKEY_decrypt_init(ctx) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) > 0 &&
        EVP_PKEY_decrypt(ctx, out, &outlen, (const unsigned char *)cipher, (size_t)clen) > 0)
        rc = (int32_t)outlen;
    EVP_PKEY_CTX_free(ctx);
    return rc;
}

#else
/* ===================== none 后端（未配置 TLS：所有调用安全失败） ===================== */

struct ssl_conn { int _unused; };

int32_t  ssl_backend(void)      { return SSL_BACKEND_NONE; }
char    *ssl_backend_name(void) { return (char *)"none"; }

void *ssl_client_new(char *host, int32_t verify) {
    (void)host; (void)verify;
    return NULL;   /* 无后端：无法建连 */
}

void ssl_set_transport(void *s, void *send, void *recv, void *ctx) {
    (void)s; (void)send; (void)recv; (void)ctx;
}

void ssl_free(void *s) { (void)s; }

int32_t ssl_handshake(void *s)                          { (void)s; return SSL_ERR; }
int32_t ssl_read (void *s, void *buf, uint32_t len)     { (void)s; (void)buf; (void)len; return SSL_ERR; }
int32_t ssl_write(void *s, void *buf, uint32_t len)     { (void)s; (void)buf; (void)len; return SSL_ERR; }

#endif

/* ===================== com 设备适配层（后端无关：仅用公开 ssl_* API + op.h 的 com） =====================
 * 把 TLS 包成 com 设备，叠在底层传输 com（io.tcp 等）之上。同步形态：构造时阻塞驱动握手，
 * read/write 经 ssl_read/ssl_write 加解密；密文经传输回调缝桥到底层 com.read/com.write。
 * 要求底层为「阻塞」传输（tcp(fd,false,1,1)）——故 ssl_read/write 的 SSL_WANT_IO 在循环内
 * 由阻塞传输的下一次读/写解除。异步（非阻塞 + async_loop）形态留待后续。
 */
#include "op.h"   /* com / limit / ioq / IO_AGAIN / IO_EOF / sc_chunk0 / sc_recycle */

typedef struct sc_ssl_dev {
    com       com;            /* 端点（offset 0，返回其地址即 com&） */
    void     *ssl;            /* ssl_conn*（不透明，经公开 API 操作） */
    com      *transport;      /* 底层传输 com（不拥有） */
    int32_t   owns_transport; /* close 时是否一并关闭底层（默认 0） */
    int32_t   tx_eof;         /* 传输层 EOF（sticky） */
    int32_t   tx_err;         /* 传输层错误（每次 io 前清） */
} sc_ssl_dev;

/* 传输回调（ctx = sc_ssl_dev*）：把密文搬运到/自底层 com。 */
static int sc_ssl_tx_send(void *ctx, const void *buf, uint32_t len) {
    sc_ssl_dev *d = (sc_ssl_dev *)ctx;
    com *t = d->transport;
    if (!t || !t->write) { d->tx_err = 1; return -1; }
    uint32_t n = len;
    int32_t r = t->write(t, (void *)buf, &n);
    if (r < 0) { d->tx_err = 1; return -1; }
    return (int)n;            /* 实写（阻塞传输=len；部分写时 ssl_flush_out 续发剩余） */
}
static int sc_ssl_tx_recv(void *ctx, void *buf, uint32_t len) {
    sc_ssl_dev *d = (sc_ssl_dev *)ctx;
    com *t = d->transport;
    if (!t || !t->read) { d->tx_err = 1; return -1; }
    uint32_t n = len;
    int32_t r = t->read(t, buf, &n);
    if (r < 0) { d->tx_err = 1; return -1; }
    if (r == IO_EOF)  { d->tx_eof = 1; return 0; }
    if (r == IO_AGAIN) return 0;     /* 暂无数据 → ssl 视作 WANT_IO */
    return (int)n;
}

/* com[...] 句柄：limit 缓冲紧随 limit 结构之后分配 */
static void *sc_ssl_limit_data(limit *s) { return (char *)s + sizeof(limit); }
static limit *sc_ssl_alloc(com *_this, uint32_t size, void *ending) {
    (void)_this;
    limit *s = (limit *)sc_chunk0(sizeof(limit) + (size ? size : 1));
    if (!s) return NULL;
    s->size   = size;
    s->len    = 0;
    s->data   = sc_ssl_limit_data;
    s->ending = (int32_t (*)(limit *))ending;
    return s;
}
static void sc_ssl_free_limit(com *_this, limit *s) { (void)_this; sc_recycle(s); }

/* 设备读：经 TLS 解密读至多 *size 字节，回写实读数。阻塞传输下 SSL_WANT_IO 在循环内解除。
 *   有明文 → 0 / 对端干净关闭或传输 EOF → IO_EOF / 错误 → <0。 */
static int32_t sc_ssl_com_read(com *_this, void *data, uint32_t *size) {
    sc_ssl_dev *d = (sc_ssl_dev *)_this;
    if (!size) return -1;
    uint32_t want = *size;
    *size = 0;
    for (;;) {
        d->tx_err = 0;
        int r = ssl_read(d->ssl, data, want);
        if (r > 0) { *size = (uint32_t)r; return 0; }
        if (r == 0) return IO_EOF;                 /* TLS close_notify */
        if (r == SSL_WANT_IO) {
            if (d->tx_err) return -1;
            if (d->tx_eof) return IO_EOF;          /* 传输层 EOF 且无更多明文 */
            continue;                              /* 阻塞传输：泵入更多密文后重试 */
        }
        return -1;
    }
}

/* 设备写：经 TLS 加密写至多 *size 字节，回写实写数。 */
static int32_t sc_ssl_com_write(com *_this, void *buf, uint32_t *size) {
    sc_ssl_dev *d = (sc_ssl_dev *)_this;
    if (!size) return -1;
    uint32_t want = *size;
    *size = 0;
    for (;;) {
        d->tx_err = 0;
        int r = ssl_write(d->ssl, buf, want);
        if (r > 0) { *size = (uint32_t)r; return 0; }
        if (r == SSL_WANT_IO) {
            if (d->tx_err) return -1;
            if (d->tx_eof) return IO_EOF;
            continue;
        }
        return -1;
    }
}

static int32_t sc_ssl_com_error(com *_this) {
    sc_ssl_dev *d = (sc_ssl_dev *)_this;
    return d->tx_err ? -1 : 0;
}

/* 读/写就绪：委托底层传输（密文驱动）；底层无 readable 则恒就绪。
 * 注：TLS 记录缓冲下就绪语义近似——本同步设备不接 async_loop，仅供完备性。 */
static int32_t sc_ssl_readable(com *_this, void **id) {
    sc_ssl_dev *d = (sc_ssl_dev *)_this;
    if (d->transport && d->transport->readable)
        return d->transport->readable(d->transport, id);
    *id = NULL;
    return 1;
}
static int32_t sc_ssl_writable(com *_this, void **id) {
    sc_ssl_dev *d = (sc_ssl_dev *)_this;
    if (d->transport && d->transport->writable)
        return d->transport->writable(d->transport, id);
    *id = NULL;
    return 1;
}

/* 关闭：释放 TLS 上下文（不发 close_notify，简洁优先）；owns_transport 时关底层；释放设备。 */
static int32_t sc_ssl_com_close(com *_this) {
    sc_ssl_dev *d = (sc_ssl_dev *)_this;
    int32_t r = 0;
    if (d->ssl) { ssl_free(d->ssl); d->ssl = NULL; }
    if (d->owns_transport && d->transport && d->transport->close)
        r = d->transport->close(d->transport);
    sc_recycle(d);
    return r;
}

struct com *ssl_com(struct com *transport, char *host, int32_t verify) {
    if (!transport || !transport->read || !transport->write) return NULL;
    void *s = ssl_client_new(host, verify);
    if (!s) return NULL;                       /* none 后端或建连失败 */

    sc_ssl_dev *d = (sc_ssl_dev *)sc_chunk0(sizeof(sc_ssl_dev));
    if (!d) { ssl_free(s); return NULL; }
    d->ssl       = s;
    d->transport = transport;
    ssl_set_transport(s, (void *)sc_ssl_tx_send, (void *)sc_ssl_tx_recv, d);

    /* 同步驱动握手（阻塞传输：循环至完成/失败） */
    int r;
    while ((r = ssl_handshake(s)) == SSL_WANT_IO) {
        if (d->tx_err || d->tx_eof) break;
    }
    if (r != SSL_OK) { ssl_free(s); sc_recycle(d); return NULL; }

    d->com.dev      = d;
    d->com.alloc    = sc_ssl_alloc;
    d->com.free     = sc_ssl_free_limit;
    d->com.read     = sc_ssl_com_read;
    d->com.write    = sc_ssl_com_write;
    d->com.error    = sc_ssl_com_error;
    d->com.readable = sc_ssl_readable;
    d->com.writable = sc_ssl_writable;
    d->com.close    = sc_ssl_com_close;
    return &d->com;
}
