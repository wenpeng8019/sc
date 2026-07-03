/* os.h —— sc 操作系统基本操作的 C ABI 契约（与 builtins/os/os.sc 同步维护） */
#ifndef SC_OS_H
#define SC_OS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CPU 逻辑核数（至少返回 1） */
uint32_t sc_ncpu(void);

/* ---------------- fs_*：文件/目录跨平台操作 ----------------
 * 跨平台实现见 os_impl.c，平台适配统一经由 builtins/platform.h。
 * path/from/to 为 NUL 结尾 C 字符串。返回约定：
 *   · 谓词      → bool
 *   · size      → int64_t（字节数；不存在/出错 -1）
 *   · 变更操作  → int32_t（0=成功 / -1=失败）
 */
bool     sc_fs_exists(const char *path);      /* 文件或目录是否存在 */
bool     sc_fs_is_dir(const char *path);      /* 是否为目录 */
bool     sc_fs_is_file(const char *path);     /* 是否为普通文件 */
int64_t  sc_fs_size(const char *path);        /* 文件字节数（-1=出错） */
int32_t  sc_fs_mkdir(const char *path);       /* 创建单级目录 */
int32_t  sc_fs_mkdirs(const char *path);      /* 递归创建目录（mkdir -p） */
int32_t  sc_fs_rmdir(const char *path);       /* 删除空目录 */
int32_t  sc_fs_remove(const char *path);      /* 删除文件 */
int32_t  sc_fs_rename(const char *from, const char *to); /* 重命名/移动 */

/* 目录遍历：不透明句柄（内部堆分配），用后必须 sc_fs_dir_close 释放。
 * 返回的名字/路径指向句柄内部缓冲，有效期至下次 next / close，只读勿改。 */
void    *sc_fs_dir_open(const char *path);    /* 打开目录，失败返回 NULL */
char    *sc_fs_dir_next(void *h);             /* 下一项名字；遍历结束返回 NULL */
bool     sc_fs_dir_is_dir(void *h);           /* 当前项是否为目录 */
int64_t  sc_fs_dir_size(void *h);             /* 当前项字节数（-1=未知） */
char    *sc_fs_dir_path(void *h);             /* 当前项完整路径 */
void     sc_fs_dir_close(void *h);            /* 关闭句柄 */

/* （待实现：网卡/防火墙/路由等系统管理查询；env_* / proc_* 等基本操作。
 *   应用网络套接字已迁至 sys 模块（sock_*）。） */

#ifdef __cplusplus
}
#endif

#endif /* SC_OS_H */
