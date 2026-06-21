# env —— sc 运行环境 / 系统路径内置模块
#
# 本文件是 env 接口的唯一事实源：
#   @fnc name:: 声明 C 侧实现的自由函数（无函数体）：转 C 生成 extern 原型，
#   实现在 env_impl.c（链接期注入）。
# C ABI 契约见同目录 env.h，默认实现见 env_impl.c；跨平台经由 builtins/platform.h。
#
# 用法：inc env.sc
#
# 返回码约定（ret，即 i4 语义别名）：
#   0  成功
#  -1  系统调用失败（ENV_ERR）
#  -2  buffer 容量不足（ENV_ERR_CAPACITY）
#
# 所有函数把结果路径写入调用方提供的 buffer（NUL 结尾），size 为其字节容量。
# 建议 buffer 至少 PATH_MAX（4096）字节；download_dir/exe_file 在部分平台
# 要求 buffer >= PATH_MAX，过小将返回 -2。

# ---------------- 系统路径查询（C 实现接口）----------------

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
