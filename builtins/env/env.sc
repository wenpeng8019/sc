# env —— sc 运行环境 / 系统路径内置模块
#
# 本文件是 env 接口的唯一事实源：
#   @fnc name:: 声明 C 侧实现的自由函数（无函数体）：转 C 生成 extern 原型，
#   实现在 env_impl.c（链接期注入）。
# C ABI 契约见同目录 env.h，默认实现见 env_impl.c；跨平台经由 builtins/platform.h。
# 用法：inc env.sc

# ---------------- 命令行参数解析（ARGS_*，C 实现接口）----------------
#
# 配套的参数定义宏族 ARGS_*（ARGS_B/I/S/L/... 等）见 args.h。
# 典型用法：下游模块 inc env.sc（连带导入 args.h），用 ::ARGS_B(abc, ...) 定义
# 一个参数（宏内部定义全局 ARGS_abc），再以 let ARGS_abc:: arg_var_st 认领它，
# 然后调用 ::ARGS_parse(...) 解析。详见 args.h 顶部用法示例。

inc "args.h"

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
