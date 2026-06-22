#ifndef SC_ARGS_H
#define SC_ARGS_H

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

typedef enum arg_type {
    ARG_FLOAT = -3,
    ARG_INT,
    ARG_BOOL,
    ARG_STR,
    ARG_DIR,                    // 目录路径：自动规范化：展开~、移除末尾/、合并//、处理/./
    ARG_LS,                     // 字符串列表：逗号分隔的字符串数组。此时 ARGS_xxx.ls[i] 可获取列表项; ARGS_ls_count(&ARGS_xxx) 可获取列表数量
    ARG_PRE,                    // 预处理回调：在解析命令行参数过程中触发。常用于指定语言选项，可在执行 show_help 之前执行
                                // 回调接口定义：void prev_cb(char* argv)
} arg_type_e;

/**
 * 参数值联合体
 *
 * 各类型的默认值（选项未出现在命令行时）：
 *   ARG_BOOL   -> i64 = 0
 *   ARG_INT    -> i64 = 0
 *   ARG_FLOAT  -> f64 = 0.0
 *   ARG_STR    -> str = NULL
 *   ARG_DIR    -> str = NULL
 *   ARG_LS     -> ls  = NULL
 *
 * 各类型对空字符串值（如 --opt ""）的处理：
 *   ARG_INT    -> 0   (strtoll 返回 0)
 *   ARG_FLOAT  -> 0.0 (strtod 返回 0.0)
 *   ARG_STR    -> ""  (指向空字符串)
 *   ARG_DIR    -> ""  (指向空字符串)
 *
 * @note 选项后缺少值时（如 -n 后无参数）会报错退出
 * @note INT/FLOAT 标记为必选时，无法区分"未设置"和"设置为0"
 */
typedef union arg_var {
    const char*  str;           // ARG_STR, ARG_DIR
    int64_t      i64;           // ARG_BOOL, ARG_INT
    double       f64;           // ARG_FLOAT
    const char** ls;            // ARG_LS，用 ARGS_ls_count() 获取数量
} arg_var_st;

typedef struct arg_def {
    const char* name;
    const char* desc;
    arg_type_e  type;
    char        s;
    const char* l;
    bool        req;
    arg_var_st* var;
    struct arg_def* next;
} arg_def_st;

#define ARGS_DEF(req, name, type, s_cmd, l_cmd, desc)   \
extern arg_var_st ARGS_##name;                          \
arg_var_st ARGS_##name;                                 \
static arg_def_st ARGS_DEF_##name = {                   \
    #name, desc, ARG_##type, s_cmd, l_cmd, req,         \
    &ARGS_##name, NULL                                  \
}
#define ARGS_B(req, name, s_cmd, l_cmd, desc)   ARGS_DEF(req, name, BOOL, s_cmd, l_cmd, desc)
#define ARGS_I(req, name, s_cmd, l_cmd, desc)   ARGS_DEF(req, name, INT, s_cmd, l_cmd, desc)
#define ARGS_F(req, name, s_cmd, l_cmd, desc)   ARGS_DEF(req, name, FLOAT, s_cmd, l_cmd, desc)
#define ARGS_S(req, name, s_cmd, l_cmd, desc)   ARGS_DEF(req, name, STR, s_cmd, l_cmd, desc)
#define ARGS_D(req, name, s_cmd, l_cmd, desc)   ARGS_DEF(req, name, DIR, s_cmd, l_cmd, desc)
#define ARGS_L(req, name, s_cmd, l_cmd, desc)   ARGS_DEF(req, name, LS, s_cmd, l_cmd, desc)

#define ARGS_DFT(init, name, type, s_cmd, l_cmd, desc)  \
extern arg_var_st ARGS_##name;                          \
arg_var_st ARGS_##name = init;                          \
static arg_def_st ARGS_DEF_##name = {                   \
    #name, desc, ARG_##type, s_cmd, l_cmd, false,       \
    &ARGS_##name, NULL                                  \
}
#define ARGS_Bv(dft, name, s_cmd, l_cmd, desc)  ARGS_DFT({.i64=(dft)}, name, BOOL, s_cmd, l_cmd, desc)
#define ARGS_Iv(dft, name, s_cmd, l_cmd, desc)  ARGS_DFT({.i64=(dft)}, name, INT, s_cmd, l_cmd, desc)
#define ARGS_Fv(dft, name, s_cmd, l_cmd, desc)  ARGS_DFT({.f64=(dft)}, name, FLOAT, s_cmd, l_cmd, desc)
#define ARGS_Sv(dft, name, s_cmd, l_cmd, desc)  ARGS_DFT({.str=(dft)}, name, STR, s_cmd, l_cmd, desc)
#define ARGS_Dv(dft, name, s_cmd, l_cmd, desc)  ARGS_DFT({.str=(dft)}, name, DIR, s_cmd, l_cmd, desc)
#define ARGS_Lv(dft, name, s_cmd, l_cmd, desc)  ARGS_DFT({.ls=(dft)}, name, LS, s_cmd, l_cmd, desc)

#define ARGS_PRE(cb_pre, name, s_cmd, l_cmd, desc)      \
static arg_def_st ARGS_DEF_##name = {                   \
    #name, desc, ARG_PRE, s_cmd, l_cmd, false,          \
    (arg_var_st*)cb_pre, NULL                           \
}

#define ARGS(name) extern arg_var_st ARGS_##name

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* SC_ARGS_H */
