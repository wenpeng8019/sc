# crypto —— sc 自我独立的密码学原语内置模块
# 唯一事实源：C ABI 契约见同目录 crypto.h，默认实现见 crypto_impl.c。
# 定位：代码内置、自构建、零外部库链接（不链接 mbedcrypto / OpenSSL 等）。
#   跨平台经由 builtins/platform.h。
#
# 用法：inc crypto.sc
#
# 第一期范围（哈希 / MAC / KDF / 编码，均可由官方 KAT 向量验证）：
#   SHA-256（FIPS 180-4） / HMAC-SHA256（RFC 2104） / HKDF-SHA256（RFC 5869）
#   SHA-1（FIPS 180-1，遗留协议互通） / Base64（RFC 4648）
#   —— 后两者由原 templates/utils/codec.sc 移植内置（codec 已作废）。
# 后续按模块铺开：SHA-512 / SHA-3 / PBKDF2 / AES（CBC/CTR/GCM）/
#   ChaCha20-Poly1305 / X25519 / Ed25519（曲线与 AEAD 以 vendor 权威实现内置）。
# 边界：RSA、NIST P-256/384 需大数运算，第一期不含（延后或由第三期 TLS 提供）。

# ---------------- SHA-256（FIPS 180-4）----------------
# 一次性摘要：data[0..len) -> out[0..32)。out 须 >= 32 字节。
@fnc crypto_sha256:: data: &, len: u8, out: u1&

# ---------------- HMAC-SHA256（RFC 2104）----------------
# key[0..keylen) 对 data[0..len) 求 MAC -> out[0..32)。out 须 >= 32 字节。
@fnc crypto_hmac_sha256:: key: &, keylen: u8, data: &, len: u8, out: u1&

# ---------------- HKDF-SHA256（RFC 5869）----------------
# salt/info 可为 nil（对应长度传 0）；out[0..outlen)，outlen <= 255*32。
@fnc crypto_hkdf_sha256:: salt: &, saltlen: u8, ikm: &, ikmlen: u8, info: &, infolen: u8, out: u1&, outlen: u8

# ---------------- SHA-1（FIPS 180-1）----------------
# 一次性摘要：data[0..len) -> out[0..20)。out 须 >= 20 字节。
# 已不具碰撞抗性，仅用于 WebSocket 握手等遗留协议互通，勿用于新安全用途。
@fnc crypto_sha1:: data: &, len: u8, out: u1&

# ---------------- Base64 编码（RFC 4648）----------------
# data[0..len) -> out（不补 0），返回写入字符数 ((len+2)/3)*4。out 须 >= 该长度。
@fnc crypto_base64:: i4, data: &, len: u8, out: char&
