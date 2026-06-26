# sys —— sc 运行环境 / 系统路径内置模块
#
# 本文件是 sys 接口的唯一事实源：
#   @fnc name:: 声明 C 侧实现的自由函数（无函数体）：转 C 生成 extern 原型，
#   实现在 sys_impl.c（链接期注入）。
# C ABI 契约见同目录 sys.h，默认实现见 sys_impl.c；跨平台经由 builtins/platform.h。
# 用法：inc sys.sc

# ---------------- 命令行参数解析（ARGS_*，C 实现接口）----------------
#
# ARGS 机制已原生定义在本文件：参数类型、参数定义结构、以及 ARGS_* 宏族。
# 典型用法：下游模块 inc sys.sc 后，在顶层写
#   mix ARGS_B(abc, ...)
# 定义参数（宏内部定义全局 ARGS_abc / ARGS_DEF_abc）；编译器会对
# 顶层 mix ARGS_* 做自动登记，ARGS_DEF_abc 由「声明即构造」自注册进
# arg_defs，因此可直接在 sc 里访问，并以 ARGS_parse(argc, argv)
# 完成解析（无需再手工传 &ARGS_DEF_abc 变参列表）。
#   说明：sc 路径下 arg_defs 已由构造自注册（非空），ARGS_parse 直接遍历它、
#   完全不读变参，故连结尾的 nil 也可省略。仅当一个参数都没定义（arg_defs 为空、
#   走纯 C 变参回退）时才需显式传 nil 作终止符；定义了任意 ARGS_* 即无此顾虑。
# 详见本文件与 sys.h 的示例说明。
#
# 示例（sc）：
#
#   inc sys.sc
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
#       var pos_count: i4 = ARGS_parse(argc, argv)
#
#       if ARGS_verbose                # bool 全局，直读无需选字段
#           printf("Verbose mode\n")
#       printf("Input: %s\n", ARGS_input)
#       printf("Count: %lld\n", ARGS_count)
#       return pos_count

# 参数类型枚举（与 C ABI 对齐）
@def arg_type: [
    ARG_FLOAT = -3
    ARG_INT
    ARG_BOOL
    ARG_STR
    ARG_DIR
    ARG_LS
    ARG_PRE
] : i4

# 参数值以「自然具体类型」独立存为全局 ARGS_<name>（选项即 app 属性，带编译期默认值）；
# 描述符 arg_def_st 的 slot 是指向该全局的 void*（裸 &），解析器按 type 以正确宽度写入。
# 已去掉旧的 arg_var 联合体——读取端直接按具体类型访问 ARGS_<name>，取错类型即编译错。
@def arg_def_st: {
    name: const char&
    desc: const char&
    type: arg_type
    s: char
    l: const char&
    req: bool
    slot: &
    next: arg_def_st&

    fnc init::    # 构造：把自身挂入全局注册链表 arg_defs（C 实现于 sys_impl.c）
}

# 已声明参数的全局注册链表头：每个 arg_def_st 构造（init）时把自己挂入此链。
#   sc 侧顶层 mix ARGS_* 展开为真实全局 ARGS_DEF_xxx 后，编译器「声明即构造」自动调用
#   arg_def_st.init 完成登记；ARGS_parse 优先采用本链（非 nil 时忽略 ... 变参）。
#   C 侧定义于 sys_impl.c。
let arg_defs:: arg_def_st&

# 参数定义宏：各自声明「具体类型」的全局 ARGS_<name>（值即 app 属性）与描述符 ARGS_DEF_<name>，
# 其 slot 取 &ARGS_<name>（隐式具体指针 → void*）。已去 union，故各宏独立、不再共用 ARGS_DEF。
def ARGS_B: req, name, s_cmd, l_cmd, desc
    @var ARGS_\name: bool = false
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_BOOL, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

def ARGS_I: req, name, s_cmd, l_cmd, desc
    @var ARGS_\name: i8 = 0
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_INT, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

def ARGS_F: req, name, s_cmd, l_cmd, desc
    @var ARGS_\name: f8 = 0
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_FLOAT, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

def ARGS_S: req, name, s_cmd, l_cmd, desc
    @var ARGS_\name: const char& = nil
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_STR, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

def ARGS_D: req, name, s_cmd, l_cmd, desc
    @var ARGS_\name: const char& = nil
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_DIR, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

def ARGS_L: req, name, s_cmd, l_cmd, desc
    @var ARGS_\name: const char&& = nil
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_LS, s_cmd, l_cmd, req,
        &ARGS_\name, nil
    }

# 带默认值变体：dft 即属性初值。
def ARGS_Bv: dft, name, s_cmd, l_cmd, desc
    @var ARGS_\name: bool = dft
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_BOOL, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

def ARGS_Iv: dft, name, s_cmd, l_cmd, desc
    @var ARGS_\name: i8 = dft
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_INT, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

def ARGS_Fv: dft, name, s_cmd, l_cmd, desc
    @var ARGS_\name: f8 = dft
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_FLOAT, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

def ARGS_Sv: dft, name, s_cmd, l_cmd, desc
    @var ARGS_\name: const char& = dft
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_STR, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

def ARGS_Dv: dft, name, s_cmd, l_cmd, desc
    @var ARGS_\name: const char& = dft
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_DIR, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

def ARGS_Lv: dft, name, s_cmd, l_cmd, desc
    @var ARGS_\name: const char&& = dft
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_LS, s_cmd, l_cmd, false,
        &ARGS_\name, nil
    }

# 预处理回调参数：无值全局，回调地址（cb_pre）直接存入 slot（void*）。
def ARGS_PRE: cb_pre, name, s_cmd, l_cmd, desc
    var ARGS_DEF_\name: arg_def_st = {
        `name`, desc, ARG_PRE, s_cmd, l_cmd, false,
        (cb_pre: &), nil
    }

# 设置命令行帮助信息。
#   pos_desc  位置参数描述，显示在 Usage 行（如 "<subcommand>" / "<file>..."），nil 时自动生成。
#   usage_ex  额外帮助说明，显示在选项列表之后，支持 $0 占位符（替换为程序名）；nil/空不显示。
@fnc ARGS_usage:: pos_desc: const char&, usage_ex: const char&

# 解析命令行参数。argc/argv 来自 main。sc 路径下参数定义已由「声明即构造」自注册进
# arg_defs，直接 ARGS_parse(argc, argv) 即可（连结尾 nil 也可省）；纯 C 调用方走变参回退，
# 须按 &ARGS_DEF_xxx 逐个传入并以 nil 结尾。返回位置参数数量（可经 argv[argc - 返回值]
# 访问）。遇 -h/--help 或必选缺失时自动打印帮助并退出。
@fnc ARGS_parse:: i4, argc: i4, argv: char&&, ...

# 打印命令行帮助信息。arg0 通常传 argv[0]。始终返回 0（通常由 ARGS_parse 自动调用）。
@fnc ARGS_print:: i4, arg0: const char&

# 取列表类型参数（ARGS_L 定义）的元素数量；未设置返回 0。
@fnc ARGS_ls_count:: i4, ls: const char&&


# ---------------- 系统路径查询（C 实现接口）----------------
# 返回码约定（ret，即 i4 语义别名）：
#   0  成功
#  -1  系统调用失败（SYS_ERR）
#  -2  buffer 容量不足（SYS_ERR_CAPACITY）
#
# 所有函数把结果路径写入调用方提供的 buffer（NUL 结尾），size 为其字节容量。
# 建议 buffer 至少 PATH_MAX（4096）字节；download_dir/exe_file 在部分平台
# 要求 buffer >= PATH_MAX，过小将返回 -2。

# 当前工作目录（cwd）。
@fnc sys_work_dir:: ret, buf: char&, size: u4

# 当前用户 home 目录。POSIX 优先 $HOME，回退 getpwuid_r；Windows 取用户配置目录。
@fnc sys_home_dir:: ret, buf: char&, size: u4

# 用户下载目录。Windows: Downloads 已知文件夹；macOS: sysdir；
# Linux: $XDG_DOWNLOAD_DIR，回退 ~/Downloads。
@fnc sys_download_dir:: ret, buf: char&, size: u4

# 当前可执行文件的绝对路径（已规范化）。
@fnc sys_exe_file:: ret, buf: char&, size: u4

# 在系统临时目录创建一个唯一的空临时文件，返回其路径。
# 注意：会真实创建文件（避免命名竞争），调用方用完应自行删除。
@fnc sys_tmp_file:: ret, buf: char&, size: u4
