# sagent/src 模块段配置（§7.4）：http.sc（libcurl vendor）的链接自描述
# lib = 库搜索目录（相对本目录展开）；libs = 库名。交叉目标另配段（TAG 对应）。

[darwin]
lib     = ../../vendor/curl/out/host/lib
libs    = curl, mbedtls_all
ldflags = -framework CoreFoundation -framework SystemConfiguration

[linux]
lib     = ../../vendor/curl/out/host/lib
libs    = curl, mbedtls_all, pthread
