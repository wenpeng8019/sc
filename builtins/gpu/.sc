# gpu 模块构建/链接配置（INI 段扩展：段名 fnmatch 匹配 target_suffix → triple → 平台族）
# 消费侧（inc gpu.sc）按当前目标注入；模块库构建（scc builtins/gpu --build）亦读取。
# 形态标签编码进 target_suffix（自由文本）：GLES 板卡目标档写
#   target_suffix = aarch64-linux-gnu-gles   ← 命中 [*gles*] 段(cflags/inc/ldflags 全套)
# 即命中 [*gles*] 段。段自上而下取首个命中：特殊形态在前、一般平台在后。

[*ios*]
# iOS/tvOS/模拟器：仅 Metal（无桌面 NSOpenGL）；UIKit 供 UIView layer 取用。
# triple/target_suffix 含 "ios"（含 -simulator），须置于 [darwin] 之前（首个命中独占）。
ldflags = -framework Metal -framework QuartzCore -framework UIKit -framework Foundation -framework IOSurface -framework CoreFoundation

[darwin]
ldflags = -framework Cocoa -framework Metal -framework QuartzCore -framework OpenGL -framework IOSurface -framework CoreFoundation

[*gles*]
cflags  = -DSC_GPU_GLES
inc     = khr
ldflags = -lGLESv2 -lEGL -lgbm

[*android*]
# Android NDK：GLES3 + EGL 窗口交换链（ANativeWindow 即 EGLNativeWindowType）。
# 无 GBM/dma-heap/X11 → 无 headless memimg（走窗口路径；memimg 待接 AHardwareBuffer）。
# triple/target_suffix 含 "android"（属 linux 族但 family=android，[linux] 不命中），
# 须置于 [linux] 之前。
cflags  = -DSC_GPU_GLES
inc     = khr
ldflags = -lGLESv3 -lEGL

[windows]
# WGL 桌面 GL：GL 函数 = opengl32；ChoosePixelFormat/SetPixelFormat/SwapBuffers = gdi32；
# GetDC/ReleaseDC = user32。Vulkan：自包含——头文件 vendor 至 khr/（inc）、运行时
# 动态加载 vulkan-1.dll（vk_loader.c），无需 -lvulkan-1 与 Vulkan SDK。
# GL 零拷贝导出（WGL_NV_DX_interop2）：d3d11 + dxgi + dxguid（共享 NT 句柄）。
inc     = khr
ldflags = -lopengl32 -lgdi32 -luser32 -ld3d11 -ldxgi -ldxguid

[linux]
inc     = khr
ldflags = -lGL -lEGL -lgbm -lX11
