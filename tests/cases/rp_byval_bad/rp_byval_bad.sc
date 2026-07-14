# 负向：@@ 根导出注入的类型，被消费单元【按值】用于 @导出签名 —— 应报错。
#   根类型 cfg 的完整定义仅随末位根接口头进入消费单元 .c；若消费单元把 cfg 按值放进
#   自己的 @导出签名，该签名进入 ABI 头 scm_<consumer>.h，其它单元 include 它早于末位
#   根头 → C 编译期 "unknown type name 'sc_cfg'"。语义层在此提前拦截，强制指针形态 cfg&。
@@                                  # 根模块标记：全局前奏提供者
@def cfg: {
    x: i4
}

inc rp_consumer_bad.sc

fnc main: i4
    return 0
