# gfx 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 与 gpu 同目标共链一套平台库（消费侧按词元查重，重复注入自动去重）。

[*ios*]
# iOS/tvOS/模拟器：仅 Metal（须置于 [darwin] 之前，首个命中独占）。
ldflags = -framework Metal -framework QuartzCore -framework UIKit -framework Foundation -framework IOSurface -framework CoreFoundation

[darwin]
ldflags = -framework Cocoa -framework Metal -framework QuartzCore -framework OpenGL -framework IOSurface -framework CoreFoundation

[*gles*]
cflags  = -DSC_GPU_GLES
inc     = ../gpu/khr
ldflags = -lGLESv2 -lEGL -lgbm

[windows]
# WGL 桌面 GL：GL 函数 = opengl32（与 gpu 同链一套，重复自去重）。
# Vulkan 头自包含（inc 指向 gpu/khr），运行时动态加载，无 -lvulkan-1。
# D3D11 后端：d3d11 + dxgi（设备/交换链）+ d3dcompiler（HLSL→DXBC 运行时编译）。
inc     = ../gpu/khr
ldflags = -lopengl32 -ld3d11 -ldxgi -ld3dcompiler

[linux]
inc     = ../gpu/khr
ldflags = -lGL -lEGL -lgbm
