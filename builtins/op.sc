
# op.sc —— 语言底层（语法层面）机制的 sc 侧声明（platform.h / op.h 的 sc 侧）
#
# 角色：与 platform.h 一样属"默认导入"，无需显式 inc。汇集编译器需要感知的语法
#       机制类型与接口声明，据此识别方法调用糖、字段访问、链表/容器注入等语法糖：
#         operand —— 设备操作数 . 透传（原子读写等），C 侧由 platform.h 的 sc_<op> 宏实现
#         chain   —— 侵入式双向链表（def T: ~ {}），C 侧由 op.h / op_impl.c 实现
#       （后续切片/容器/COM 等机制亦归于此）。
# 运行时：本模块声明不生成代码、不参与链接；其 C 结构体/原型/实现由对应运行时提供
#         （chain → builtins/op.h + op_impl.c，编译器随每个工程自动编译并链接）。


# ---------------- operand：设备操作数通用指令集 ----------------
# 机制：下面 operand 伪结构体的每个成员函数声明一条指令。语法上为基础类型（i4、
#       type& 等）扩展 . 操作来调用：v.get() / p->set(x)。scc 不生成 operand_xxx，
#       而是透传为 platform.h 的同名 sc_<op> 宏（接收者以指针传入，值接收者自动取址）。
#       指令类型无关（C 侧 __typeof__ 推导），故忽略入参与返回值类型。
# 扩展：新增一条指令时，在此 operand 内加一行 fnc 声明，并在 platform.h 加同名
#       sc_<op> 宏即可。

def operand: {
    fnc get::                            # v.get()      → sc_get(&v)        原子读（relaxed）
    fnc set::                            # v.set(x)     → sc_set(&v, x)     原子写（relaxed）
    fnc get_acq::                        # v.get_acq()  → sc_get_acq(&v)    原子读（acquire）
    fnc set_rel::                        # v.set_rel(x) → sc_set_rel(&v, x) 原子写（release）
}

# 导航：base(o) 得到首个真实成员地址，prev(o) 得到前驱节点 void*，next(o) 得到后继节点 void*

# 双向链机制
# ----------------------------------------------------------------------------- 
# 该机制可以让结构体对象形成双向链表
# 语法：def T: ~ {}
# 机制：
# 编译器自动在结构体首位注入 void* _prev/_next 字段，构成链表节点；
# 链表头部由 chain 结构体管理
# ! 注意，chain 集合并不拥有元素：remove/pop/cut 也不释放元素本身

@def chain: {
    head: &        # 首元素（空链为 nil）

    fnc append:: it: &                               # 添加到队尾
    fnc push:: it: &                                 # 添加到队首
    fnc pop:: &                                     # 移除并返回首元素（空返回 nil）
    fnc before:: pos: &, it: &                        # 添加 it 到 pos 之前
    fnc after:: pos: &, it: &                         # 添加 it 到 pos 之后
    fnc remove:: it: &                               # 移除元素（须在链中）
    fnc first:: &                                   # 首元素（空返回 nil）
    fnc last:: &                                    # 尾元素（空返回 nil）
    fnc revert::                                     # 首尾翻转
    fnc append_to:: dst: chain&                      # 整链接到 dst 尾部（自身清空）
    fnc push_to:: dst: chain&                        # 整链接到 dst 头部（自身清空）
    fnc cut:: from: &, to: &, out: chain&              # 截取 [from..to] 段为新链 out（out 被覆盖）
}

# 分身/切片机制
# ----------------------------------------------------------------------------- 
# 该机制可以让结构体对象生成分身/切片对象
# 语法 def T: <S> {}
# 机制：
#   这里 S 为分身/切片类型
#   @def S: { fnc alloc:: &, ... }
# 语法糖：
#   T[...] → T 的分身/切片对象，等价 S.alloc(&T, ...)


# 容器/成员机制
# ----------------------------------------------------------------------------- 
# 该机制可以给结构体类型构建一个容器，用来管理和检索该结构体类型的对象
# 语法 def T: <C, I> {}
# 机制：
#   - T 构成容器的元素类型（如 treeNode）；I 是链接节点类型（如 node），被注入到 T 首位，
#     因此 T& 与 I& 可零偏移互相重解释（编译器在方法实参处自动 treeNode& ⟷ node&）。
#   - C 是自定义容器（普通伪类结构体），必须具备必备成员函数：
#       insert: ret, item: I&, tag: ...   # 插入
#       remove: ret, item: I&             # 移除
#       find:   ret, out: I&&, ...        # 查找（出参回填）
#       first:  I&                        # 首元素
#       next:   I&, item: I&              # 后继
#     可选：last: I&   prev: I&, item: I&  # 反向导航
#   - 导航只经容器方法：t.first() / t.next(it)，返回 I&，用显式 `: T&` 下转回元素类型。
#   - ret：ADT 接口返回码（i4 别名）；ok：成功返回码（= 0）。
#   - base(&t)：跳过注入的 I，取 T 首个真实成员的地址（与链表 base 对称）。


# COM 机制
# ----------------------------------------------------------------------------- 
# com 结构体：COM 设备通信上下文

# n: u1                               # >0: 由后续 limit 个字节序列作为结束边界（最大序列长度 255）
    #                                     # 0: 固定长度边界;
    #                                     #    > 如果后续 2 字节小于 0xFFFF，则作为 u2 长度边界（最大序列长度 65534）
    #                                     #    > 如果后续 2 字节等于 0xFFFF，则作为 u4 长度边界（最大序列长度 4294967295）
    # fnc init::                          # 初始化 limit（如填充边界数据）                                
    # fnc data:: &                        # 返回 limit 数据起始地址
    # fnc len:: u4                        # 返回 limit 数据长度（不含边界本身）
@def limit: {
    fnc init::                            # 初始化 limit（如填充边界数据）
}

# [...] 操作符: limit(...) 伪调用 → limit.init(&owner, ...)
# << 操作符: send buf 字节流到 COM
# >> 操作符: 从 COM recv 字节流到 rpc
# @def com: <limit> {
#     alloc: fnc: limit&
#     send: fnc: com&, msg: i4, data: &, size: u4&
#     recv: fnc: com&, buf: &, size: u4&
#     ready: fnc: ret
# }

# rpc do_some: msg: i4
#     return

# rpc on_some1: msg: i4, data: com['aaa'], desc:com
#     return

# rpc on_some2: com&
#     return

# fnc call: com&
#     var v: chunk
#     com << val
#     com << on_some1
#     com >> res
#     com >> do_some(a, b, c)
#     return