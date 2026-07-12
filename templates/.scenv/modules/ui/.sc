# ui 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 控件库：共享逻辑 ui.c + 平台后端（darwin=Cocoa 原生；ios=UIKit 原生；
# android=原生控件经 wsi JNI 反射代理；linux=Nuklear 软渲染；
# windows=Win32 原生；其余平台 null 空实现兜底）。
# cflags 的后端宏供模块库构建（scc . --build）；全部 src/* 一起编，
# 非选中后端由源文件内 SC_UI_* 守卫自空化。

# [*ios*]/[*simulator*] 须置于 [darwin] 之前（iOS 三元组含 apple/darwin 族），
# [*android*] 须置于 [linux] 之前（android 三元组含 linux）——段自上而下取首个命中。
[*simulator*]
cflags = -DSC_UI_UIKIT -fno-objc-arc
ldflags = -framework UIKit -framework QuartzCore

[*ios*]
cflags = -DSC_UI_UIKIT -fno-objc-arc -mios-version-min=13.0
ldflags = -framework UIKit -framework QuartzCore

[darwin]
# -fno-objc-arc：cocoa_ui.m 为 MRC 写法（手动 retain/release），覆盖默认 ARC
cflags = -DSC_UI_COCOA -fno-objc-arc

[*android*]
# android 原生控件后端（android_ui.c）：经 wsi 的 sc_jni_* 反射代理驱动。
# sc_jni_* 符号在 libwsi 内，最终 app 链接期解析（ui 静态库允许未决符号）。
cflags = -DSC_UI_ANDROID

[windows]
cflags = -DSC_UI_WIN32
# win32 原生控件：窗口/消息 = user32；字体/绘制 = gdi32（-l<名> → <名>.lib）
ldflags = -luser32 -lgdi32

[linux]
cflags = -DSC_UI_NK
