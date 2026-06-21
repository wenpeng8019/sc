/* env.h —— sc 运行环境 / 系统路径模块的 C ABI 契约（与 builtins/env/env.sc 同步维护）
 *
 * 所有函数把结果路径写入调用方提供的 buf（NUL 结尾），size 为字节容量。
 * 返回码：0 成功 / ENV_ERR 系统失败 / ENV_ERR_CAPACITY buf 太小。
 * 跨平台实现见 env_impl.c，平台适配统一经由 builtins/platform.h。
 */
#ifndef SC_ENV_H
#define SC_ENV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回码（对应 sc 侧 ret：0 成功，非 0 失败） */
#define ENV_OK            0
#define ENV_ERR          (-1)   /* 系统调用失败 */
#define ENV_ERR_CAPACITY (-2)   /* buf 容量不足 */

/* 当前工作目录（cwd） */
int32_t env_work_dir(char *buf, uint32_t size);

/* 当前用户 home 目录 */
int32_t env_home_dir(char *buf, uint32_t size);

/* 用户下载目录 */
int32_t env_download_dir(char *buf, uint32_t size);

/* 当前可执行文件的规范化绝对路径 */
int32_t env_exe_file(char *buf, uint32_t size);

/* 在系统临时目录创建唯一空临时文件，返回其路径（调用方负责删除） */
int32_t env_tmp_file(char *buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* SC_ENV_H */
