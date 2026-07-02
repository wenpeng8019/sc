# os —— sc 操作系统基本操作内置模块
# 唯一事实源：C ABI 契约见同目录 os.h，默认实现见 os_impl.c
# 跨平台经由 builtins/platform.h
#
# 用法：inc os.sc

# ---------------- os_rand：密码学强随机字节 ----------------
# 填充 n 字节随机到 buf，返回 0。转发 platform.h 的 P_rand_bytes（CSPRNG）：
# macOS/BSD arc4random、Windows rand_s、Linux /dev/urandom。
# 用于 WS 客户端掩码键等需「不可预测」随机处（RFC 6455 §5.3）。
@fnc os_rand:: i4, buf: &, n: u4

# （待实现：fs_*（文件/目录/路径）/ env_*（环境变量）/ proc_*（进程）等基本操作）
