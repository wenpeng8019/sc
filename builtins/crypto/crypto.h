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
void crypto_sha256(void *data, uint64_t len, uint8_t *out);

/* HMAC-SHA256（RFC 2104）：key[0..keylen) 对 data[0..len) -> out[0..32) */
void crypto_hmac_sha256(void *key, uint64_t keylen,
                        void *data, uint64_t len, uint8_t *out);

/* HKDF-SHA256（RFC 5869）：salt/info 可为 NULL（对应长度传 0）；
 *   out[0..outlen)，outlen <= 255*32。 */
void crypto_hkdf_sha256(void *salt, uint64_t saltlen,
                        void *ikm,  uint64_t ikmlen,
                        void *info, uint64_t infolen,
                        uint8_t *out, uint64_t outlen);

/* SHA-1 一次性摘要：data[0..len) -> out[0..20)。
 *   已不具碰撞抗性，仅用于 WebSocket 握手等遗留协议互通，勿用于新安全用途。 */
void crypto_sha1(void *data, uint64_t len, uint8_t *out);

/* Base64 编码（RFC 4648）：data[0..len) -> out（不补 0），
 *   返回写入字符数 ((len+2)/3)*4。out 须 >= 该长度。 */
int32_t crypto_base64(void *data, uint64_t len, char *out);

/* ============ 第二期 · 批1：流密码 / MAC / AEAD / 曲线 ============ */

#define SC_CHACHA20_KEY    32u  /* ChaCha20 密钥字节数 */
#define SC_CHACHA20_NONCE  12u  /* ChaCha20 nonce 字节数（RFC 8439） */
#define SC_POLY1305_TAG    16u  /* Poly1305 / AEAD 标签字节数 */
#define SC_X25519_LEN      32u  /* X25519 标量 / 坐标 / 共享密钥字节数 */

/* ChaCha20 流密码（RFC 8439 §2.4）：key[32]、nonce[12]、起始块 counter；
 *   对 data[0..len) 异或密钥流 -> out[0..len)（out 可与 data 同址）。 */
void crypto_chacha20(uint8_t *key, uint8_t *nonce, uint32_t counter,
                     void *data, uint64_t len, uint8_t *out);

/* Poly1305 一次性 MAC（RFC 8439 §2.5）：key[32] 对 data[0..len) -> out[0..16)。
 *   每个 key 只能用于单条消息。 */
void crypto_poly1305(uint8_t *key, void *data, uint64_t len, uint8_t *out);

/* ChaCha20-Poly1305 AEAD 封装（RFC 8439 §2.8）：
 *   key[32]、nonce[12]、aad[0..aadlen)（可 NULL/0）；
 *   plain[0..plen) -> cipher[0..plen) + tag[0..16)。 */
void crypto_aead_seal(uint8_t *key, uint8_t *nonce,
                      void *aad, uint64_t aadlen,
                      void *plain, uint64_t plen,
                      uint8_t *cipher, uint8_t *tag);

/* AEAD 解封：校验 tag 后解密 cipher[0..clen) -> plain[0..clen)。
 *   返回 0=成功，-1=认证失败（plain 不可信）。 */
int32_t crypto_aead_open(uint8_t *key, uint8_t *nonce,
                         void *aad, uint64_t aadlen,
                         void *cipher, uint64_t clen,
                         uint8_t *tag, uint8_t *plain);

/* X25519 标量乘（RFC 7748 §5）：out[32] = scalar[32] · point[32]。 */
void crypto_x25519(uint8_t *out, uint8_t *scalar, uint8_t *point);

/* X25519 基点乘：out[32] = scalar[32] · 9（私钥导出公钥）。 */
void crypto_x25519_base(uint8_t *out, uint8_t *scalar);

/* ============ 第二期 · 批2：SHA-512 家族 / PBKDF2 / SHA-3 ============ */

#define SC_SHA512_LEN    64u   /* SHA-512 摘要字节数 */
#define SC_SHA384_LEN    48u   /* SHA-384 摘要字节数 */
#define SC_SHA3_256_LEN  32u   /* SHA3-256 摘要字节数 */
#define SC_SHA3_512_LEN  64u   /* SHA3-512 摘要字节数 */

/* SHA-512 / SHA-384 一次性摘要（FIPS 180-4）。 */
void crypto_sha512(void *data, uint64_t len, uint8_t *out);
void crypto_sha384(void *data, uint64_t len, uint8_t *out);

/* HMAC-SHA512（RFC 4231）：key[0..keylen) 对 data[0..len) -> out[0..64)。 */
void crypto_hmac_sha512(void *key, uint64_t keylen,
                        void *data, uint64_t len, uint8_t *out);

/* PBKDF2-HMAC-SHA256（RFC 8018）：pw 经 salt 加盐迭代 iters 轮 -> out[0..outlen)。 */
void crypto_pbkdf2_sha256(void *pw, uint64_t pwlen,
                          void *salt, uint64_t saltlen,
                          uint32_t iters, uint8_t *out, uint64_t outlen);

/* SHA-3（FIPS 202）固定长摘要。 */
void crypto_sha3_256(void *data, uint64_t len, uint8_t *out);
void crypto_sha3_512(void *data, uint64_t len, uint8_t *out);

/* SHAKE（FIPS 202）可扩展输出函数：out[0..outlen)。 */
void crypto_shake128(void *data, uint64_t len, uint8_t *out, uint64_t outlen);
void crypto_shake256(void *data, uint64_t len, uint8_t *out, uint64_t outlen);

/* ============ 第二期 · 批3：AES（CTR/CBC/GCM）/ Ed25519 ============ */

#define SC_AES_BLOCK     16u   /* AES 分组字节数 */
#define SC_GCM_TAG       16u   /* GCM 标签字节数 */
#define SC_ED25519_SIG   64u   /* Ed25519 签名字节数 */
#define SC_ED25519_KEY   32u   /* Ed25519 公钥/种子字节数 */

/* AES-CTR（SP 800-38A）：keybits=128/256；iv 为 16 字节计数块；加解密同一函数。 */
void crypto_aes_ctr(uint8_t *key, uint32_t keybits, uint8_t *iv,
                    void *data, uint64_t len, uint8_t *out);

/* AES-CBC（SP 800-38A）：len 须为 16 倍数。 */
void crypto_aes_cbc_encrypt(uint8_t *key, uint32_t keybits, uint8_t *iv,
                            void *data, uint64_t len, uint8_t *out);
void crypto_aes_cbc_decrypt(uint8_t *key, uint32_t keybits, uint8_t *iv,
                            void *data, uint64_t len, uint8_t *out);

/* AES-GCM AEAD（SP 800-38D）。 */
void crypto_aes_gcm_seal(uint8_t *key, uint32_t keybits, void *iv, uint64_t ivlen,
                         void *aad, uint64_t aadlen, void *plain, uint64_t plen,
                         uint8_t *cipher, uint8_t *tag);
int32_t crypto_aes_gcm_open(uint8_t *key, uint32_t keybits, void *iv, uint64_t ivlen,
                            void *aad, uint64_t aadlen, void *cipher, uint64_t clen,
                            uint8_t *tag, uint8_t *plain);

/* Ed25519 签名（RFC 8032）。 */
void crypto_ed25519_pubkey(uint8_t *pub, uint8_t *seed);
void crypto_ed25519_sign(uint8_t *sig, void *msg, uint64_t mlen,
                         uint8_t *seed, uint8_t *pub);
int32_t crypto_ed25519_verify(uint8_t *sig, void *msg, uint64_t mlen, uint8_t *pub);

#endif /* SC_CRYPTO_H */
