# os —— sc 操作系统基本操作内置模块
# 唯一事实源：C ABI 契约见同目录 os.h，默认实现见 os_impl.c
# 跨平台经由 builtins/platform.h
#
# 用法：inc os.sc

# CPU 逻辑核数（至少返回 1）
@fnc ncpu:: u4

# （待实现：fs_*（文件/目录/路径）/ env_*（环境变量）/ proc_*（进程）等基本操作）
