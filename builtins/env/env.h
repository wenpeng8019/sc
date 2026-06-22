/* env.h —— sc 运行环境 / 系统路径模块的 C ABI 契约（与 builtins/env/env.sc 同步维护）
 *
 * 所有函数把结果路径写入调用方提供的 buf（NUL 结尾），size 为字节容量。
 * 返回码：0 成功 / ENV_ERR 系统失败 / ENV_ERR_CAPACITY buf 太小。
 * 跨平台实现见 env_impl.c，平台适配统一经由 builtins/platform.h。
 */
#ifndef SC_ENV_H
#define SC_ENV_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
/**
 * args 使用示例:
 * -------------------------------------------------------------------------
 *
 * // 定义参数（全局作用域）
 * ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output");
 * ARGS_S(true,  input,   'i', "input",   "Input file path");
 * ARGS_I(false, count,   'n', "count",   "Number of iterations");
 *
 * int main(int argc, char** argv) {
 *
 *     // 设置帮助信息（可选），usage_ex 中的 $0 会被替换为程序名
 *     ARGS_usage("<file>...",
 *         "Examples:\n"
 *         "  $0 -i input.txt -n 10 file1 file2\n"
 *         "  $0 --verbose -i data.bin");
 *
 *     // 解析参数
 *     int pos_count = ARGS_parse(argc, argv,
 *         &ARGS_DEF_verbose,
 *         &ARGS_DEF_input,
 *         &ARGS_DEF_count,
 *         NULL);
 *
 *     // 使用参数
 *     if (ARGS_verbose.i64) printf("Verbose mode\n");
 *     printf("Input: %s\n", ARGS_input.str);
 *     printf("Count: %lld\n", ARGS_count.i64);
 *
 *     // 访问位置参数
 *     for (int i = 0; i < pos_count; i++) {
 *         printf("Positional[%d]: %s\n", i, argv[argc - pos_count + i]);
 *     }
 *     return 0;
 * }
 * -------------------------------------------------------------------------
 */

 #include "args.h"

/**
 * 设置命令行帮助信息
 * @param pos_desc  位置参数描述，显示在 Usage 行中，如 "<subcommand>" 或 "<file>..."
 *                  传 NULL 时自动根据解析结果生成（有位置参数则显示 "ARGS..."）
 * @param usage_ex  额外的帮助说明，显示在选项列表之后，如子命令说明或使用示例
 *                  支持 $0 占位符，输出时会被替换为程序名（argv[0]）
 *                  传 NULL 或空字符串时不显示
 */
void ARGS_usage(const char* pos_desc, const char* usage_ex);

/**
 * 解析命令行参数
 * @param argc      main() 的 argc
 * @param argv      main() 的 argv，解析后会重排：选项在前，位置参数在后
 * @param ...       参数定义列表，以 NULL 结尾，每项为 &ARGS_DEF_xxx
 * @return          位置参数数量（可通过 argv[argc - 返回值] 访问位置参数）
 *
 * @note 遇到 -h/--help 或无参数时自动打印帮助并退出
 * @note 必选参数缺失时打印错误并退出
 */
int ARGS_parse(int argc, char** argv, .../* end with NULL */);

/**
 * 打印命令行帮助信息
 * @param arg0      程序名（通常传 argv[0]）
 * @return          始终返回 0
 *
 * @note 通常由 ARGS_parse 自动调用，也可手动调用
 */
int ARGS_print(const char* arg0);

/**
 * 获取列表类型参数的元素数量
 * @param var       ARGS_L 定义的参数变量指针
 * @return          列表元素数量，若未设置返回 0
 */
int ARGS_ls_count(arg_var_st* var);

///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* SC_ENV_H */
