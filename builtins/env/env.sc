# env —— sc 运行环境 / 系统路径内置模块
#
# 本文件是 env 接口的唯一事实源：
#   @fnc name:: 声明 C 侧实现的自由函数（无函数体）：转 C 生成 extern 原型，
#   实现在 env_impl.c（链接期注入）。
# C ABI 契约见同目录 env.h，默认实现见 env_impl.c；跨平台经由 builtins/platform.h。
# 用法：inc env.sc

# ---------------- 命令行参数解析（ARGS_*，C 实现接口）----------------
#
# ARGS 机制已原生定义在本文件：参数类型、参数定义结构、以及 ARGS_* 宏族。
# 典型用法：下游模块 inc env.sc 后，在顶层写
#   mix ARGS_B(abc, ...)
# 定义参数（宏内部定义全局 ARGS_abc / ARGS_DEF_abc）；编译器会对
# 顶层 mix ARGS_* 做自动登记，因此可直接在 sc 里访问并把
# &ARGS_DEF_abc 传给 ARGS_parse(...)
# 完成解析。
# 详见本文件与 env.h 的示例说明。
#
# 示例（sc）：
#
#   inc env.sc
#   inc stdio.h
#
#   mix ARGS_B(false, verbose, 'v', "verbose", "Enable verbose output")
#   mix ARGS_S(true,  input,   'i', "input",   "Input file path")
#   mix ARGS_I(false, count,   'n', "count",   "Number of iterations")
#
#   fnc main: i4, argc: i4, argv: char&&
#       ARGS_usage("<file>...",
#           "Examples:\n"
#           "  $0 -i input.txt -n 10 file1 file2\n"
#           "  $0 --verbose -i data.bin")
#
#       var pos_count: i4 = ARGS_parse(argc, argv,
#           &ARGS_DEF_verbose,
#           &ARGS_DEF_input,
#           &ARGS_DEF_count,
#           nil)
#
#       if ARGS_verbose.i64
#           printf("Verbose mode\n")
#       printf("Input: %s\n", ARGS_input.str)
#       printf("Count: %lld\n", ARGS_count.i64)
#       return pos_count

# 参数类型枚举（与 C ABI 对齐）
def arg_type: [
    ARG_FLOAT = -3
    ARG_INT
    ARG_BOOL
    ARG_STR
    ARG_DIR
    ARG_LS
    ARG_PRE
] : i4

# 参数值联合体
def arg_var_st: (
    str: const char&
    i64: i8
    f64: f8
    ls: const char&&
)

# 参数定义结构
def arg_def_st: {
    name: const char&
    desc: const char&
    type: arg_type
    s: char
    l: const char&
    req: bool
    slot: arg_var_st&
    next: arg_def_st&
}

# 参数定义宏：生成 ARGS_<name> 与 ARGS_DEF_<name>
def ARGS_DEF: req, name, type, s_cmd, l_cmd, desc
    @var ARGS_\name: arg_var_st
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_\type, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

def ARGS_B: req, name, s_cmd, l_cmd, desc
    mix ARGS_DEF(req, name, BOOL, s_cmd, l_cmd, desc)

def ARGS_I: req, name, s_cmd, l_cmd, desc
    mix ARGS_DEF(req, name, INT, s_cmd, l_cmd, desc)

def ARGS_F: req, name, s_cmd, l_cmd, desc
    mix ARGS_DEF(req, name, FLOAT, s_cmd, l_cmd, desc)

def ARGS_S: req, name, s_cmd, l_cmd, desc
    mix ARGS_DEF(req, name, STR, s_cmd, l_cmd, desc)

def ARGS_D: req, name, s_cmd, l_cmd, desc
    mix ARGS_DEF(req, name, DIR, s_cmd, l_cmd, desc)

def ARGS_L: req, name, s_cmd, l_cmd, desc
    mix ARGS_DEF(req, name, LS, s_cmd, l_cmd, desc)

# 带默认值的参数定义宏
def ARGS_DFT: init, name, type, s_cmd, l_cmd, desc
    var ARGS_\name: arg_var_st = init
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_\type, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

def ARGS_Bv: dft, name, s_cmd, l_cmd, desc
    mix ARGS_DFT({ i64 = dft }, name, BOOL, s_cmd, l_cmd, desc)

def ARGS_Iv: dft, name, s_cmd, l_cmd, desc
    mix ARGS_DFT({ i64 = dft }, name, INT, s_cmd, l_cmd, desc)

def ARGS_Fv: dft, name, s_cmd, l_cmd, desc
    mix ARGS_DFT({ f64 = dft }, name, FLOAT, s_cmd, l_cmd, desc)

def ARGS_Sv: dft, name, s_cmd, l_cmd, desc
    mix ARGS_DFT({ str = dft }, name, STR, s_cmd, l_cmd, desc)

def ARGS_Dv: dft, name, s_cmd, l_cmd, desc
    mix ARGS_DFT({ str = dft }, name, DIR, s_cmd, l_cmd, desc)

def ARGS_Lv: dft, name, s_cmd, l_cmd, desc
    mix ARGS_DFT({ ls = dft }, name, LS, s_cmd, l_cmd, desc)

# 预处理回调参数（cb_pre 视为 C 回调地址）
def ARGS_PRE: cb_pre, name, s_cmd, l_cmd, desc
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_PRE, s_cmd, l_cmd, false,
        cb_pre: arg_var_st&, nil
    }

# 认领助手：让宏生成的 ARGS_<name> 可被 sc 类型检查器识别
# 在新语义下，顶层 mix ARGS_* 已自动登记；此助手仅保留兼容旧写法。
def ARGS: name
    let ARGS_\name:: arg_var_st

# 设置命令行帮助信息。
#   pos_desc  位置参数描述，显示在 Usage 行（如 "<subcommand>" / "<file>..."），nil 时自动生成。
#   usage_ex  额外帮助说明，显示在选项列表之后，支持 $0 占位符（替换为程序名）；nil/空不显示。
@fnc ARGS_usage:: pos_desc: const char&, usage_ex: const char&

# 解析命令行参数。argc/argv 来自 main；变参为参数定义列表（以 nil 结尾，每项 &ARGS_DEF_xxx）。
# 返回位置参数数量（可经 argv[argc - 返回值] 访问）。遇 -h/--help 或必选缺失时自动打印帮助并退出。
@fnc ARGS_parse:: i4, argc: i4, argv: char&&, ...

# 打印命令行帮助信息。arg0 通常传 argv[0]。始终返回 0（通常由 ARGS_parse 自动调用）。
@fnc ARGS_print:: i4, arg0: const char&

# 取列表类型参数（ARGS_L 定义）的元素数量；未设置返回 0。
@fnc ARGS_ls_count:: i4, v: arg_var_st&


# ---------------- 系统路径查询（C 实现接口）----------------
# 返回码约定（ret，即 i4 语义别名）：
#   0  成功
#  -1  系统调用失败（ENV_ERR）
#  -2  buffer 容量不足（ENV_ERR_CAPACITY）
#
# 所有函数把结果路径写入调用方提供的 buffer（NUL 结尾），size 为其字节容量。
# 建议 buffer 至少 PATH_MAX（4096）字节；download_dir/exe_file 在部分平台
# 要求 buffer >= PATH_MAX，过小将返回 -2。

# 当前工作目录（cwd）。
@fnc env_work_dir:: ret, buf: char&, size: u4

# 当前用户 home 目录。POSIX 优先 $HOME，回退 getpwuid_r；Windows 取用户配置目录。
@fnc env_home_dir:: ret, buf: char&, size: u4

# 用户下载目录。Windows: Downloads 已知文件夹；macOS: sysdir；
# Linux: $XDG_DOWNLOAD_DIR，回退 ~/Downloads。
@fnc env_download_dir:: ret, buf: char&, size: u4

# 当前可执行文件的绝对路径（已规范化）。
@fnc env_exe_file:: ret, buf: char&, size: u4

# 在系统临时目录创建一个唯一的空临时文件，返回其路径。
# 注意：会真实创建文件（避免命名竞争），调用方用完应自行删除。
@fnc env_tmp_file:: ret, buf: char&, size: u4
