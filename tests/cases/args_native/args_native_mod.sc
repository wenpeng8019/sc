# args_native 消费子模块：演示「根模块导出注入（@@）」头文件透传。
#
# 本模块**不** inc 根模块 args_native.sc，却直接读取根经 mix 展开并 @导出 的
# 全局 ARGS_verbose —— 这正是「根模块导出注入」的效果：
#   编译器把根的接口头 scm_<root>.h 作为本单元 .c 的末位 include，
#   并把根的 @导出 声明以 external 并入本单元语义，使 ARGS_verbose 全局可见。
# 仅 arg_var_st 类型本身仍由 sys.sc 提供（故 inc sys.sc）。
#
# 开启条件：同目录存在被 @@ 标注的根（见 args_native.sc）。移除根的 @@ 标记
# 即关闭（届时本模块将因找不到 ARGS_verbose 而报错）。

inc sys.sc

@fnc args_report_verbose:
    if ARGS_verbose                # 直接访问根注入的 ARGS_verbose 属性（bool）
        ::printf("v=1\n")
