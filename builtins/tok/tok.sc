# tok —— 分布式 token（tok / dep / form 机制）的运行时模块
#
# 本目录是 tok 机制「实现」的事实源（op 层只承载接口协议）：
#   · 句柄类型协议 @def token（方法 get/set）声明在 builtins/op.sc —— 语言内核机制，
#     默认导入，供编译器识别 t.get()/t.set() 方法分派；
#   · C ABI 契约见同目录 tok.h（由 op.h 默认带入每个 C 单元）；
#   · 默认运行时见同目录 tok_impl.c（编译器经 op→tok 隐式依赖自动编译并链接）。
#
# tok / dep / form 是语言关键字（恒可用，无需 inc）：
#   tok t: "id"        声明 token 句柄（enforce 纯从：set 直接赋值）
#   tok t: "id": 体     声明并挂 combine 回调（form 候选：体即 combine，据当前/输入算新值）
#   form t, v          初始化 form token：灌初值并升格为 form 主
#   dep all/any: ...   声明 token 间依赖（follow 回调，all=与门 / any=或门）
#   t.get() / t.set(v) 取值 / 设值（set 触发依赖级联）
#
# 完整说明见 builtins/tok.md。
#
# 本模块自身不声明 sc 侧符号：token 协议在 op.sc，C ABI 在 tok.h。
# 本文件仅作为运行时载体（同目录 tok_impl.c 经拼接机制随工程链接）。
