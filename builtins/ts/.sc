# ts 模块构建/链接配置（格式详见 builtins/gpu/.sc）
# ts 的 C 实现（ts_impl.c 等）用到 <math.h>（exp/log/pow/sqrt 等）：
#   macOS 随 libSystem 自动链接；Linux/Android glibc 须显式 -lm
#   （glibc 2.29+ 的 exp 等仍在 libm，缺 -lm → undefined reference exp@@GLIBC_2.29）。
#   Windows CRT 内含数学函数，无需单独库。

[linux]
ldflags = -lm

[*android*]
ldflags = -lm
