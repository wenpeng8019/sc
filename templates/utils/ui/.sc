# ui 模块构建/链接配置（格式说明见 builtins/gpu/.sc）
# 控件库：共享逻辑 ui.c + 平台后端（darwin=Cocoa 原生；linux=Nuklear 软渲染；
# windows=Win32 原生；其余平台 null 空实现兜底）。
# cflags 的后端宏供模块库构建（scc . --build）；全部 src/* 一起编，
# 非选中后端由源文件内 SC_UI_* 守卫自空化。

[darwin]
# -fno-objc-arc：cocoa_ui.m 为 MRC 写法（手动 retain/release），覆盖默认 ARC
cflags = -DSC_UI_COCOA -fno-objc-arc

[windows]
cflags = -DSC_UI_WIN32

[linux]
cflags = -DSC_UI_NK
