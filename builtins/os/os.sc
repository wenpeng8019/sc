# os —— sc 操作系统基本操作内置模块
# 唯一事实源：C ABI 契约见同目录 os.h，默认实现见 os_impl.c
# 跨平台经由 builtins/platform.h
#
# 用法：inc os.sc

# CPU 逻辑核数（至少返回 1）
@fnc ncpu:: u4

# ---------------- fs_*：文件/目录跨平台操作（C 实现接口）----------------
# 底层跨平台经 builtins/platform.h（Windows ↔ POSIX）；路径为 NUL 结尾 C 字符串。
# 返回码约定：
#   · 谓词类（exists/is_dir/is_file）→ bool
#   · 大小类（size）               → i8（字节数；不存在/出错返回 -1）
#   · 变更类（mkdir/mkdirs/rmdir/remove/rename）→ ret（i4 语义别名：0=成功 / -1=失败）

# 路径是否存在（文件或目录皆可）。
@fnc fs_exists:: bool, path: const char&
# 路径是否为目录。
@fnc fs_is_dir:: bool, path: const char&
# 路径是否为普通文件。
@fnc fs_is_file:: bool, path: const char&
# 文件字节数；路径不存在或出错返回 -1。
@fnc fs_size:: i8, path: const char&

# 创建单级目录（父目录须已存在，已存在算失败）。0=成功 / -1=失败。
@fnc fs_mkdir:: ret, path: const char&
# 递归创建目录（等价 mkdir -p）：逐级补齐缺失父目录，末级已存在视为成功。0=成功 / -1=失败。
@fnc fs_mkdirs:: ret, path: const char&
# 删除空目录。0=成功 / -1=失败。
@fnc fs_rmdir:: ret, path: const char&
# 删除文件。0=成功 / -1=失败。
@fnc fs_remove:: ret, path: const char&
# 重命名/移动文件或目录（限同卷）。0=成功 / -1=失败。
@fnc fs_rename:: ret, from: const char&, to: const char&

# ---------------- 目录遍历（不透明句柄，用后必须 fs_dir_close）----------------
# 遍历句柄内部持当前项与拼接缓冲；同一句柄非线程安全，逐项串行访问。
# 遍历含 "." 与 ".." 项（如需过滤由调用方自行判断）。

# 打开目录，返回遍历句柄（失败返回 nil）。
@fnc fs_dir_open:: &, path: const char&
# 取下一项文件名（相对名，非全路径）；只读、勿修改，有效期至下次 next/close；结束返回 nil。
@fnc fs_dir_next:: char&, h: &
# 当前项是否为目录。
@fnc fs_dir_is_dir:: bool, h: &
# 当前项字节数（-1=未知/出错）。
@fnc fs_dir_size:: i8, h: &
# 当前项完整路径（open 的目录 + 分隔符 + 项名）；只读，有效期至下次 next/close。
@fnc fs_dir_path:: char&, h: &
# 关闭遍历句柄，释放资源。
@fnc fs_dir_close:: h: &

# （待实现：env_*（环境变量）/ proc_*（进程）等基本操作）
