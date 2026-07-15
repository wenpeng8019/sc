# http 模块消费侧链接配置（格式见 compiler.md §7.6）。
# 当前 native 后端与 mbedTLS 由 build.sh 从 vendor 源码构建到本目录；
# add libcurl.a / libmbedtls_all.a 负责目标库，以下只补平台系统依赖。

[darwin]
ldflags = -framework CoreFoundation -framework SystemConfiguration

[linux]
ldflags = -lpthread

[windows]
ldflags = -lws2_32 -lbcrypt -ladvapi32
