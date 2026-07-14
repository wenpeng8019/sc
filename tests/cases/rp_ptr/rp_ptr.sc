# 正向：@@ 根导出注入的类型，经【指针形态】跨模块用于消费单元的 @导出签名。
#   根定义 cfg；消费单元 rp_consumer.sc 不 inc 根，却在其 @导出函数签名里以 cfg& 引用
#   根类型——编译器须在消费单元的 ABI 头 scm_<consumer>.h 里为 cfg 发【前向声明】
#   （typedef struct sc_cfg sc_cfg;），使根接口头（末位注入）之前 include 该头的单元
#   里 sc_cfg* 原型可编译。缺前向声明则 C 编译期报 "unknown type name 'sc_cfg'"。
# 本用例经 --test 真实编译+运行，端到端验证该前向声明链路。
@@                                  # 根模块标记：全局前奏（root-prelude）提供者
@def cfg: {
    x: i4
}

# 根导出的辅助操作（指针形态）：供消费单元/测试写入字段。
@fnc cfg_set_x: c: cfg&, v: i4
    c->x = v

inc rp_consumer.sc

tst "@@ 根类型 cfg 经指针跨模块用于消费单元导出签名（前向声明生效）"
    var c: cfg
    cfg_set_x(&c, 21)
    assert rp_double_x(&c) == 42
