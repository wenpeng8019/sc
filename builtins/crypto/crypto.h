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

#endif /* SC_CRYPTO_H */
