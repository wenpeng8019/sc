/* ssl.h —— ssl.sc 的 C ABI 契约（TLS 流加密：双后端 mbedTLS / OpenSSL，编译期可选）
 *
 * 角色：ssl.sc 是「TLS 记录层」内置模块的语法声明；本头是其 C 侧契约。实现见
 *       builtins/ssl/ssl_impl.c（inc ssl.sc 时拼接进生成单元，同 TU 编译）。
 *
 * 后端选择（构建 scc 时固化，参考 async 的 SCC_WITH_UV）：
 *   -DSCC_WITH_MBEDTLS  → mbedTLS（vendor 源码内置 / 静态库）
 *   -DSCC_WITH_OPENSSL  → OpenSSL（系统库头 + 链接 -lssl -lcrypto）
 *   两者皆未定义        → none 后端：所有调用安全失败（ssl_backend()==SSL_BACKEND_NONE）
 *
 * 设计：传输回调缝（ssl_set_transport）—— 密文搬运由调用方注入的 send/recv 完成，
 *       与具体传输（socket / sc com 设备）解耦；对应 mbedtls_ssl_set_bio / OpenSSL BIO。
 */
#ifndef SC_SSL_H
#define SC_SSL_H

#include <stdint.h>   /* 整型契约（int32_t / uint32_t）；后端头在 ssl_impl.c 内按 ifdef 引 */

#ifdef __cplusplus
extern "C" {
#endif

/* 后端标识（ssl_backend 返回值） */
#define SC_SSL_BACKEND_NONE    0
#define SC_SSL_BACKEND_MBEDTLS 1
#define SC_SSL_BACKEND_OPENSSL 2

/* 握手 / 读写返回码。
 * 注意：ssl_read/ssl_write 用 >0 表示明文字节数，故 SSL_WANT_IO 必须为负，
 * 否则会与「读到 1 字节」的合法返回值冲突（曾导致首字节读出垃圾）。 */
#define SC_SSL_OK       0    /* 完成 / 干净关闭（读返回 0） */
#define SC_SSL_WANT_IO (-2)  /* 需更多 io：握手未完 / 读写需在传输就绪后重试 */
#define SC_SSL_ERR    (-1)   /* 错误 */

/* 不透明连接句柄：实体定义在 ssl_impl.c（随后端而异）。
 * 注意——公开 ABI 一律用 void* 而非 sc_ssl_conn*，以与 scc 按 ssl.sc 生成的 @fnc
 * extern 原型（& → void*）逐一精确一致（同 crypto.h 用 void* 的约定）；impl 内部回转。 */
typedef struct sc_ssl_conn sc_ssl_conn;

/* 传输回调（impl 内部把 void* send/recv 回转为此类型）：密文搬运到 / 自底层。
 * 返回处理字节数；<0 错误；0=暂无数据 / EOF。ctx 透传。 */
typedef int (*sc_ssl_send_fn)(void *ctx, const void *buf, uint32_t len);
typedef int (*sc_ssl_recv_fn)(void *ctx, void *buf, uint32_t len);

/* ---------------- 后端探测 ---------------- */
int32_t  sc_ssl_backend(void);        /* SC_SSL_BACKEND_* */
char    *sc_ssl_backend_name(void);   /* "none" / "mbedtls" / "openssl" */

/* ---------------- 连接生命周期 ---------------- */
/* 新建 TLS 客户端连接（未握手）；host=SNI/校验域名；verify!=0 校验对端证书。失败返回 NULL。 */
void    *sc_ssl_client_new(char *host, int32_t verify);
/* 注入传输回调（send/recv 实为 sc_ssl_send_fn/sc_ssl_recv_fn，及透传 ctx）。 */
void     sc_ssl_set_transport(void *s, void *send, void *recv, void *ctx);
/* 释放连接及底层 TLS 上下文（NULL 安全）。 */
void     sc_ssl_free(void *s);

/* ---------------- 握手与明文收发 ---------------- */
/* 驱动握手：SC_SSL_OK 完成 / SC_SSL_WANT_IO 需再 io / SC_SSL_ERR 失败。 */
int32_t sc_ssl_handshake(void *s);
/* 明文读（自动 TLS 解密）：返回 >=0 字节 / SC_SSL_WANT_IO / SC_SSL_ERR。 */
int32_t sc_ssl_read (void *s, void *buf, uint32_t len);
/* 明文写（自动 TLS 加密）：返回 >=0 字节 / SC_SSL_WANT_IO / SC_SSL_ERR。 */
int32_t sc_ssl_write(void *s, void *buf, uint32_t len);

/* ---------------- com 设备适配层（后端无关，仅用公开 sc_ssl_* API） ----------------
 * 把 TLS 包成 com 设备：叠在底层传输 com（如 io.tcp 得到的同步 com）之上，ssl com 的
 * read/write 经 TLS 加解密、密文走底层 com。构造时同步驱动握手（要求底层为阻塞传输），
 * 握手失败返回 NULL。底层 com 不被 ssl com 拥有（close 默认不关底层，由调用方负责）。 */
struct sc_com;   /* op.h 定义；此处仅作返回/参数类型的不完全声明 */
struct sc_com *sc_ssl_com(struct sc_com *transport, char *host, int32_t verify);

#ifdef __cplusplus
}
#endif

#endif /* SC_SSL_H */
