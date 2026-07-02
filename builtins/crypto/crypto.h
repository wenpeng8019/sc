/* crypto.h —— sc 自我独立密码学内置模块的 C ABI 契约
 *
 * 唯一事实源：与同目录 crypto.sc 的 @fnc 声明逐一对应（类型必须精确一致，
 *   因 scc 既经本头 #include 进类型，又按 crypto.sc 生成等价 extern 原型，
 *   两者须为同一 C 类型，故长度统一 uint64_t、缓冲统一 uint8_t*）。
 * 实现：同目录 crypto_impl.c —— 代码内置、自构建、零外部库链接
 *   （不链接 mbedcrypto / OpenSSL 等）。
 *
 * 第一期范围：哈希 / MAC / KDF（FIPS 180-4 / RFC 2104 / RFC 5869），
 *   均可由官方 KAT 向量验证。后续按模块铺开 SHA-512 / AES / ChaCha20-Poly1305
 *   / X25519 / Ed25519（曲线与 AEAD 以 vendor 权威实现内置）。
 */
#ifndef SC_CRYPTO_H
#define SC_CRYPTO_H

#include <stdint.h>

#define SC_SHA256_LEN    32u   /* SHA-256 摘要字节数 */
#define SC_SHA256_BLOCK  64u   /* SHA-256 分组字节数 */
#define SC_SHA1_LEN      20u   /* SHA-1 摘要字节数（仅遗留协议互通） */

/* SHA-256 一次性摘要：data[0..len) -> out[0..32) */
void sc_crypto_sha256(void *data, uint64_t len, uint8_t *out);

/* HMAC-SHA256（RFC 2104）：key[0..keylen) 对 data[0..len) -> out[0..32) */
void sc_crypto_hmac_sha256(void *key, uint64_t keylen,
                        void *data, uint64_t len, uint8_t *out);

/* HKDF-SHA256（RFC 5869）：salt/info 可为 NULL（对应长度传 0）；
 *   out[0..outlen)，outlen <= 255*32。 */
void sc_crypto_hkdf_sha256(void *salt, uint64_t saltlen,
                        void *ikm,  uint64_t ikmlen,
                        void *info, uint64_t infolen,
                        uint8_t *out, uint64_t outlen);

/* SHA-1 一次性摘要：data[0..len) -> out[0..20)。
 *   已不具碰撞抗性，仅用于 WebSocket 握手等遗留协议互通，勿用于新安全用途。 */
void sc_crypto_sha1(void *data, uint64_t len, uint8_t *out);

/* Base64 编码（RFC 4648）：data[0..len) -> out（不补 0），
 *   返回写入字符数 ((len+2)/3)*4。out 须 >= 该长度。 */
int32_t sc_crypto_base64(void *data, uint64_t len, char *out);

/* ============ 第二期 · 批1：流密码 / MAC / AEAD / 曲线 ============ */

#define SC_CHACHA20_KEY    32u  /* ChaCha20 密钥字节数 */
#define SC_CHACHA20_NONCE  12u  /* ChaCha20 nonce 字节数（RFC 8439） */
#define SC_POLY1305_TAG    16u  /* Poly1305 / AEAD 标签字节数 */
#define SC_X25519_LEN      32u  /* X25519 标量 / 坐标 / 共享密钥字节数 */

/* ChaCha20 流密码（RFC 8439 §2.4）：key[32]、nonce[12]、起始块 counter；
 *   对 data[0..len) 异或密钥流 -> out[0..len)（out 可与 data 同址）。 */
void sc_crypto_chacha20(uint8_t *key, uint8_t *nonce, uint32_t counter,
                     void *data, uint64_t len, uint8_t *out);

/* Poly1305 一次性 MAC（RFC 8439 §2.5）：key[32] 对 data[0..len) -> out[0..16)。
 *   每个 key 只能用于单条消息。 */
void sc_crypto_poly1305(uint8_t *key, void *data, uint64_t len, uint8_t *out);

/* ChaCha20-Poly1305 AEAD 封装（RFC 8439 §2.8）：
 *   key[32]、nonce[12]、aad[0..aadlen)（可 NULL/0）；
 *   plain[0..plen) -> cipher[0..plen) + tag[0..16)。 */
void sc_crypto_aead_seal(uint8_t *key, uint8_t *nonce,
                      void *aad, uint64_t aadlen,
                      void *plain, uint64_t plen,
                      uint8_t *cipher, uint8_t *tag);

/* AEAD 解封：校验 tag 后解密 cipher[0..clen) -> plain[0..clen)。
 *   返回 0=成功，-1=认证失败（plain 不可信）。 */
int32_t sc_crypto_aead_open(uint8_t *key, uint8_t *nonce,
                         void *aad, uint64_t aadlen,
                         void *cipher, uint64_t clen,
                         uint8_t *tag, uint8_t *plain);

/* X25519 标量乘（RFC 7748 §5）：out[32] = scalar[32] · point[32]。 */
void sc_crypto_x25519(uint8_t *out, uint8_t *scalar, uint8_t *point);

/* X25519 基点乘：out[32] = scalar[32] · 9（私钥导出公钥）。 */
void sc_crypto_x25519_base(uint8_t *out, uint8_t *scalar);

/* ============ 第二期 · 批2：SHA-512 家族 / PBKDF2 / SHA-3 ============ */

#define SC_SHA512_LEN    64u   /* SHA-512 摘要字节数 */
#define SC_SHA384_LEN    48u   /* SHA-384 摘要字节数 */
#define SC_SHA3_256_LEN  32u   /* SHA3-256 摘要字节数 */
#define SC_SHA3_512_LEN  64u   /* SHA3-512 摘要字节数 */

/* SHA-512 / SHA-384 一次性摘要（FIPS 180-4）。 */
void sc_crypto_sha512(void *data, uint64_t len, uint8_t *out);
void sc_crypto_sha384(void *data, uint64_t len, uint8_t *out);

/* HMAC-SHA512（RFC 4231）：key[0..keylen) 对 data[0..len) -> out[0..64)。 */
void sc_crypto_hmac_sha512(void *key, uint64_t keylen,
                        void *data, uint64_t len, uint8_t *out);

/* PBKDF2-HMAC-SHA256（RFC 8018）：pw 经 salt 加盐迭代 iters 轮 -> out[0..outlen)。 */
void sc_crypto_pbkdf2_sha256(void *pw, uint64_t pwlen,
                          void *salt, uint64_t saltlen,
                          uint32_t iters, uint8_t *out, uint64_t outlen);

/* SHA-3（FIPS 202）固定长摘要。 */
void sc_crypto_sha3_256(void *data, uint64_t len, uint8_t *out);
void sc_crypto_sha3_512(void *data, uint64_t len, uint8_t *out);

/* SHAKE（FIPS 202）可扩展输出函数：out[0..outlen)。 */
void sc_crypto_shake128(void *data, uint64_t len, uint8_t *out, uint64_t outlen);
void sc_crypto_shake256(void *data, uint64_t len, uint8_t *out, uint64_t outlen);

/* ============ 第二期 · 批3：AES（CTR/CBC/GCM）/ Ed25519 ============ */

#define SC_AES_BLOCK     16u   /* AES 分组字节数 */
#define SC_GCM_TAG       16u   /* GCM 标签字节数 */
#define SC_ED25519_SIG   64u   /* Ed25519 签名字节数 */
#define SC_ED25519_KEY   32u   /* Ed25519 公钥/种子字节数 */

/* AES-CTR（SP 800-38A）：keybits=128/256；iv 为 16 字节计数块；加解密同一函数。 */
void sc_crypto_aes_ctr(uint8_t *key, uint32_t keybits, uint8_t *iv,
                       void *data, uint64_t len, uint8_t *out);

/* AES-CBC（SP 800-38A）：len 须为 16 倍数。 */
void sc_crypto_aes_cbc_encrypt(uint8_t *key, uint32_t keybits, uint8_t *iv,
                               void *data, uint64_t len, uint8_t *out);
void sc_crypto_aes_cbc_decrypt(uint8_t *key, uint32_t keybits, uint8_t *iv,
                               void *data, uint64_t len, uint8_t *out);

/* AES-GCM AEAD（SP 800-38D）。 */
void sc_crypto_aes_gcm_seal(uint8_t *key, uint32_t keybits, void *iv, uint64_t ivlen,
                            void *aad, uint64_t aadlen, void *plain, uint64_t plen,
                            uint8_t *cipher, uint8_t *tag);
int32_t sc_crypto_aes_gcm_open(uint8_t *key, uint32_t keybits, void *iv, uint64_t ivlen,
                               void *aad, uint64_t aadlen, void *cipher, uint64_t clen,
                               uint8_t *tag, uint8_t *plain);

/* Ed25519 签名（RFC 8032）。 */
void sc_crypto_ed25519_pubkey(uint8_t *pub, uint8_t *seed);
void sc_crypto_ed25519_sign(uint8_t *sig, void *msg, uint64_t mlen,
                            uint8_t *seed, uint8_t *pub);
int32_t sc_crypto_ed25519_verify(uint8_t *sig, void *msg, uint64_t mlen, uint8_t *pub);

/* ===== 第二期 · 批4：遗留 / 弱算法（仅遗留互通；勿用于新安全设计） ===== */
#define SC_MD5_LEN        16u  /* MD5 摘要字节数 */
#define SC_RIPEMD160_LEN  20u  /* RIPEMD-160 摘要字节数 */
#define SC_DES_BLOCK       8u  /* DES 分组字节数 */
#define SC_DES_KEY         8u  /* DES 密钥字节数 */
#define SC_DES3_KEY       24u  /* 3DES 密钥束字节数（K1||K2||K3） */

/* MD5（RFC 1321）：out 须 >= 16 字节。⚠ 已被碰撞攻破，仅遗留校验。 */
void sc_crypto_md5(void *data, uint64_t len, uint8_t *out);
/* RIPEMD-160：out 须 >= 20 字节。 */
void sc_crypto_ripemd160(void *data, uint64_t len, uint8_t *out);

/* DES（FIPS 46-3）：key 8 字节；len 须为 8 倍数。⚠ 弱密钥，仅遗留互通。 */
void sc_crypto_des_ecb_encrypt(uint8_t *key, void *data, uint64_t len, uint8_t *out);
void sc_crypto_des_ecb_decrypt(uint8_t *key, void *data, uint64_t len, uint8_t *out);
void sc_crypto_des_cbc_encrypt(uint8_t *key, uint8_t *iv, void *data, uint64_t len, uint8_t *out);
void sc_crypto_des_cbc_decrypt(uint8_t *key, uint8_t *iv, void *data, uint64_t len, uint8_t *out);

/* 3DES EDE（密钥束 24 字节）：加密 = E_K3(D_K2(E_K1(块)))。 */
void sc_crypto_des3_ecb_encrypt(uint8_t *key, void *data, uint64_t len, uint8_t *out);
void sc_crypto_des3_ecb_decrypt(uint8_t *key, void *data, uint64_t len, uint8_t *out);
void sc_crypto_des3_cbc_encrypt(uint8_t *key, uint8_t *iv, void *data, uint64_t len, uint8_t *out);
void sc_crypto_des3_cbc_decrypt(uint8_t *key, uint8_t *iv, void *data, uint64_t len, uint8_t *out);

/* AES-ECB（SP 800-38A）：keybits=128/256；len 须为 16 倍数。⚠ 暴露分组模式。 */
void sc_crypto_aes_ecb_encrypt(uint8_t *key, uint32_t keybits, void *data, uint64_t len, uint8_t *out);
void sc_crypto_aes_ecb_decrypt(uint8_t *key, uint32_t keybits, void *data, uint64_t len, uint8_t *out);

/* ============ 第二期 · 批5：算法簇补全（哈希/MAC/对称/AEAD/编码/随机/国密）============ */
#define SC_SHA224_LEN     28u   /* SHA-224 摘要字节数 */
#define SC_SHA512_256_LEN 32u   /* SHA-512/256 摘要字节数 */
#define SC_SHA3_224_LEN   28u   /* SHA3-224 摘要字节数 */
#define SC_SHA3_384_LEN   48u   /* SHA3-384 摘要字节数 */
#define SC_SM3_LEN        32u   /* SM3 摘要字节数 */
#define SC_CMAC_TAG       16u   /* AES-CMAC 标签字节数 */
#define SC_SM4_BLOCK      16u   /* SM4 分组字节数 */
#define SC_SM4_KEY        16u   /* SM4 密钥字节数 */
#define SC_XNONCE         24u   /* XChaCha20 扩展 nonce 字节数 */

/* —— 哈希补全 —— */
void sc_crypto_sha224(void *data, uint64_t len, uint8_t *out);
void sc_crypto_sha512_256(void *data, uint64_t len, uint8_t *out);
void sc_crypto_sha3_224(void *data, uint64_t len, uint8_t *out);
void sc_crypto_sha3_384(void *data, uint64_t len, uint8_t *out);
void sc_crypto_sm3(void *data, uint64_t len, uint8_t *out);

/* —— MAC：AES-CMAC（SP 800-38B / RFC 4493）—— */
void sc_crypto_aes_cmac(uint8_t *key, uint32_t keybits, void *data, uint64_t len, uint8_t *out);

/* —— 对称：AES-CFB128 / OFB（SP 800-38A），任意长度 —— */
void sc_crypto_aes_cfb_encrypt(uint8_t *key, uint32_t keybits, uint8_t *iv, void *data, uint64_t len, uint8_t *out);
void sc_crypto_aes_cfb_decrypt(uint8_t *key, uint32_t keybits, uint8_t *iv, void *data, uint64_t len, uint8_t *out);
void sc_crypto_aes_ofb(uint8_t *key, uint32_t keybits, uint8_t *iv, void *data, uint64_t len, uint8_t *out);

/* —— 国密 SM4（GM/T 0002-2012）：分组/密钥 16 字节 —— */
void sc_crypto_sm4_ecb_encrypt(uint8_t *key, void *data, uint64_t len, uint8_t *out);
void sc_crypto_sm4_ecb_decrypt(uint8_t *key, void *data, uint64_t len, uint8_t *out);
void sc_crypto_sm4_cbc_encrypt(uint8_t *key, uint8_t *iv, void *data, uint64_t len, uint8_t *out);
void sc_crypto_sm4_cbc_decrypt(uint8_t *key, uint8_t *iv, void *data, uint64_t len, uint8_t *out);

/* —— AEAD：XChaCha20-Poly1305（24 字节 nonce）—— */
void sc_crypto_xaead_seal(uint8_t *key, uint8_t *nonce, void *aad, uint64_t aadlen,
                          void *plain, uint64_t plen, uint8_t *cipher, uint8_t *tag);
int32_t sc_crypto_xaead_open(uint8_t *key, uint8_t *nonce, void *aad, uint64_t aadlen,
                             void *cipher, uint64_t clen, uint8_t *tag, uint8_t *plain);

/* —— AEAD：AES-CCM（SP 800-38C / RFC 3610）—— */
void sc_crypto_aes_ccm_seal(uint8_t *key, uint32_t keybits, uint8_t *nonce, uint64_t noncelen,
                            void *aad, uint64_t aadlen, void *plain, uint64_t plen,
                            uint8_t *cipher, uint8_t *tag, uint64_t taglen);
int32_t sc_crypto_aes_ccm_open(uint8_t *key, uint32_t keybits, uint8_t *nonce, uint64_t noncelen,
                               void *aad, uint64_t aadlen, void *cipher, uint64_t clen,
                               uint8_t *tag, uint64_t taglen, uint8_t *plain);

/* —— 编码：Base64URL / Base64 解码 / Hex —— */
int32_t sc_crypto_base64url(void *data, uint64_t len, char *out);
int32_t sc_crypto_base64_decode(char *b64, uint64_t len, uint8_t *out);
int32_t sc_crypto_hex_encode(void *data, uint64_t len, char *out);
int32_t sc_crypto_hex_decode(char *hex, uint64_t len, uint8_t *out);

/* —— 随机数与工具 —— */
int32_t sc_crypto_random(uint8_t *out, uint64_t len);
int32_t sc_crypto_verify(void *a, void *b, uint64_t len);

/* ============ 大类二 · 代理算法（RSA，转发 ssl 后端；无后端安全失败）============
 * 默认实现为 __attribute__((weak)) 安全失败桩；编译进 ssl 后端时由其提供强符号覆盖。
 * hash_id 取值如下（与 OpenSSL/mbedTLS NID 解耦的内部约定）。 */
#define SC_HASH_SHA1   1
#define SC_HASH_SHA224 3
#define SC_HASH_SHA256 4
#define SC_HASH_SHA384 5
#define SC_HASH_SHA512 6

int32_t sc_crypto_rsa_backend(void);                 /* 0=none / 1=mbedtls / 2=openssl */
void   *sc_crypto_rsa_keygen(uint32_t bits, uint32_t pub_e);
void    sc_crypto_rsa_free(void *k);
void   *sc_crypto_rsa_import(void *buf, uint64_t len, int32_t is_private, int32_t fmt);
int32_t sc_crypto_rsa_export(void *k, int32_t is_private, int32_t fmt, uint8_t *out, uint64_t cap);
int32_t sc_crypto_rsa_sign_pkcs1(void *k, int32_t hash_id, void *digest, uint64_t dlen, uint8_t *sig, uint64_t sigcap);
int32_t sc_crypto_rsa_verify_pkcs1(void *k, int32_t hash_id, void *digest, uint64_t dlen, void *sig, uint64_t siglen);
int32_t sc_crypto_rsa_sign_pss(void *k, int32_t hash_id, void *digest, uint64_t dlen, uint8_t *sig, uint64_t sigcap);
int32_t sc_crypto_rsa_verify_pss(void *k, int32_t hash_id, void *digest, uint64_t dlen, void *sig, uint64_t siglen);
int32_t sc_crypto_rsa_encrypt_oaep(void *k, int32_t hash_id, void *plain, uint64_t plen, uint8_t *out, uint64_t cap);
int32_t sc_crypto_rsa_decrypt_oaep(void *k, int32_t hash_id, void *cipher, uint64_t clen, uint8_t *out, uint64_t cap);
int32_t sc_crypto_rsa_encrypt_pkcs1(void *k, void *plain, uint64_t plen, uint8_t *out, uint64_t cap);
int32_t sc_crypto_rsa_decrypt_pkcs1(void *k, void *cipher, uint64_t clen, uint8_t *out, uint64_t cap);

#endif /* SC_CRYPTO_H */
