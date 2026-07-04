# plib —— 跨平台库模板（sc FFI 侧）
#
# 展示 sc 如何引入独立的跨平台原生库：
#   库本身通过 build.sh 独立编译为 .a，sc 侧只做 FFI 声明 + add 链接。
#
# 平台适配：build.sh 产出 libplib.<triple>.a，add libplib.a 由编译器自动匹配变体。

inc plib.h
add libplib.a

@fnc plib_hello:: const char&
@fnc plib_add:: i4, a: i4, b: i4
