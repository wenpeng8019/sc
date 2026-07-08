# scc 本地构建配置（key = value，'#' 行注释）。
# spc 的 macOS 三引擎需链接系统框架；从本目录运行 scc 时自动生效。
ldflags = -framework Metal -framework MetalPerformanceShaders -framework MetalPerformanceShadersGraph -framework CoreML -framework Foundation
