/* sys.h —— sc 运行环境 / 系统路径模块的 C ABI 契约（与 builtins/sys/sys.sc 同步维护）
 *
 * 所有函数把结果路径写入调用方提供的 buf（NUL 结尾），size 为字节容量。
 * 返回码：0 成功 / SYS_ERR 系统失败 / SYS_ERR_CAPACITY buf 太小。
 * 跨平台实现见 sys_impl.c，平台适配统一经由 builtins/platform.h。
 */
#ifndef SC_SYS_H
#define SC_SYS_H

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
 * sc（推荐）:
 *
 *   inc sys.sc
 *   mix ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output")
 *
 *   fnc main: i4, argc: i4, argv: char&&
 *       ARGS_parse(argc, argv)        // 自注册路径：无需变参列表，连 nil 也可省
 *       return ARGS_verbose            // bool 全局，直读无需选字段
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
 *     // 使用参数（按具体类型直读，取错类型即编译错）
 *     if (ARGS_verbose) printf("Verbose mode\n");
 *     printf("Input: %s\n", ARGS_input);
 *     printf("Count: %lld\n", (long long)ARGS_count);
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
	ARG_DIR,
	ARG_LS,
	ARG_PRE,
} arg_type_e;

/* 每个选项的值以其「自然具体类型」独立存储为全局 ARGS_<name>（选项即 app 属性，
 * 带编译期默认值）；描述符 arg_def 的 slot 是指向该全局的 void*，解析器按 type 以
 * 正确宽度写入。已去掉旧的 arg_var 联合体——读取端直接按具体类型访问 ARGS_<name>
 * （bool / int64_t / double / const char* / const char**），取错类型即编译错，
 * 不再有 .i64/.str 选字段的隐患。各 ARGS_<X> 宏因此各自声明具体类型的全局。 */
typedef struct arg_def {
	const char* name;
	const char* desc;
	arg_type_e  type;
	char        s;
	const char* l;
	bool        req;
	void*       slot;          /* 指向具体类型全局 ARGS_<name>（PRE：存回调函数地址） */
	struct arg_def* next;
} arg_def_st;

/* 已声明参数的全局注册链表头：arg_def_st 构造（arg_def_st_init）时把自身挂入此链。
 * sc 侧顶层 mix ARGS_* 展开为真实全局后，编译器「声明即构造」自动调用 arg_def_st_init
 * 完成登记；ARGS_parse 优先采用本链（非 NULL 时忽略 ... 变参）。定义见 sys_impl.c。 */
extern arg_def_st* arg_defs;

/* arg_def_st 构造函数：把 _this 挂入全局注册链表 arg_defs（头插）。
 * 由 sc 编译器对静态全局 arg_def_st「声明即构造」自动调用；纯 C 调用方无此自动机制，
 * 仍可经 ARGS_parse 的 ... 变参传入 &ARGS_DEF_xxx（回退路径）。 */
void arg_def_st_init(arg_def_st* _this);

/* 选项定义宏：各自声明「具体类型」的全局 ARGS_<name>（值即 app 属性），并构造描述符
 * ARGS_DEF_<name>，其 slot 为指向该全局的 void*。已去 union——故各宏独立、不再共用一个
 * 通用 ARGS_DEF。无默认值的整型/浮点/布尔取静态零初值；指针型取 NULL。 */
#define ARGS_B(req, name, s_cmd, l_cmd, desc)                                  \
extern bool ARGS_##name; bool ARGS_##name = false;                            \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_BOOL, s_cmd, l_cmd, req, &ARGS_##name, NULL              \
}
#define ARGS_I(req, name, s_cmd, l_cmd, desc)                                  \
extern int64_t ARGS_##name; int64_t ARGS_##name = 0;                          \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_INT, s_cmd, l_cmd, req, &ARGS_##name, NULL               \
}
#define ARGS_F(req, name, s_cmd, l_cmd, desc)                                  \
extern double ARGS_##name; double ARGS_##name = 0;                            \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_FLOAT, s_cmd, l_cmd, req, &ARGS_##name, NULL             \
}
#define ARGS_S(req, name, s_cmd, l_cmd, desc)                                  \
extern const char* ARGS_##name; const char* ARGS_##name = NULL;               \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_STR, s_cmd, l_cmd, req, &ARGS_##name, NULL               \
}
#define ARGS_D(req, name, s_cmd, l_cmd, desc)                                  \
extern const char* ARGS_##name; const char* ARGS_##name = NULL;               \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_DIR, s_cmd, l_cmd, req, &ARGS_##name, NULL               \
}
#define ARGS_L(req, name, s_cmd, l_cmd, desc)                                  \
extern const char** ARGS_##name; const char** ARGS_##name = NULL;             \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_LS, s_cmd, l_cmd, req, &ARGS_##name, NULL                \
}

/* 带默认值变体：default 即属性初值。 */
#define ARGS_Bv(dft, name, s_cmd, l_cmd, desc)                                 \
extern bool ARGS_##name; bool ARGS_##name = (dft);                            \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_BOOL, s_cmd, l_cmd, false, &ARGS_##name, NULL            \
}
#define ARGS_Iv(dft, name, s_cmd, l_cmd, desc)                                 \
extern int64_t ARGS_##name; int64_t ARGS_##name = (dft);                      \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_INT, s_cmd, l_cmd, false, &ARGS_##name, NULL             \
}
#define ARGS_Fv(dft, name, s_cmd, l_cmd, desc)                                 \
extern double ARGS_##name; double ARGS_##name = (dft);                        \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_FLOAT, s_cmd, l_cmd, false, &ARGS_##name, NULL           \
}
#define ARGS_Sv(dft, name, s_cmd, l_cmd, desc)                                 \
extern const char* ARGS_##name; const char* ARGS_##name = (dft);              \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_STR, s_cmd, l_cmd, false, &ARGS_##name, NULL             \
}
#define ARGS_Dv(dft, name, s_cmd, l_cmd, desc)                                 \
extern const char* ARGS_##name; const char* ARGS_##name = (dft);              \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_DIR, s_cmd, l_cmd, false, &ARGS_##name, NULL             \
}
#define ARGS_Lv(dft, name, s_cmd, l_cmd, desc)                                 \
extern const char** ARGS_##name; const char** ARGS_##name = (dft);            \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_LS, s_cmd, l_cmd, false, &ARGS_##name, NULL              \
}

/* 预处理回调：无值全局，回调地址直接存入 slot（void*）。 */
#define ARGS_PRE(cb_pre, name, s_cmd, l_cmd, desc)                             \
static arg_def_st ARGS_DEF_##name = {                                         \
	#name, desc, ARG_PRE, s_cmd, l_cmd, false, (void*)cb_pre, NULL            \
}

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
 *                  仅当全局注册链表 arg_defs 为 NULL（纯 C 调用方）时才解析本变参；
 *                  sc 侧 mix ARGS_* 已由构造自动登记，arg_defs 非 NULL，变参被忽略，
 *                  可直接 ARGS_parse(argc, argv, NULL)
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
int ARGS_ls_count(const char** ls);

///////////////////////////////////////////////////////////////////////////////

/* 返回码（对应 sc 侧 ret：0 成功，非 0 失败） */
#define SYS_OK            0
#define SYS_ERR          (-1)   /* 系统调用失败 */
#define SYS_ERR_CAPACITY (-2)   /* buf 容量不足 */

/* 当前工作目录（cwd） */
int32_t sys_work_dir(char *buf, uint32_t size);

/* 当前用户 home 目录 */
int32_t sys_home_dir(char *buf, uint32_t size);

/* 用户下载目录 */
int32_t sys_download_dir(char *buf, uint32_t size);

/* 当前可执行文件的规范化绝对路径 */
int32_t sys_exe_file(char *buf, uint32_t size);

/* 在系统临时目录创建唯一空临时文件，返回其路径（调用方负责删除） */
int32_t sys_tmp_file(char *buf, uint32_t size);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* SC_SYS_H */
