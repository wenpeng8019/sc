# ssh 模块消费侧链接配置（格式说明见 builtins/gpu/.sc）
#
# 特殊：ssh 的原生库 libssh2.a 由本目录 build.sh 独立构建（源在 vendor/，
#   scc . --build 不适用），故此 .sc 不描述模块库构建，仅供「消费侧」——
#   inc ssh.sc 的程序最终链接时，scc 按目标段追加平台系统库
#   （见 compiler.md §7.6 loadModuleConfig 消费侧注入）。
#
# libssh2.a 已自包含 mbedTLS 密码学（零第三方依赖）；POSIX（darwin/linux）
#   的 socket 与熵源在 libc / /dev/urandom，无需额外链接，故无对应段。

[windows]
# libssh2 winsock 路径需 ws2_32；mbedTLS 的 Windows 熵源（CryptGenRandom /
# BCryptGenRandom）需 bcrypt + advapi32。
ldflags = -lws2_32 -lbcrypt -ladvapi32
