# wsi 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 窗口库：darwin = Cocoa/IOKit；linux X11/Wayland 链接项待板验补充。
#
# cflags 的平台后端宏供模块库构建（scc . --build，glfw 多后端设计：
# 一个库可同时含多个后端，运行时 wsi_select_platform 选择）；
# 全部 src/* 一起编，非本平台源由源文件内 WSI_* 守卫自空化。
# linux 的 wayland 协议头由 build.sh 先行生成到 build/wayland-protocols。

# [*ios*] / [*simulator*] 必须置于 [darwin] 之前：iOS 三元组（arm64-apple-ios*）
# 同时含 "apple"（平台族=darwin），段自上而下取首个命中；模拟器三元组
# （arm64-apple-ios-simulator）又同时含 *ios* 与 *simulator*，故 [*simulator*]
# 需再置于 [*ios*] 之前，独占匹配模拟器（真机 SDK/部署目标标志与模拟器不同）。
[*simulator*]
# iOS 模拟器后端：同 uikit_platform.m，但 SDK/部署目标由构建脚本的 cc 包装
# （xcrun -sdk iphonesimulator + -target arm64-apple-ios*-simulator + -isysroot）
# 提供，故此处不带 -mios-version-min（避免与 -target ...-simulator 冲突）。
cflags  = -DWSI_IOS -DWSI_SHARED -DWSI_EXPORTS -fno-objc-arc
ldflags = -framework UIKit -framework QuartzCore -framework Metal -framework Foundation

[*ios*]
# iOS 后端（uikit_platform.m）：全屏 UIKit 窗口 + CADisplayLink 帧驱动 + 触摸/生命周期。
# -fno-objc-arc：与 cocoa 一致用 MRC（id 存于 C 结构体）。最低 iOS 13。
cflags  = -DWSI_IOS -DWSI_SHARED -DWSI_EXPORTS -fno-objc-arc -mios-version-min=13.0
ldflags = -framework UIKit -framework QuartzCore -framework Metal -framework Foundation

[darwin]
# -fno-objc-arc：cocoa_platform.m 为 MRC 写法（glfw 遗产，手动 retain/release），
# 覆盖 scc 对 .m 默认注入的 -fobjc-arc（段 cflags 追加在后，后者胜）
cflags  = -DWSI_COCOA -DWSI_SHARED -DWSI_EXPORTS -fno-objc-arc
ldflags = -framework Cocoa -framework IOKit -framework CoreFoundation -framework QuartzCore

[windows]
cflags  = -DWSI_WIN32 -DUNICODE -DOEMRESOURCE -DWSI_SHARED -DWSI_EXPORTS
# win32 后端：窗口/监视器/原始输入 = user32；GDI 绘制 = gdi32；拖放 = shell32
# （-l<名> 经 scc 翻译层转 <名>.lib；MSVC 控制台子系统不自动链接这些 GUI 库）
ldflags = -luser32 -lgdi32 -lshell32

# [*android*] 必须置于 [linux] 之前：android 三元组（aarch64-linux-android 等）
# 同时含 "linux"，段自上而下取首个命中，否则会被 [linux] 抢匹配（走 X11/Wayland）。
[*android*]
# Android 后端（android_platform.c 待实现 + android_jni.c 进程级 JNI 桥，均由
# WSI_ANDROID 守卫）。NDK sysroot 自带 jni.h/android/*；链接 libandroid（ANativeWindow/
# AChoreographer 等）+ liblog（__android_log_print）。cflags/ldflags 供模块库构建。
cflags  = -DWSI_ANDROID -DWSI_SHARED -DWSI_EXPORTS
ldflags = -landroid -llog

[linux]
cflags  = -DWSI_X11 -DWSI_WAYLAND -DWSI_SHARED -DWSI_EXPORTS
inc     = build/wayland-protocols
# X11/Wayland 运行时 dlopen（glfw 遗产），链接期只需 libm（round 等）+ libdl
ldflags = -lm -ldl
