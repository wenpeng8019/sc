# gpu 模块构建/链接配置（INI 段扩展：段名 fnmatch 匹配 target_suffix → triple → 平台族）
# 消费侧（inc gpu.sc）按当前目标注入；模块库构建（scc builtins/gpu --build）亦读取。
# 形态标签编码进 target_suffix（自由文本）：GLES 板卡目标档写
#   target_suffix = aarch64-linux-gnu-gles   ← 命中 [*gles*] 段(cflags/inc/ldflags 全套)
# 即命中 [*gles*] 段。段自上而下取首个命中：特殊形态在前、一般平台在后。

[darwin]
ldflags = -framework Cocoa -framework Metal -framework QuartzCore -framework OpenGL -framework IOSurface -framework CoreFoundation

[*gles*]
cflags  = -DSC_GPU_GLES
inc     = khr
ldflags = -lGLESv2 -lEGL -lgbm

[linux]
ldflags = -lGL -lEGL -lgbm -lvulkan -lX11
