# wsi 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 窗口库：darwin = Cocoa/IOKit；linux X11/Wayland 链接项待板验补充。
#
# cflags 的平台后端宏供模块库构建（scc . --build，glfw 多后端设计：
# 一个库可同时含多个后端，运行时 wsi_select_platform 选择）；
# 全部 src/* 一起编，非本平台源由源文件内 WSI_* 守卫自空化。
# linux 的 wayland 协议头由 build.sh 先行生成到 build/wayland-protocols。

[darwin]
# -fno-objc-arc：cocoa_platform.m 为 MRC 写法（glfw 遗产，手动 retain/release），
# 覆盖 scc 对 .m 默认注入的 -fobjc-arc（段 cflags 追加在后，后者胜）
cflags  = -DWSI_COCOA -DWSI_SHARED -DWSI_EXPORTS -fno-objc-arc
ldflags = -framework Cocoa -framework IOKit -framework CoreFoundation -framework QuartzCore

[windows]
cflags  = -DWSI_WIN32 -DUNICODE -DOEMRESOURCE -DWSI_SHARED -DWSI_EXPORTS

[linux]
cflags  = -DWSI_X11 -DWSI_WAYLAND -DWSI_SHARED -DWSI_EXPORTS
inc     = build/wayland-protocols
# X11/Wayland 运行时 dlopen（glfw 遗产），链接期只需 libm（round 等）+ libdl
ldflags = -lm -ldl
