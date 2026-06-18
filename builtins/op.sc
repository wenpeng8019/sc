
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

# todo base 的声明
# 导航：base(o) 得到首个真实成员地址，prev(o) 得到前驱节点 void*，next(o) 得到后继节点 void*
#   - base(&t)：跳过注入的 I，取 T 首个真实成员的地址（与链表 base 对称）。

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

    fnc append:: it: &                              # 添加到队尾
    fnc push:: it: &                                # 添加到队首
    fnc pop:: &                                     # 移除并返回首元素（空返回 nil）
    fnc before:: pos: &, it: &                      # 添加 it 到 pos 之前
    fnc after:: pos: &, it: &                       # 添加 it 到 pos 之后
    fnc remove:: it: &                              # 移除元素（须在链中）
    fnc first:: &                                   # 首元素（空返回 nil）
    fnc last:: &                                    # 尾元素（空返回 nil）
    fnc revert::                                    # 首尾翻转
    fnc append_to:: dst: chain&                     # 整链接到 dst 尾部（自身清空）
    fnc push_to:: dst: chain&                       # 整链接到 dst 头部（自身清空）
    fnc cut:: from: &, to: &, out: chain&           # 截取 [from..to] 段为新链 out（out 被覆盖）
}

# 分身/切片机制
# ----------------------------------------------------------------------------- 
# 该机制可以让结构体对象生成分身/切片对象
# 语法 def T: <S> {}
#   - 这里 S 为分身/切片类型
#     @def S: { }
#     @def S: ~ { }      -> 支持链表注入（S 也可作为链表元素类型）
#     编译器会在 S 首部（若 S 带 ~，则在注入的 _prev/_next 之后）注入一个指向本体
#     的回指指针成员 _self: T&，使分身能反查到它所属的实体 T（free 时据此定位本体）。
#     在 S 的成员函数内可用上下文关键字 self（等价 _this->_self）访问本体实体 T。
#   - 这里 T 为 S 的实体类型，但必须具备以下必备成员函数：
#     alloc: fnc: S&, ...   # 分身构造器：隐式 this=T&，余参=切片参数，返回 S&
#     free: fnc: S&         # 分身析构器
# 机制：
# 语法糖：
#   - var s: T[...]
#     这里 T[...] 可以看成是一个类型，但本质是一个匿名结构体 T__project，
#     T[...] <==> {
#        ...                # T 的 alloc 参数，编译器会根据 T 的 alloc 声明自动生成
#        _: S&              # 分身/切片对象（nil=未绑定）
#     }
#     方括号内的初值即切片参数，存入句柄上下文（如 var s: buffer[2, 5]）。
#   - s = t
#     即用 T 对象（本体）对 T[...] 句柄（分身/切片）赋值。语法糖展开为：
#     s._ = T_alloc(&t, s.param, ...)   # 调用 T 的 alloc，传入本体地址与切片参数
#     s._->_self = &t                   # 编译器回写回指指针 _self 指向本体
#   - s = nil
#     即把 s 置空，语法糖展开为：
#     if s._ 
#         T_free(s._->_self, s._)       # 经回指指针 _self 取回本体 T，调用其 free(s._)
#         s._ = nil                     # 把 s._ 置空
# 

# 容器/成员机制
# ----------------------------------------------------------------------------- 
# 该机制可以给结构体类型构建一个容器，用来管理和检索该结构体类型的对象
# 语法 def T: <C, I> {}
#   - 这里 I 为注入到 T 首位的容器 Item 节点类型
#     @def I: { }
#     @def I: ~ { }      -> 支持链表注入（I 也可作为链表元素类型）
#   - 这里 C 为自定义容器类型，但必须具备必备以下成员函数：
#       insert: ret, item: I&, tag: ...   # 插入
#       remove: ret, item: I&             # 移除
#       find:   ret, out: I&&, ...        # 查找（出参回填）
#       first:  I&                        # 首元素
#       next:   I&, item: I&              # 后继
#     可选：last: I&   prev: I&, item: I&  # 反向导航
# 机制：
#   - T 构成容器的元素类型（如 treeNode）；I 是容器节点类型（如 node），被注入到 T 首位，
#     因此 T& 与 I& 可零偏移互相强转（编译器在方法实参处自动 treeNode& ⟷ node&）。
#   - 导航只经容器方法：t.first() / t.next(it)，返回 I&，用显式 `: T&` 下转回元素类型。


#  async 机制
# ----------------------------------------------------------------------------- 
# 给机制使得 rpc 可以支持异步操作（如线程、IO 等）
# 语法：
#  - await rpc [, timeout] 
#    等待 rpc 调用完成，或超时（单位 ms，默认无限等待）
#  - async rpc ... 
#    和 run 相对，run 是启动一个独立线程执行 rpc，而 async 则是让 rpc 在当前线程异步执行（如交给线程池、IO 队列等）
#    该操作会返回一个 future 对象（类型由 rpc 返回值类型自动推导），通过 future.get() 等待结果
#  - done future [, result]
#    标记 future 就绪并唤醒等待者（等价 future_done）；自定义异步原语在"完成时"调用以接入 await
# 机制：
#  - 通过线程局部存储，在当前线程维护一个当前正在执行的异步调用栈
#    编译器在编译 rpc 时，会根据内部 await 语言法糖的使用情况，以及流程中异步变量的依赖访问情况，
#    自动将 rpc 处理过程拆分为多个阶段，然后将异步变量和参数一起作为 rpc 结构体字段一部分，
#    并在此基础上，增加一个 stage 字段来标记当前执行阶段，从而实现异步调用的状态机机制
#    运行时，会根据 stage 字段进行 goto 跳转来恢复执行，而这里关键在于依赖上下文的状态，全部保存在 rpc 结构体中
#
# 分工：future 类型 + async_init/loop/final 是语言底层机制，声明在此（默认导入）；
#       C 结构体/原型见 op.h（默认带入每个 C 单元）。默认运行时（libuv）实现在
#       builtins/async/async_impl.c —— 仅 inc async.sc 时才链接（不污染普通程序）。
#       delay 等 libuv 叶子原语属"异步功能库"，保留在 async 模块（inc async.sc）。

# ---------------- future：异步结果句柄（类型擦除） ----------------
# 字段为 C ABI 布局占位（实际定义在 op.h；sc 侧仅按方法访问，不直接读字段）。
@def future: {
    _ready: i4          # 0=未就绪, 1=已就绪
    _result: &          # 类型擦除结果
    _frame: &           # 等待者状态机帧（await 点登记）
    _resume: &          # 等待者恢复入口
    _next: &            # 就绪队列链接（运行时内部）

    fnc init::          # future()：伪类构造，登记到当前事件循环（未就绪）
    fnc ready:: bool    # f.ready()：是否已就绪
    fnc get:: &         # f.get()：取结果（调用点用 : T& 强转还原类型）
}

# ---------------- 事件循环生命周期 ----------------
@fnc async_init::           # 建立当前线程事件循环
@fnc async_loop::           # 驱动事件循环至全部异步完成
@fnc async_final::          # 销毁当前线程事件循环

# com 通讯机制
# ----------------------------------------------------------------------------- 
# 语言层面提供设备通讯的基础能力
# 语法：
# << 操作符: send buf 字节流到 COM
# >> 操作符: 从 COM recv 字节流到 buf
# 机制：
#   - 支持通过 ioq 来实现异步通讯机制
#     即如果 com 的 rq/wq 不为 nil，则说明提供并支持了异步 io 的能力
#   - 支持对 rpc 机制在语法层面的打通
#     对于异步 io 通讯，则 << 和 >> 操作只能在 rpc 中 使用
#     此时会自动利用 rpc 的 await 机制来实现异步访问
#     也就是编译器会把 << 和 >> 操作转移为特定 await 机制相关的语句
# 示例：
# rpc do_some: a:i4: a:i4: a:i4
#     return
# rpc on_some: a:i4: a:i4: a:i4
#     return
# fnc call: com&
#     com << val1 << val2
#     com << do_some(a, b, c)
#     com >> res
#     com >> on_some(a, b, c)
#     return

# fnc init::                          # 初始化 limit（如填充边界数据）                                
@def limit: { 
    fnc data:: &                      # 返回 limit 数据起始地址
    fnc len:: u4                      # 返回 limit 数据长度（不含边界本身）
}

# @def com: <limit> {
#     dev: &
#     rq: ioq&
#     wq: ioq&

    # n: u1  >0: 由后续 limit 个字节序列作为结束边界（最大序列长度 255）
    # 0: 固定长度边界;
    #    > 如果后续 2 字节小于 0xFFFF，则作为 u2 长度边界（最大序列长度 65534）
    #    > 如果后续 2 字节等于 0xFFFF，则作为 u4 长度边界（最大序列长度 4294967295）
#     alloc: fnc: limit&, 
#     free: fnc: s: limit&

#     read: fnc: com&, data: &, size: u4&
#     write: fnc: com&, buf: &, size: u4&
#     error: fnc: ret
# }

# @def ioq: {
#     com: com&
#     fnc push:: buf: &, size:i4
#     fnc done:: cb: fnc:, data: &
#     fnc pull:: &                                    # 空队列返回 nil，区别于 pop 的是 pull 会阻塞等待直到有元素可用
# }

