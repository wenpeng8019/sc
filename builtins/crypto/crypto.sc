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

# ================================================================
# 第二期 · 批1：流密码 / MAC / AEAD / 椭圆曲线密钥协商
#   （自实现 + RFC 8439 / RFC 7748 官方 KAT 验证；解锁自通讯加密信道）
# ================================================================

# ---------------- ChaCha20 流密码（RFC 8439 §2.4）----------------
# key[0..32)、nonce[0..12)、起始块计数 counter；对 data[0..len) 逐字节异或
# 密钥流 -> out[0..len)（out 可与 data 同址，原地加解密）。加解密同一函数。
@fnc crypto_chacha20:: key: u1&, nonce: u1&, counter: u4, data: &, len: u8, out: u1&

# ---------------- Poly1305 一次性 MAC（RFC 8439 §2.5）----------------
# 一次性密钥 key[0..32) 对 data[0..len) 求 16 字节标签 -> out[0..16)。
# 安全前提：每条消息的 key 只能用一次（通常由 ChaCha20 块 0 派生）。
@fnc crypto_poly1305:: key: u1&, data: &, len: u8, out: u1&

# ---------------- ChaCha20-Poly1305 AEAD（RFC 8439 §2.8）----------------
# 封装：key[0..32)、nonce[0..12)、附加数据 aad[0..aadlen)（可 nil/0）；
#   明文 plain[0..plen) -> 密文 cipher[0..plen) + 认证标签 tag[0..16)。
@fnc crypto_aead_seal:: key: u1&, nonce: u1&, aad: &, aadlen: u8, plain: &, plen: u8, cipher: u1&, tag: u1&
# 解封：校验 tag 后解密 cipher[0..clen) -> plain[0..clen)。
#   返回 0=成功，-1=认证失败（此时 plain 内容不可信，勿使用）。
@fnc crypto_aead_open:: i4, key: u1&, nonce: u1&, aad: &, aadlen: u8, cipher: &, clen: u8, tag: u1&, plain: u1&

# ---------------- X25519 椭圆曲线 DH（RFC 7748 §5）----------------
# 标量乘：out[0..32) = scalar[0..32) · point[0..32)（Montgomery u 坐标）。
@fnc crypto_x25519:: out: u1&, scalar: u1&, point: u1&
# 基点乘：out[0..32) = scalar[0..32) · 基点(9)，即由私钥导出公钥。
@fnc crypto_x25519_base:: out: u1&, scalar: u1&

# ================================================================
# 第二期 · 批2：SHA-512 家族 / PBKDF2 / SHA-3（Keccak）
#   （自实现 + FIPS 180-4 / 202、RFC 4231、PBKDF2-SHA256 公开 KAT 验证）
# ================================================================

# ---------------- SHA-512 / SHA-384（FIPS 180-4）----------------
# 一次性摘要：data[0..len) -> out[0..64)（512）或 out[0..48)（384）。
@fnc crypto_sha512:: data: &, len: u8, out: u1&
@fnc crypto_sha384:: data: &, len: u8, out: u1&

# ---------------- HMAC-SHA512（RFC 4231）----------------
# key[0..keylen) 对 data[0..len) -> out[0..64)。
@fnc crypto_hmac_sha512:: key: &, keylen: u8, data: &, len: u8, out: u1&

# ---------------- PBKDF2-HMAC-SHA256（RFC 8018）----------------
# 口令 pw 经 salt 加盐、迭代 iters 轮派生 out[0..outlen)。iters 越大越抗暴力。
@fnc crypto_pbkdf2_sha256:: pw: &, pwlen: u8, salt: &, saltlen: u8, iters: u4, out: u1&, outlen: u8

# ---------------- SHA-3 / SHAKE（FIPS 202）----------------
# 固定长摘要：SHA3-256 -> out[0..32)，SHA3-512 -> out[0..64)。
@fnc crypto_sha3_256:: data: &, len: u8, out: u1&
@fnc crypto_sha3_512:: data: &, len: u8, out: u1&
# 可扩展输出（XOF）：输出任意长度 out[0..outlen)。
@fnc crypto_shake128:: data: &, len: u8, out: u1&, outlen: u8
@fnc crypto_shake256:: data: &, len: u8, out: u1&, outlen: u8

# ================================================================
# 第二期 · 批3：AES（CTR/CBC/GCM）/ Ed25519 签名
#   （自实现 + FIPS 197、SP 800-38A/D、RFC 8032 官方 KAT 验证）
# ================================================================

# ---------------- AES-CTR（SP 800-38A）----------------
# keybits 取 128 或 256；iv 为完整 16 字节初始计数块（整块 128 位大端自增）。
# 加解密同一函数；out 可与 data 同址。
@fnc crypto_aes_ctr:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&

# ---------------- AES-CBC（SP 800-38A）----------------
# len 须为 16 的倍数（填充由调用方负责）；iv 为 16 字节初始向量。
@fnc crypto_aes_cbc_encrypt:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_aes_cbc_decrypt:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&

# ---------------- AES-GCM AEAD（SP 800-38D）----------------
# iv 任意长度（12 字节最常用且最快）；aad 可 nil/0；plain -> cipher + tag[0..16)。
@fnc crypto_aes_gcm_seal:: key: u1&, keybits: u4, iv: &, ivlen: u8, aad: &, aadlen: u8, plain: &, plen: u8, cipher: u1&, tag: u1&
# 解封：校验 tag 后解密。返回 0=成功，-1=认证失败（plain 不可信）。
@fnc crypto_aes_gcm_open:: i4, key: u1&, keybits: u4, iv: &, ivlen: u8, aad: &, aadlen: u8, cipher: &, clen: u8, tag: u1&, plain: u1&

# ---------------- Ed25519 签名（RFC 8032）----------------
# 由 32 字节私钥种子 seed 导出 32 字节公钥 pub。
@fnc crypto_ed25519_pubkey:: pub: u1&, seed: u1&
# 签名：对 msg[0..mlen) 生成 64 字节签名 sig（需 seed 与其公钥 pub）。
@fnc crypto_ed25519_sign:: sig: u1&, msg: &, mlen: u8, seed: u1&, pub: u1&
# 验签：返回 0=有效，-1=无效。
@fnc crypto_ed25519_verify:: i4, sig: u1&, msg: &, mlen: u8, pub: u1&

# ================================================================
# 第二期 · 批4：遗留 / 弱算法（MD5/RIPEMD-160/DES/3DES/AES-ECB）
#   仅供与遗留系统互通；⚠ 勿用于新的安全设计。全部自实现 + 权威 KAT 验证。
# ================================================================

# ---------------- MD5（RFC 1321，16 字节摘要）----------------
# 一次性摘要：data[0..len) -> out[0..16)。out 须 >= 16 字节。
# ⚠ 已被实用碰撞攻破，绝不可用于签名/防篡改；仅作遗留校验和/协议互通。
@fnc crypto_md5:: data: &, len: u8, out: u1&

# ---------------- RIPEMD-160（20 字节摘要）----------------
# 一次性摘要：data[0..len) -> out[0..20)。out 须 >= 20 字节。
# 主要见于比特币地址等既有体系；新设计优先 SHA-256/SHA-3。
@fnc crypto_ripemd160:: data: &, len: u8, out: u1&

# ---------------- DES（FIPS 46-3，分组/密钥均 8 字节）----------------
# ⚠ 56 位有效密钥已可被暴力破解，绝不可用于新安全用途；仅遗留互通。
# ECB：len 须为 8 的倍数（填充由调用方负责）；加/解密分开。
@fnc crypto_des_ecb_encrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des_ecb_decrypt:: key: u1&, data: &, len: u8, out: u1&
# CBC：iv 为 8 字节初始向量；len 须为 8 的倍数。
@fnc crypto_des_cbc_encrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_des_cbc_decrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&

# ---------------- 3DES / Triple-DES（EDE 三密钥，密钥束 24 字节）----------------
# 加密 = E_K3(D_K2(E_K1(分组)))（解密反向）；K1=K2=K3 时退化为单 DES。
# ⚠ 安全裕度有限（NIST 已弃用），仅遗留互通；新设计请用 AES。
@fnc crypto_des3_ecb_encrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des3_ecb_decrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des3_cbc_encrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_des3_cbc_decrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&

# ---------------- AES-ECB（SP 800-38A，裸分组）----------------
# keybits 取 128 或 256；len 须为 16 的倍数。各分组独立加/解密、互不串链。
# ⚠ ECB 会暴露明文分组模式（相同明文块→相同密文块），除非确知用途否则用 CTR/CBC/GCM。
@fnc crypto_aes_ecb_encrypt:: key: u1&, keybits: u4, data: &, len: u8, out: u1&
@fnc crypto_aes_ecb_decrypt:: key: u1&, keybits: u4, data: &, len: u8, out: u1&
