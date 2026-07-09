# spc 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 一期 darwin 专属（Metal kernel + MPSGraph + CoreML/ANE）；
# 非 darwin 目标源文件自守卫空化，无需链接项。

[darwin]
ldflags = -framework Cocoa -framework Metal -framework QuartzCore -framework OpenGL -framework IOSurface -framework CoreFoundation -framework MetalPerformanceShaders -framework MetalPerformanceShadersGraph -framework CoreML -framework Foundation
