# 消费单元：不 inc 根，经 @@ 注入直接使用根类型 cfg。
#   关键点：cfg 用于本单元 @导出函数 rp_double_x 的【签名】（指针形态 cfg&）——
#   该签名进入本单元 ABI 头 scm_rp_consumer.h，其它单元 include 它早于末位根接口头，
#   故编译器须在本 ABI 头为 cfg 补前向声明。
@fnc rp_double_x: i4, c: cfg&
    return c->x * 2
