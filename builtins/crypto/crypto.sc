# crypto —— sc 自我独立的密码学原语内置模块
# 唯一事实源：C ABI 契约见同目录 crypto.h，默认实现见 crypto_impl.c。
# 定位：代码内置、自构建、零外部库链接（不链接 mbedcrypto / OpenSSL 等）。
#   跨平台经由 builtins/platform.h。所有自实现算法均以权威 KAT 向量验证。
#
# 用法：inc crypto.sc
#
# 本模块按【两大类】组织，类下再按算法簇细分：
#   ┌─ 大类一 · 自实现积木（零外链，KAT 验证）
#   │    簇1 哈希摘要 / 簇2 消息认证码 MAC / 簇3 密钥派生 KDF / 簇4 对称分组密码 /
#   │    簇5 流密码 / 簇6 认证加密 AEAD / 簇7 椭圆曲线 / 簇8 编码 / 簇9 随机与工具
#   └─ 大类二 · 代理算法（大数运算重，自身不实现 → 转发 ssl 后端；未编译后端时安全失败）
#        簇R RSA（后续可扩 ECDSA / DH / NIST-P 曲线）
# 弱/遗留算法（SHA-1、MD5、RIPEMD-160、DES/3DES、AES-ECB 等）就近归入对应簇并显式标注，
#   仅供与既有系统互通，⚠ 勿用于新的安全设计。

# ════════════════════════════════════════════════════════════════
# 大类一 · 自实现密码学积木（零外链，KAT 验证）
# ════════════════════════════════════════════════════════════════

# ───────────────────────── 簇 1：哈希摘要 ─────────────────────────

# SHA-256 / SHA-224（FIPS 180-4）：一次性摘要 data[0..len) -> out（32 / 28 字节）。
@fnc crypto_sha256:: data: &, len: u8, out: u1&
@fnc crypto_sha224:: data: &, len: u8, out: u1&
# SHA-512 / SHA-384 / SHA-512-256（FIPS 180-4）：out 64 / 48 / 32 字节。
@fnc crypto_sha512:: data: &, len: u8, out: u1&
@fnc crypto_sha384:: data: &, len: u8, out: u1&
@fnc crypto_sha512_256:: data: &, len: u8, out: u1&
# SHA-3 家族（FIPS 202）：SHA3-224/256/384/512，out 28/32/48/64 字节。
@fnc crypto_sha3_224:: data: &, len: u8, out: u1&
@fnc crypto_sha3_256:: data: &, len: u8, out: u1&
@fnc crypto_sha3_384:: data: &, len: u8, out: u1&
@fnc crypto_sha3_512:: data: &, len: u8, out: u1&
# SHAKE 可扩展输出函数（XOF，FIPS 202）：输出任意长 out[0..outlen)。
@fnc crypto_shake128:: data: &, len: u8, out: u1&, outlen: u8
@fnc crypto_shake256:: data: &, len: u8, out: u1&, outlen: u8
# SM3（GM/T 0004-2012 国密）：out 32 字节。
@fnc crypto_sm3:: data: &, len: u8, out: u1&
# —— 弱/遗留摘要（仅互通，勿用于新安全用途）——
# SHA-1（FIPS 180-1）：out 20 字节；已无碰撞抗性，仅 WebSocket/TLS 遗留握手。
@fnc crypto_sha1:: data: &, len: u8, out: u1&
# MD5（RFC 1321）：out 16 字节；已被实用碰撞攻破，仅作遗留校验和。
@fnc crypto_md5:: data: &, len: u8, out: u1&
# RIPEMD-160：out 20 字节；主要见于比特币地址等既有体系。
@fnc crypto_ripemd160:: data: &, len: u8, out: u1&

# ──────────────────────── 簇 2：消息认证码 MAC ────────────────────────

# HMAC-SHA256（RFC 2104）：key 对 data 求 MAC -> out 32 字节。
@fnc crypto_hmac_sha256:: key: &, keylen: u8, data: &, len: u8, out: u1&
# HMAC-SHA512（RFC 4231）：-> out 64 字节。
@fnc crypto_hmac_sha512:: key: &, keylen: u8, data: &, len: u8, out: u1&
# Poly1305 一次性 MAC（RFC 8439 §2.5）：一次性 key[0..32) -> out 16 字节。
#   安全前提：每条消息的 key 只能用一次（通常由 ChaCha20 块 0 派生）。
@fnc crypto_poly1305:: key: u1&, data: &, len: u8, out: u1&
# AES-CMAC（NIST SP 800-38B / RFC 4493）：keybits=128/256 -> out 16 字节。
@fnc crypto_aes_cmac:: key: u1&, keybits: u4, data: &, len: u8, out: u1&

# ───────────────────────── 簇 3：密钥派生 KDF ─────────────────────────

# HKDF-SHA256（RFC 5869）：salt/info 可为 nil（长度传 0）；outlen <= 255*32。
@fnc crypto_hkdf_sha256:: salt: &, saltlen: u8, ikm: &, ikmlen: u8, info: &, infolen: u8, out: u1&, outlen: u8
# PBKDF2-HMAC-SHA256（RFC 8018）：口令经 salt 加盐迭代 iters 轮派生 out[0..outlen)。
@fnc crypto_pbkdf2_sha256:: pw: &, pwlen: u8, salt: &, saltlen: u8, iters: u4, out: u1&, outlen: u8

# ──────────────────── 簇 4：对称分组密码（含工作模式）────────────────────

# AES（FIPS 197）keybits=128/256；各模式 iv/计数块均 16 字节。
# CTR：整块 128 位大端自增；加解密同一函数，out 可与 data 同址。
@fnc crypto_aes_ctr:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
# CBC：len 须为 16 倍数（填充由调用方负责）。
@fnc crypto_aes_cbc_encrypt:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_aes_cbc_decrypt:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
# CFB-128：密文反馈，任意长度（无需填充），加/解密分开。
@fnc crypto_aes_cfb_encrypt:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_aes_cfb_decrypt:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
# OFB：输出反馈，任意长度；加解密同一函数。
@fnc crypto_aes_ofb:: key: u1&, keybits: u4, iv: u1&, data: &, len: u8, out: u1&
# SM4（GM/T 0002-2012 国密，分组/密钥均 16 字节）：ECB/CBC，加解密分开。
@fnc crypto_sm4_ecb_encrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_sm4_ecb_decrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_sm4_cbc_encrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_sm4_cbc_decrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
# —— 弱/遗留分组密码（仅互通）——
# AES-ECB（SP 800-38A 裸分组）：len 须为 16 倍数；⚠ 暴露明文分组模式。
@fnc crypto_aes_ecb_encrypt:: key: u1&, keybits: u4, data: &, len: u8, out: u1&
@fnc crypto_aes_ecb_decrypt:: key: u1&, keybits: u4, data: &, len: u8, out: u1&
# DES（FIPS 46-3，分组/密钥均 8 字节）：⚠ 56 位密钥可暴破。ECB/CBC，加解密分开。
@fnc crypto_des_ecb_encrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des_ecb_decrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des_cbc_encrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_des_cbc_decrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
# 3DES / Triple-DES（EDE 三密钥，密钥束 24 字节）：加密=E_K3(D_K2(E_K1(块)))。
@fnc crypto_des3_ecb_encrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des3_ecb_decrypt:: key: u1&, data: &, len: u8, out: u1&
@fnc crypto_des3_cbc_encrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&
@fnc crypto_des3_cbc_decrypt:: key: u1&, iv: u1&, data: &, len: u8, out: u1&

# ───────────────────────── 簇 5：流密码 ─────────────────────────

# ChaCha20（RFC 8439 §2.4）：key[32]/nonce[12]/起始计数 counter；逐字节异或密钥流。
#   加解密同一函数，out 可与 data 同址。
@fnc crypto_chacha20:: key: u1&, nonce: u1&, counter: u4, data: &, len: u8, out: u1&

# ───────────────────────── 簇 6：认证加密 AEAD ─────────────────────────

# ChaCha20-Poly1305（RFC 8439 §2.8）：key[32]/nonce[12]/aad 可 nil/0。
@fnc crypto_aead_seal:: key: u1&, nonce: u1&, aad: &, aadlen: u8, plain: &, plen: u8, cipher: u1&, tag: u1&
# 解封：校验 tag 后解密。返回 0=成功 / -1=认证失败（plain 不可信）。
@fnc crypto_aead_open:: i4, key: u1&, nonce: u1&, aad: &, aadlen: u8, cipher: &, clen: u8, tag: u1&, plain: u1&
# XChaCha20-Poly1305（扩展 24 字节 nonce，draft-irtf-cfrg-xchacha）：nonce 可安全随机生成。
@fnc crypto_xaead_seal:: key: u1&, nonce: u1&, aad: &, aadlen: u8, plain: &, plen: u8, cipher: u1&, tag: u1&
@fnc crypto_xaead_open:: i4, key: u1&, nonce: u1&, aad: &, aadlen: u8, cipher: &, clen: u8, tag: u1&, plain: u1&
# AES-GCM（SP 800-38D）：keybits=128/256；iv 任意长（12 字节最常用最快）。
@fnc crypto_aes_gcm_seal:: key: u1&, keybits: u4, iv: &, ivlen: u8, aad: &, aadlen: u8, plain: &, plen: u8, cipher: u1&, tag: u1&
@fnc crypto_aes_gcm_open:: i4, key: u1&, keybits: u4, iv: &, ivlen: u8, aad: &, aadlen: u8, cipher: &, clen: u8, tag: u1&, plain: u1&
# AES-CCM（SP 800-38C / RFC 3610）：nonce 7..13 字节；taglen 取 4..16（偶数）。
@fnc crypto_aes_ccm_seal:: key: u1&, keybits: u4, nonce: u1&, noncelen: u8, aad: &, aadlen: u8, plain: &, plen: u8, cipher: u1&, tag: u1&, taglen: u8
@fnc crypto_aes_ccm_open:: i4, key: u1&, keybits: u4, nonce: u1&, noncelen: u8, aad: &, aadlen: u8, cipher: &, clen: u8, tag: u1&, taglen: u8, plain: u1&

# ──────────────────── 簇 7：椭圆曲线（密钥协商与签名）────────────────────

# X25519 椭圆曲线 DH（RFC 7748 §5）。
# 标量乘：out[0..32) = scalar[0..32) · point[0..32)（Montgomery u 坐标）。
@fnc crypto_x25519:: out: u1&, scalar: u1&, point: u1&
# 基点乘：由私钥导出公钥。
@fnc crypto_x25519_base:: out: u1&, scalar: u1&
# Ed25519 签名（RFC 8032）。
@fnc crypto_ed25519_pubkey:: pub: u1&, seed: u1&
@fnc crypto_ed25519_sign:: sig: u1&, msg: &, mlen: u8, seed: u1&, pub: u1&
@fnc crypto_ed25519_verify:: i4, sig: u1&, msg: &, mlen: u8, pub: u1&

# ───────────────────────── 簇 8：编码 ─────────────────────────

# Base64（RFC 4648 标准字母表，补 =）：返回写入字符数 ((len+2)/3)*4。out 不补 0。
@fnc crypto_base64:: i4, data: &, len: u8, out: char&
# Base64URL（URL 安全字母表 -_，不补 =）：返回写入字符数。
@fnc crypto_base64url:: i4, data: &, len: u8, out: char&
# Base64/Base64URL 解码（自动识别两套字母表，忽略尾部 =）：返回字节数，<0 非法。
@fnc crypto_base64_decode:: i4, b64: char&, len: u8, out: u1&
# 十六进制编码（小写，不补 0）：返回写入字符数 2*len。
@fnc crypto_hex_encode:: i4, data: &, len: u8, out: char&
# 十六进制解码（大小写均可）：返回字节数 len/2，<0 非法（奇数长/非法字符）。
@fnc crypto_hex_decode:: i4, hex: char&, len: u8, out: u1&

# ──────────────────────── 簇 9：随机数与工具 ────────────────────────

# 密码学安全随机数（CSPRNG，取自 OS 熵源 getrandom/getentropy/BCryptGenRandom）：
#   填充 out[0..len)。返回 0=成功 / -1=失败（熵源不可用）。
@fnc crypto_random:: i4, out: u1&, len: u8
# 常量时间比较（防时序侧信道）：返回 0=相等 / -1=不等。
@fnc crypto_verify:: i4, a: &, b: &, len: u8

# ════════════════════════════════════════════════════════════════
# 大类二 · 代理算法（大数运算重，转发 ssl 后端；未编译后端时安全失败）
#   crypto 自身不实现这些算法（保持零外链）；编译进 ssl 后端
#   （OpenSSL libcrypto / mbedTLS）后由其提供强符号覆盖默认弱桩。
#   未配置后端时：crypto_rsa_backend()==0，下列调用一律安全失败（返回 nil/<0）。
#   hash_id 取值：1=SHA-1 4=SHA-256 5=SHA-384 6=SHA-512（见 crypto.h SC_HASH_*）。
# ════════════════════════════════════════════════════════════════

# ─────────────────────────── 簇 R：RSA ───────────────────────────

# 后端探测：0=无 / 1=mbedtls / 2=openssl。
@fnc crypto_rsa_backend:: i4
# 生成密钥对：bits=2048/3072/4096，pub_e 常用 65537。返回不透明 rsa_key*（失败 nil）。
@fnc crypto_rsa_keygen:: &, bits: u4, pub_e: u4
# 释放密钥句柄。
@fnc crypto_rsa_free:: k: &
# 导入：fmt 0=DER 1=PEM；is_private!=0 私钥否则公钥。返回 rsa_key*（失败 nil）。
@fnc crypto_rsa_import:: &, buf: &, len: u8, is_private: i4, fmt: i4
# 导出到 out[0..cap)：fmt/is_private 同上。返回写入字节数，<0 失败。
@fnc crypto_rsa_export:: i4, k: &, is_private: i4, fmt: i4, out: u1&, cap: u8
# PKCS#1 v1.5 签名/验签：digest 为已算好的摘要；sign 返回签名字节数(<0 失败)，
#   verify 返回 0=有效 / -1=无效。
@fnc crypto_rsa_sign_pkcs1:: i4, k: &, hash_id: i4, digest: &, dlen: u8, sig: u1&, sigcap: u8
@fnc crypto_rsa_verify_pkcs1:: i4, k: &, hash_id: i4, digest: &, dlen: u8, sig: &, siglen: u8
# PSS 签名/验签（盐长 = 摘要长）。
@fnc crypto_rsa_sign_pss:: i4, k: &, hash_id: i4, digest: &, dlen: u8, sig: u1&, sigcap: u8
@fnc crypto_rsa_verify_pss:: i4, k: &, hash_id: i4, digest: &, dlen: u8, sig: &, siglen: u8
# OAEP 加密/解密（hash_id 用于 MGF1 与标签摘要）。返回输出字节数，<0 失败。
@fnc crypto_rsa_encrypt_oaep:: i4, k: &, hash_id: i4, plain: &, plen: u8, out: u1&, cap: u8
@fnc crypto_rsa_decrypt_oaep:: i4, k: &, hash_id: i4, cipher: &, clen: u8, out: u1&, cap: u8
# PKCS#1 v1.5 加密/解密（⚠ 遗留，存在 Bleichenbacher 风险；新设计用 OAEP）。
@fnc crypto_rsa_encrypt_pkcs1:: i4, k: &, plain: &, plen: u8, out: u1&, cap: u8
@fnc crypto_rsa_decrypt_pkcs1:: i4, k: &, cipher: &, clen: u8, out: u1&, cap: u8
