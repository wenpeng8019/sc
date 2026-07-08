# scc 本地构建配置（key = value，'#' 行注释）。
# gfx 的 macOS 后端（Metal + GL）需链接系统框架；从本目录运行 scc 时自动生效。
# 交叉编译其他平台时请改用对应 --target 目标档的 ldflags。
ldflags = -framework Metal -framework QuartzCore -framework OpenGL -framework Cocoa
