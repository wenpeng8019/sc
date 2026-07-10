# gfx 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 与 gpu 同目标共链一套平台库（消费侧按词元查重，重复注入自动去重）。

[darwin]
ldflags = -framework Cocoa -framework Metal -framework QuartzCore -framework OpenGL -framework IOSurface -framework CoreFoundation

[*gles*]
cflags  = -DSC_GPU_GLES
inc     = ../gpu/khr
ldflags = -lGLESv2 -lEGL -lgbm

[windows]
# WGL 桌面 GL：GL 函数 = opengl32（与 gpu 同链一套，重复自去重）
ldflags = -lopengl32

[linux]
ldflags = -lGL -lEGL -lgbm -lvulkan
