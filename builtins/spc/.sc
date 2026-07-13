# spc 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# kernel 面按 gpu 后端派发：darwin=Metal、linux/android=GL·Vulkan、win=Vulkan。
# graph/model 面仍 darwin 专属（MPSGraph/CoreML）；非命中平台源文件自守卫空化。
# Vulkan 头 = gpu 模块 vendored（gpu/khr）；GLES 头同源。

[*ios*]
# iOS：仅 Metal kernel + CoreML（无 GL/Vulkan）
ldflags = -framework Metal -framework Foundation -framework CoreML -framework MetalPerformanceShaders -framework MetalPerformanceShadersGraph

[darwin]
ldflags = -framework Cocoa -framework Metal -framework QuartzCore -framework OpenGL -framework IOSurface -framework CoreFoundation -framework MetalPerformanceShaders -framework MetalPerformanceShadersGraph -framework CoreML -framework Foundation

[*gles*]
# 嵌入式 GLES3.1 形态（同 gpu [*gles*]）：gl_spc 走 GLES compute
cflags  = -DSC_GPU_GLES
inc     = ../gpu/khr
ldflags = -lGLESv2 -lEGL

[*android*]
# Android：GLES3.1 compute + Vulkan compute（libvulkan.so 动态加载，见 gpu vk_loader）
cflags  = -DSC_GPU_GLES
inc     = ../gpu/khr
ldflags = -lGLESv3 -lEGL

[windows]
# Vulkan compute（vulkan-1.dll 动态加载）；GL compute 待桌面 GL 加载器
inc     = ../gpu/khr

[linux]
# 桌面 GL4.3 compute + Vulkan compute
inc     = ../gpu/khr
ldflags = -lGL -lEGL
