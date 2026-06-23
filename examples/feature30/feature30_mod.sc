# 特性 30 附属模块：根（集成单元）导出注入的「消费单元」。
#
# 本模块**不** inc 根模块 feature30.sc，却直接引用根导出的全局类型 metric
# 与全局操作 app_report —— 这正是「根模块导出注入」机制的效果：
#   编译器把根的导出接口头 scm_<root>.h 作为本单元 .c 的末位 include，
#   并把根的 @导出 声明以 external 并入本单元语义，使其全局可见。
#
# 开启条件：同目录存在被 @@ 标注的根模块（见 feature30.sc）。该机制仅在
# 「可执行（EXE）构建」下生效。移除根的 @@ 标记即可关闭（届时本模块将因
# 找不到 metric/app_report 而报错）。

inc stdio.h

# 直接使用根导出的 metric（类型）与 app_report（操作），无需 inc 根模块。
@fnc sensor_sample: label: char&, v: i4
    var m: metric
    m.tag = label
    m.value = v
    app_report(m)
