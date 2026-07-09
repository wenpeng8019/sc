# wsi 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 窗口库：darwin = Cocoa/IOKit；linux X11/Wayland 链接项待板验补充。

[darwin]
ldflags = -framework Cocoa -framework IOKit -framework CoreFoundation -framework QuartzCore
