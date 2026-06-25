
# op.sc —— 语言底层（语法层面）机制的 sc 侧声明（platform.h / op.h 的 sc 侧）
#
# 角色：与 platform.h 一样属"默认导入"，无需显式 inc。汇集编译器需要感知的语法
#       机制类型与接口声明，据此识别方法调用糖、字段访问、链表/容器注入等语法糖：
#         operand —— 设备操作数 . 透传（原子读写等），C 侧由 platform.h 的 sc_<op> 宏实现
#         chain   —— 侵入式双向链表（def T: ~ {}），C 侧由 op.h / op_impl.c 实现
#       （后续切片/容器/COM 等机制亦归于此）。
# 运行时：本模块声明不生成代码、不参与链接；其 C 结构体/原型/实现由对应运行时提供
#         （chain → builtins/op.h + op_impl.c，编译器随每个工程自动编译并链接）。

# 引用与释放机制
# ----------------------------------------------------------------------------- 
# 引用计数：
# + 现在语法通过 () 伪构造可以实现 malloc 分配对象
#   此时，分配对象，会实际分配 sizeof(ref:i4) + sizeof(struct)，
#   然后将 alloc 的地址偏移 sizeof(ref:i4) 返回给用户，但需要考虑内存对齐问题
#   这样每个动态分配的对象前面都有一个 ref:i4 的引用计数，用来追踪对象的引用和自动释放
#   跟踪的规则和其他语言原则一致：
#   + 创建初始为 1，赋值指针变量增加引用。指针变量重置或所在域结束时减少引用。引用计数为 0 时自动释放对象。
# 
# 创建指针对象：{ p:& tar:& own:& } 
# + 编译器自动将所有指针变量，都变成 { p:& tar:& own:& } 结构体
#   它由三部分组成 p 存实际目标对象地址，tar 存目标对象的 ref 变量的地址，own 该指针变量所属对象/域的 ref 变量的地址
#   指针变量指向的目标对象，以及指针变量所属对象/域，要么来自堆分配，要么来自栈分配。
#   对于堆分配，上面的引用计数机制已确保其存在 ref 变量
#   对于栈分配，下面的 & 取址操作所述，会在栈对象所在域开始，自动为其分配一个 ref:i4 的引用计数变量
#
# & 取址操作：
#   逐级找到该（成员）变量所属对象的根节点。最后该根节点
#   - 它要么是某个结构体类型对象的起始地址（如 struct T { ... } 的 T 对象），
#   - 它要么是某个数组类型对象的起始地址（如 T[] 的数组对象）
#   - 要么就是某个函数子作用域内的局部变量（如 var x: i4 = 0; 中的 x）
#   确定根节点的对象类型
#   - 如果是堆对象，就是前面说的，会自动带 ref:i4 的引用计数机制。所以取值操作会增加其引用计数
#   - 如果是栈对象，此时会在所在域开始，自动为其分配一个 ref:i4 的引用计数，并在域结束时检查，如不为 0 则跟踪报错
#
# 原则：
# 1. 只负责根对象的自动释放。也就是域结束时，会自动释放域内所有栈对象的引用计数。
#    这和其他 rust/java 这种完全自动释放的语言不同。
#    这既减少了使用成本（还要区分所属权、转移所属权等），又增加了 C 指针的灵活性
# 2. 对应的，域根对象释放时，或者用户主动释放时，会检测并确保其引用计数为 0，保证报错
#    这也是这里的设计原则，即确保正确以及错误可追踪，而非完全替代
# 3. 关键指针赋值时，会双向增加引用。（如果双向为自引用，则不会增加引用，此时可以将 own 置空，以示区分）

# ---------------- operand：设备操作数通用指令集 ----------------
# 机制：下面 operand 伪结构体的每个成员函数声明一条指令。语法上为基础类型（i4、
#       type& 等）扩展 . 操作来调用：v.get() / p->set(x)。scc 不生成 operand_xxx，
#       而是透传为 platform.h 的同名 sc_<op> 宏（接收者以指针传入，值接收者自动取址）。
#       指令类型无关（C 侧 __typeof__ 推导），故忽略入参与返回值类型。
# 扩展：新增一条指令时，在此 operand 内加一行 fnc 声明，并在 platform.h 三个平台
#       分支各加同名 sc_<op> 宏即可。
# 内存序后缀：无=relaxed、_acq=acquire、_rel=release、_dbl=acq_rel、_ord=seq_cst。
#   · sc_inc/and/or/xor 返回新值；sc_get_and_* 返回旧值；
#   · sc_test_and_set* 为 CAS，返回 bool（成功 true，失败把实际旧值写回 *pTest）。

def operand: {
    # 序列化指令（按接收者标量宽度 2/4/8 派发 sc_<op>_s/_l/_ll；非 2/4/8 字节报错）。
    # buf 为字节缓冲指针；与原子指令不同，这些要看变量类型选 platform 的 read_s/_l/_ll 等。
    fnc read::                           # v.read(buf)   → sc_read_X(&v, buf)   主机序：buf→v（反序列化）
    fnc write::                          # v.write(buf)  → sc_write_X(buf, v)   主机序：v→buf（序列化）
    fnc nread::                          # v.nread(buf)  → sc_nread_X(&v, buf)  网络序：buf→v
    fnc nwrite::                         # v.nwrite(buf) → sc_nwrite_X(buf, v)  网络序：v→buf
    fnc nget::                           # v.nget()      → sc_nget_X(v)         取 v 的网络序值（host→net，返回）
    fnc nset::                           # v.nset(x)     → v = sc_nset_X(x)     把网络序值 x 转主机序赋给 v（net→host）


    # 原子读/写：get=relaxed 读，set=relaxed 写（带内存序后缀变体）
    fnc get::                            # v.get()      → sc_get(&v)        原子读（relaxed）
    fnc get_acq::                        # v.get_acq()  → sc_get_acq(&v)    原子读（acquire）
    fnc get_ord::                        # v.get_ord()  → sc_get_ord(&v)    原子读（seq_cst）
    fnc set::                            # v.set(x)     → sc_set(&v, x)     原子写（relaxed）
    fnc set_rel::                        # v.set_rel(x) → sc_set_rel(&v, x) 原子写（release）
    fnc set_ord::                        # v.set_ord(x) → sc_set_ord(&v, x) 原子写（seq_cst）

    # 原子交换（返回旧值）：get_and_set
    fnc get_and_set::                    # v.get_and_set(x)     → sc_get_and_set(&v, x)     交换（relaxed）
    fnc get_and_set_dbl::                # v.get_and_set_dbl(x) → sc_get_and_set_dbl(&v, x) 交换（acq_rel）
    fnc get_and_set_acq::                # 交换（acquire）
    fnc get_and_set_rel::                # 交换（release）
    fnc get_and_set_ord::                # 交换（seq_cst）

    # 读改写（返回新值）：inc(加)/and/or/xor
    fnc inc::                            # v.inc(n) → sc_inc(&v, n)  加并返回新值（relaxed）
    fnc inc_dbl::
    fnc inc_acq::
    fnc inc_rel::
    fnc inc_ord::
    fnc and::                            # v.and(m) → sc_and(&v, m)  按位与并返回新值（relaxed）
    fnc and_dbl::
    fnc and_acq::
    fnc and_rel::
    fnc and_ord::
    fnc or::                             # v.or(m)  → sc_or(&v, m)   按位或并返回新值（relaxed）
    fnc or_dbl::
    fnc or_acq::
    fnc or_rel::
    fnc or_ord::
    fnc xor::                            # v.xor(m) → sc_xor(&v, m)  按位异或并返回新值（relaxed）
    fnc xor_dbl::
    fnc xor_acq::
    fnc xor_rel::
    fnc xor_ord::

    # 读改写（返回旧值）：get_and_inc/and/or/xor
    fnc get_and_inc::                    # v.get_and_inc(n) → sc_get_and_inc(&v, n)  加并返回旧值（relaxed）
    fnc get_and_inc_dbl::
    fnc get_and_inc_acq::
    fnc get_and_inc_rel::
    fnc get_and_inc_ord::
    fnc get_and_and::                    # 按位与并返回旧值（relaxed）
    fnc get_and_and_dbl::
    fnc get_and_and_acq::
    fnc get_and_and_rel::
    fnc get_and_and_ord::
    fnc get_and_or::                     # 按位或并返回旧值（relaxed）
    fnc get_and_or_dbl::
    fnc get_and_or_acq::
    fnc get_and_or_rel::
    fnc get_and_or_ord::
    fnc get_and_xor::                    # 按位异或并返回旧值（relaxed）
    fnc get_and_xor_dbl::
    fnc get_and_xor_acq::
    fnc get_and_xor_rel::
    fnc get_and_xor_ord::

    # 比较并交换（CAS，返回 bool）：test_and_set(&expect, newval)
    # 成功序后缀同上；_or_acq 系列额外指定「失败时的 acquire 读序」。
    fnc test_and_set::                   # v.test_and_set(&e, x) → sc_test_and_set(&v, &e, x)  CAS（成功 relaxed / 失败 relaxed）
    fnc test_and_set_acq::               # 成功 acquire
    fnc test_and_set_rel::               # 成功 release
    fnc test_and_set_dbl::               # 成功 acq_rel
    fnc test_and_set_ord::               # 成功 seq_cst
    fnc test_and_set_or_acq::            # 成功 relaxed / 失败 acquire
    fnc test_and_set_acq_or_acq::        # 成功 acquire / 失败 acquire
    fnc test_and_set_rel_or_acq::        # 成功 release / 失败 acquire
    fnc test_and_set_dbl_or_acq::        # 成功 acq_rel / 失败 acquire
    fnc test_and_set_ord_or_acq::        # 成功 seq_cst / 失败 acquire
}

# ---------------- sizeof / offsetof：编译期取值（语言关键字）----------------
# 二者为语言关键字（lexer KwSizeof / KwOffsetof），编译器直接转译为 C 同名运算，
# 结果类型 u8（size_t）；无 sc 侧声明、不依赖任何模块。
#   sizeof(表达式 | 类型名)        # 字节大小
#   offsetof(类型名, 字段名)       # 字段在结构体内的字节偏移

# ---------------- base / prev / next：节点导航内置函数（语言关键字）----------------
# 三者是编译器内置的伪函数（语言内核），不在任何 sc 模块声明、不生成调用、不参与链接；
# 编译器直接展开为指针重解释/字段访问（语义见 semantic.cpp，展开见 codegen_c.cpp）。
# 与普通函数同名时让位于用户符号（局部/全局/函数名存在即按普通调用处理）。
#   base(o)  —— 取「首个真实成员」的地址：跳过链表注入的 _prev/_next 或容器注入的 Item
#               节点 I，定位结构体 T 的首个用户字段。
#               · base(o: T)        → 按 o 静态类型 T 推导，结果 T&（值接收者自动取址）；
#               · base(o: T&)/数组   → 直接以该指针为基址重解释；
#               · base((T)o)        → 显式指定目标类型 T，把 o 首址强转为 T&（绕过推导）。
#   prev(o) / next(o) —— 链表（def T: ~ {}）逻辑前驱/后继节点，返回 void&（nil=无）：
#               · next(o) 取注入的 _next；prev(o) 为「边界安全前驱」（链头 → nil，
#                 运行时经 op.h 的 chain_prev 契约判定，见下 chain 机制）。
#   用显式 `: T&` 下转把导航结果还原回元素类型（如 t.next(it): T&）。
#

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
#     fnc alloc: S&, ...    # 分身构造器：隐式 this=T&，余参=切片参数，返回 S&
#     fnc free: s: S&       # 分身析构器
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
#   - 这里 C 为自定义容器类型，但必须具备以下必备成员函数：
#       fnc insert: ret, item: I&, tag: ...   # 插入
#       fnc remove: ret, item: I&             # 移除
#       fnc find:   ret, out: I&&, ...        # 查找（出参回填）
#       fnc first:  I&                        # 首元素
#       fnc next:   I&, item: I&              # 后继
#     可选：fnc last: I&   fnc prev: I&, item: I&  # 反向导航
# 机制：
#   - T 构成容器的元素类型（如 treeNode）；I 是容器节点类型（如 node），被注入到 T 首位，
#     因此 T& 与 I& 可零偏移互相强转（编译器在方法实参处自动 treeNode& ⟷ node&）。
#   - 导航只经容器方法：t.first() / t.next(it)，返回 I&，用显式 `: T&` 下转回元素类型。
#   - 下标糖 c[key, ...]：对容器实例 c（类型 C）执行 find，命中得元素节点 I&（用 `: T&`
#     下转回元素类型），未命中/出错（find 返回非 ok）得 nil。等价展开：
#       c[k]  <==>  { var _o: I&; (c.find(&_o, k) == ok) ? _o : nil }
#     方括号内为 find 的检索键（透传给 find 的可变参数，逗号分隔多键）。仅对容器类型 C
#     生效，普通数组/指针下标语义不变。


#  for-in 遍历语法（集合遍历糖）
# ----------------------------------------------------------------------------- 
# 统一的集合遍历语句，转 C 后即普通 for/while 循环，零额外开销；与经典三段式
# for init; cond; step 并存（按 in 关键字区分）。
# 语法：
#   for name[: T&] [, i [, j ...]] in 集合 [revert] [step <expr>] [offset <expr>] [num <expr>]
#       体...
# 集合分三类：
#   ① 值序列：范围 [a, b]（闭区间，含 b）/ [a, b)（半开，不含 b）；整数 n（== [0, n)）
#   ② 索引序列：静态数组（编译期已知长度，支持多维）；字符串（char& / char[]，按 '\0' 终止）
#   ③ 链式序列：双向链 chain；ADT 容器（def T: <C, I>），经 first/next 游标遍历
# 循环变量类型：
#   - 默认按集合元素类型推断：范围/整数→i4；数组→元素类型；字符串→char；
#     链→void&（须显式下转）；容器→注入节点 I&（须显式 : T& 下转回元素）。
#   - 可显式注解 name: T& 覆盖（链/容器常用：把游标下转回元素指针）。
# 索引/坐标变量（name 之后逗号列出 i, j, ...，命名须互不相同且不同于 name）：
#   - 数量须与集合维度一致：一维集合 0 或 1 个；N 维静态数组恰好 N 个（生成 N 层嵌套
#     循环遍历全部标量，name 为最内层标量元素，i/j/... 为各维下标）。
#   - 取值：可索引集合（静态数组/标量/范围，可得 count）→ 真实下标，revert 时下标随元素
#     倒序，name == 集合[i] 恒等；仅 next 迭代的集合（字符串/链/容器）→ 0,1,2... 递增计数
#     （与 revert/offset 无关）。
# 尾随选项（任意顺序，各至多一次；多维数组仅支持 revert）：
#   revert        逆序遍历（范围/数组从尾向头；链/容器改用 last/prev，容器须实现 last/prev）
#   step <expr>   每次前进 expr 步（默认 1）
#   offset <expr> 起点跳过 expr 个元素（默认 0）
#   num <expr>    最多遍历 expr 个元素（默认无上限）
# 等价展开示例：
#   for i in [a, b]        <==>  for var i = a; i <= b; i++
#   for i in n             <==>  for var i = 0; i < n; i++
#   for x, i in arr        <==>  for var i = 0; i < len(arr); i++ { var x = arr[i]; ... }
#   for v, i, j in mat     <==>  for i.. { for j.. { var v = mat[i][j]; ... } }
#   for c in s             <==>  for var _i = 0; s[_i] != '\0'; _i++ { var c = s[_i]; ... }
#   for it: T& in l        <==>  for var p = l.first(); p != nil; p = next(p) { var it = p: T&; ... }


#  run / thread 机制（抢占式并发内核）
# ----------------------------------------------------------------------------- 
# run 语句创建独立线程执行一个 rpc 调用（语言特性，目标必须是 rpc 调用）。
# thread 是其线程实体类型，属语言内核（默认导入，无需 inc）：run 依赖它，
# 故与 future/async 一样下沉到 op.sc。
# 语法（第二参数决定执行形态，按类型静态分派）：
#   run work(a, b)        # detach：独立线程，结束后自释放
#   run work(a, b), &t    # joinable：t: thread&，须 t->join() 等待并回收
#   run work(a, b), p     # 入池：p 为 pool（需 inc m.sc），任务排队执行
# 机制：run 单次分配 sizeof(thread) + sizeof(rpc参数) + 实现私有区，
#   rpc 参数紧随 thread 对象之后（p + sizeof(thread) 即参数），线程实体与参数
#   同生命周期；语法层面能拿到的 thread 必为 joinable。
# 分工：thread 类型是语言内核机制，声明在此（默认导入）；C 结构体/原型见 op.h
#   （默认带入每个 C 单元），运行时（thread_run/thread_join，跨平台 pthread↔Win32）
#   见 builtins/op_impl.c（始终随工程编译链接）。pool 执行目标属多线程模块
#   （inc m.sc）。

# ---------------- thread：线程（run 创建，不可手工构造） ----------------
@def thread: {
    id: u8         # 跨平台统一线程 id（线程启动后由其自身填写）
    h: &           # 实现私有区指针（同块分配，调用方不直接访问）

    fnc join::          # 等待结束并回收（含 thread 对象本身，之后指针失效）
}


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
#       C 结构体/原型见 op.h（默认带入每个 C 单元）。运行时实现亦属语言自有异步
#       内核——见 builtins/op_impl.c（始终随工程编译链接；多路复用后端按平台选
#       epoll(Linux)/kqueue(macOS·BSD)/IOCP(Windows)/poll(其它 POSIX 兜底)，均 O(1) 就绪
#       通知 + 自管道唤醒 + pthread，不依赖 libuv；可选 SCC_WITH_UV 构建开关换成 libuv
#       后端）。delay 等叶子原语属"异步功能库"，其声明保留在 async
#       模块（inc async.sc），实现同在 op_impl.c。

# ---------------- future：异步结果句柄（类型擦除） ----------------
# 字段为 C ABI 布局占位（实际定义在 op.h；sc 侧仅按方法访问，不直接读字段）。
@def future: {
    _ready: i4          # 0=未就绪, 1=已就绪
    _result: &          # 类型擦除结果
    _frame: &           # 等待者状态机帧（await 点登记）
    _resume: &          # 等待者恢复入口
    _next: &            # 就绪队列链接（运行时内部）
    _id: i4             # future<ID> 事件 id（>=0=可派发；-1=无标签）
    _ctx: &             # future<ID>(ctx) 用户上下文：发起时挂、派发时 f->ctx() 取回

    fnc init::          # future()：伪类构造，登记到当前事件循环（未就绪）
    fnc ready:: bool    # f.ready()：是否已就绪
    fnc get:: &         # f.get()：取结果（调用点用 : T& 强转还原类型）
    fnc ctx:: &         # f.ctx()：取发起时挂载的用户上下文（无则 nil）
}

# ---------------- 事件循环生命周期 ----------------
@fnc async_init::           # 建立当前线程事件循环
@fnc async_loop:: proc: &   # 驱动事件循环至全部异步完成；proc=按 id 派发回调
                            #   fnc async_proc: i4, id: future_id, f: future&（返回<0 停循环），
                            #   nil=纯协程驱动（无 future<ID> 派发）
@fnc async_final::          # 销毁当前线程事件循环

# com 通讯机制
# ----------------------------------------------------------------------------- 
# 语言层面提供设备通讯的基础能力
# 语法：
# << 操作符: send 字节流到 COM（发）
# >> 操作符: 从 COM recv 字节流（收）
# 链式左结合：com << a << b、com >> x >> y，最左操作数须为 com 基类型，逐个执行。
#
# ============================ 实际操作完整流程 ============================
# 编译器按「所在函数是否异步」与「右操作数形态」选择展开。右操作数有三种形态：
#   ① 普通变量 v        —— 直接收发 sizeof(v) 字节
#   ② com[...] 句柄 s   —— 有界读视图，走框架确定读流程（仅 >>）
#   ③ rpc 调用/名      —— 把 rpc 参数序列化收发（同步形态：见【C】）
# 同步（fnc 内）= 【A】【B】【C】；异步（rpc 内含 await）= 【D】【E】【F】。
# base 表示 com 端点（值接收者自动取址 &com，指针接收者直接传 com）。
#
# 【A】同步 · 普通变量（fnc 内，目标为 lvalue v）—— codegen emitComChain
#   com << v   展开为：  _scsz = sizeof(v); base.write(&base, &v, &_scsz);
#   com >> v   展开为：  _scsz = sizeof(v); base.read (&base, &v, &_scsz);
#   · 直接调用 com 的每对象方法指针 read/write，一次收发 sizeof(v) 字节；
#   · size 为 in/out：传入期望字节数，回写实际收发字节数；
#   · 返回码（见下 io 枚举）由调用方按需检查，框架不介入。
#
# 【B】同步 · com[...] 句柄（fnc 内，目标 s 为分身/切片句柄）—— emitComChain
#   com >> s   展开为：  limit_read(&base, s._);
#   · s 由 `var s: com[size, ending]` + `s = com` 构造（见分身机制：调 com.alloc，
#     把 size/ending 透传写入 limit，回写 _self 回指本体）；
#   · limit_read 是「框架确定读流程」（op_impl.c，纯逻辑、不依赖任何设备/库）：
#       base = s.data()                     # 用户实现：缓冲基址
#       循环:
#         chunk = ending?size:(size-len)    # 动态:每次最多 size；定长:剩余空间
#         r = com.read(&com, base+len, &chunk)  # 设备读到 base+len，回写实读 chunk
#         若 r<0 → 中断返回；  若 r==again → 返回 again（同步上下文应视为错误）
#         len += chunk
#         若 ending: m = ending(s); 若 m>=0 → len=m, 返回 eof（命中截止）
#         否则定长: 若 len>=size → 返回 eof（读满）
#         若 r==eof → 返回 eof（设备无更多数据）
#   · 截止策略全在用户的 data()/ending 里，框架只跑这套不变的循环（最小内核）；
#   · 句柄是「有界读视图」，仅用于 >> 读流程；com << s（句柄写）不支持，直接编译报错。
#
# 【C】同步 · rpc 序列化收发（fnc 内）—— emitComRpcSend / emitComRpcRecv
#   把 rpc 的「参数」当作一组待收发的值，按声明顺序逐字段过 com（跳过返回槽 _）。
#   发收两端形态对称、字段顺序一致，构成一对「序列化协议」。
#
#   ·发· com << rpc(实参...)   目标为 rpc 调用（带括号实参）
#     展开为：填 rpc 参数结构体 _rp（与本地调用同样装填）→ 逐字段 write：
#       标量/指针 f：  _scsz = sizeof(_rp.f);     base.write(&base, &_rp.f, &_scsz);
#       数组     f：  _scsz = _rp.f_size;         base.write(&base, _rp.f,  &_scsz);
#     · 仅写参数字节，不触发本地 rpc 调用（纯发送方）；
#     · com[...] 句柄参数不可序列化 → 编译报错（须由裸字节流另行承载）。
#
#   ·收· com >> rpc            目标为裸 rpc 名（无实参）
#     展开为：逐字段从 com read 进参数结构体 _rp → 触发 worker rpc_rpc(&_rp)：
#       标量/指针 f：  _scsz = sizeof(_rp.f);     base.read(&base, &_rp.f, &_scsz);
#       数组     f：  栈上开等长后备缓冲 _rp_f，_rp.f 指向它，再 read 进缓冲；
#       句柄     f：  com[...] 参数以「本 com」为本体绑定句柄（alloc + _self 回指），
#                     走【B】的框架读流程 limit_read 从同一 com 读入（其余字段照常）；
#     · 读毕调用 rpc_rpc(&_rp) 触发本地处理；不取返回值（仅触发，无回执）。
#   · 约束：异步 rpc、可变参数 rpc 暂不支持 → 编译报错。
#
# 【D】异步 · 普通变量（rpc 内，含 await 机制）—— codegen emitComAwait
#   rpc 内出现 << / >> 即标记该 rpc 为异步（hasAwait），编译为状态机。每个收发点
#   是一个 await 切点，展开为：
#     com >> v   →  _p->_fut = com_read_async (&base, &v, sizeof(v));
#                   if (future_await(_p->_fut, _p, rpc_resume)) goto _s<k>;  # 已就绪→续跑
#                   _p->_state = <k>; return;                               # 未就绪→让出
#                   _s<k>: ;                                                # 恢复点
#     com << v   →  对称，调 com_write_async。
#   · io 直接填充/读取 v 本身（结果即数据），await 的 future 兑现仅作"完成信号"，
#     无需把字节回写 future；
#   · 让出后由事件循环在 io 完成时经就绪队列 resume 状态机，从 _s<k> 继续。
#
# 【E】异步 · com[...] 句柄（rpc 内）—— codegen emitComAwait + 运行时 com_limit_read_async
#   语义：limit_read 的框架读循环若遇 com.read 返回 again，则挂起 await、待设备
#   就绪后恢复续读，直至 ending/定长命中。展开为单个 await 切点：
#     com >> s   →  _p->_fut = com_limit_read_async(&base, s._);
#                   if (future_await(_p->_fut, _p, rpc_resume)) goto _s<k>;
#                   _p->_state = <k>; return;  _s<k>: ;
#   · 句柄仍是「有界读视图」，仅用于 >>；future 兑现值为 limit.len（<0 为错码）。
#   · 运行时 com_limit_read_async（op_impl.c，两后端均实现）：把该有界读登记为
#     事件循环活动请求（io_req.lim≠nil 标记限读模式），每次就绪驱动一轮 limit_read，
#     遇 again 重新挂回等待、直至读满或 ending 命中再 future_done 兑现。
#
# 【F】异步 · rpc 序列化收发（rpc 内）—— codegen emitComRpcSendAsync / emitComRpcRecvAsync
#   语义：把【C】的逐字段 write/read 替换为 com_write_async/com_read_async 的 await
#   切点（每字段一个让出点），收端读齐参数后再触发处理。
#   ·发· com << rpc(实参...)  →  堆上分配参数帧 _crpc<n> 装填实参（不让出）→ 逐字段
#                                com_write_async await 写出 → 释放帧。
#   ·收· com >> rpc           →  堆上分配 _crpc<n> + 数组后备/句柄上下文（不让出）→ 逐
#                                字段 com_read_async await 读入（句柄字段走【E】的
#                                com_limit_read_async）→ 读齐后触发 worker rpc_rpc → 释放。
#   · 帧字段以堆指针 struct R *_crpc<n> 注入 rpc 状态机（避免结构体定序问题）；
#   · 约束同【C】：可变参数 rpc、参数含 await 暂不支持 → 编译报错。
#
# ---------------------- com_read_async / com_write_async ----------------------
# 二者是【D】异步收发点的「桥接原语」（C ABI 见 op.h，实现在语言自有异步内核
# builtins/op_impl.c，始终链接）。目的：把一次 com 设备 io 封装成可被 await
# 的 future，使 rpc 状态机能以统一的 await 协议挂起/恢复。
#   com_read_async (&com, data, size) → future&   # 发起异步读，io 完成时 future_done 兑现
#   com_write_async(&com, buf,  size) → future&   # 发起异步写，对称
#   · 编译器只生成对它们的调用 + future_await 握手，不关心其内部如何驱动 io；
#   · 当前实现：把一次 com io 登记为事件循环的活动请求（io_req）并唤醒循环，随即
#     返回；循环以 readable/writable 探测该 com 的就绪能力：
#       - 给出可监听句柄(*id≠nil) → 常驻注册进多路复用后端(epoll/kqueue/IOCP/poll)，
#                              由其 O(1) 就绪通知唤醒；
#       - 无句柄(*id=nil) 或未提供 readable/writable → 按返回值轮询(1=就绪/0=again/<0=错)；
#     就绪后调一次 com.read/write 实行收发，再 future_done 兑现（不阻塞循环线程）；
#   · 请求现走内部活动表 io_req（非 com.rq/wq）；ioq 循环缓冲作为更重的批量 io 载体
#     仍可在其上演进，接口契约不变；
#   · 二者依赖 future_*，故与 future/await 同约束：未 inc async 用到即链接缺失。
# =========================================================================
# 示例：
# rpc do_some: i4, a: i4, b: i4         # 序列化收发的一对 rpc（参数即协议字段）
#     return 0
# rpc on_some: i4, a: i4, b: i4
#     return 0
# fnc call: com&                        # 同步：【A】普通值 + 【C】rpc 序列化
#     com << val1 << val2               # 【A】发两个值
#     com << do_some(a, b)              # 【C】发 do_some 的参数（不触发本地调用）
#     com >> res                        # 【A】收一个值
#     com >> on_some                    # 【C】收参数并触发 on_some_rpc
#     return

# fnc init::                          # 初始化 limit（如填充边界数据）                                
# —— com/limit/ioq 定稿声明（默认导入；C ABI 见 op.h，通用运行时见 op_impl.c/未来）——
# 分层（Q4）：声明在此默认导入；通用运行时（ioq 循环缓冲、com 收发框架）→ op_impl.c（不依赖 libuv）；
#           具体设备 io（read/write 实现、异步驱动）→ 可选模块（inc com.sc）。

# ---------------- ioq：com 读写缓存队列（异步 io 的载体）----------------
# 本质是可自动膨胀的循环缓冲队列。item 为连续一组值，依其首值判类型：
#   [size, buf]             size≠0 → io 缓冲，pull 执行 io 操作
#   [0,    callback, data]  size=0 → io 完成回调地址
# com 是否提供 rq/wq（非 nil）决定其是否支持异步 io。
@def ioq: {
    com: com&                           # 所属 com（前向引用，指针）
    fnc push:: buf: &, size: i4         # 入队一段 io 缓冲
    fnc notify:: cb: &, data: &         # 入队一个 io 完成回调（cb 为函数地址）
    fnc pull:: &                        # 取队首并执行 io（空则阻塞等待，区别于 pop）
}

# ---------------- io：read/write 返回码规范 ----------------
# 框架读写流程据此驱动 >> / <<：
#   <0      —— 不可恢复错误，中断（停止继续读写）
#    0      —— 成功，可继续读写
#    again  —— 异步挂起等待（仅 rpc 内合法；同步 fnc 内遇到 → 报错）
#    eof    —— 数据全部读完，此后续读目标按空（零字节）完成
def io: [
    again = 1
    eof   = 2
] : i4

# ---------------- limit：com 一次有界读视图（com 的分身/切片）----------------
# 最小内核：框架只驱动确定的读流程，所有缓存/边界策略都在用户实现里。limit 仅暴露
# 两个每对象方法接口 + 两个属性字段：
#   size    —— ending 为 nil：固定数据大小；否则每次最多读取的 chunk（读满 size 触发 ending）
#   len     —— 框架回写：已累计读取的字节计数
#   data()  —— MethodPtr（用户实现）：返回缓冲基址；框架每次以 data()+len 为写入起点
#   ending  —— 每对象方法指针 MethodPtr（用户实现，默认 nil）：动态截止判定。框架每读一段
#              后以本 limit 为接收者回调（实现里 _this: limit&，经 self.data()/self.len 自取
#              基址与已读长度）；返回 >=0 命中（值=应保留的数据长度，读循环停止）/
#              <0 未到结尾（继续读）。
#              例：HTTP 以 \r\n 截止，由用户在 ending 里扫描判定；框架不写死边界规格。
@def limit: {
    size: u4                            # ending=nil:定长大小；否则每次最大 chunk
    len:  u4                            # 框架回写：累计读取计数
    fnc data: &                         # 每对象 MethodPtr：返回缓冲基址（用户实现）
    fnc ending: ret                     # 每对象 MethodPtr：动态截止（用户实现，默认 nil）
}

# ---------------- com：设备通讯端点（机制框架）----------------
# 具体 io 依赖设备，由 read/write/error「每对象方法指针」（MethodPtr，fnc 前置、无函数体）实现
# 关键语法糖 >> <<（类似 c++）：见 op.sc 顶部 com 机制说明与后续阶段实现。
#
# ===== 多路复用就绪查询：readable / writable（设备能力，框架据此驱动 io 就绪）=====
# 两种设备能力（可被 epoll/kqueue/IOCP/poll 监听 vs. 只能轮询）统一为一个接口：
#   fnc readable: ret, id: &&     # 查询读就绪；id 出参=设备 id/句柄的返回地址
#   fnc writable: ret, id: &&     # 查询写就绪（语义对称）
# 语义（以 readable 为例）：
#   · 出参 id：用户回填本设备可交给多路复用后端（epoll/kqueue/IOCP/poll）监听的 id/句柄。
#     - *id 非 nil（fd/句柄）→ 设备「支持多路复用」：框架把 *id 注册进多路复用后端，
#                            由 epoll/kqueue/IOCP/poll 统一等待就绪（O(1) 通知）；此时 readable 返回值忽略。
#     - *id 为 nil          → 设备「不支持多路复用」：框架转而直接看 readable 返回值判就绪：
#         <0   出错（中断该 io）
#          0   pending/again（未就绪，框架稍后轮询重试）
#          1   io ready（可立即读/写）
#   · 两条路径都收敛到「设备就绪 → 框架 pull ioq → 执行 read/write → future_done 兑现」。
@def com: <limit> {
    dev: &                              # 设备句柄（设备相关）
    rq: ioq&                            # 读队列（nil=不支持异步读）
    wq: ioq&                            # 写队列（nil=不支持异步写）

    # alloc 参数即 com[...] 句柄上下文：var s: com[size, ending]。
    # size/ending 透传构造 limit；缓冲分配等策略由用户 alloc 实现自定。
    fnc alloc: limit&, size: u4, ending: &   # 分身构造：隐藏接收者 com&，返回 limit&
    fnc free: s: limit&                      # 分身析构

    fnc read: ret, data: &, size: u4&   # 设备读（每对象 io 实现，隐藏接收者 com&）
    fnc write: ret, buf: &, size: u4&   # 设备写（每对象 io 实现）
    fnc error: ret                      # 错误回调（每对象）
    fnc readable: ret, id: &&           # 读就绪查询（多路复用探测，见上契约）
    fnc writable: ret, id: &&           # 写就绪查询（语义对称）
    fnc close: ret                      # 关闭设备：释放底层资源（nil=无需关闭，OS 回收）
}

# ---------------- async_io：com 设备 io 的就绪事件循环 ----------------
# 与 async_loop（future<ID> 消息派发 / 协程 resume）正交：async_io 专司「设备 io 就绪」
# 这一层，不关心业务消息，只把「设备何时可读/可写」翻译成对 ioq 的 pull + io + 兑现。
# 驱动流程（每个登记了待办 io 的 com）：
#   1. 取该 com 待办方向（rq 非空→读 / wq 非空→写）；
#   2. 调 readable/writable(id) 探测就绪：
#        - 给出可监听句柄(*id≠nil) → 注册进多路复用后端(epoll/kqueue/IOCP/poll)，等其就绪；
#        - 否则按返回值：1=就绪、0=again（继续轮询）、<0=出错（结束该 io）；
#   3. 就绪后 ioq.pull 取队首 io 缓冲，调 com.read/write 执行实际收发；
#   4. 收发完成 → future_done 兑现对应 future（接回 await 状态机 / 派发）；
#   5. 队列排空且无登记 com → 退出循环。
# 分层：多路复用后端属语言自有异步内核，由 op_impl.c 按平台选 epoll(Linux)/
#       kqueue(macOS·BSD)/IOCP(Windows)/poll(其它 POSIX 兜底) 提供，均 O(1) 就绪通知；
#       就绪→执行 io→兑现这套「机制」语言自有（op_impl.c）。
@fnc async_io::             # 驱动 com 设备 io 就绪循环至全部待办 io 完成

# ---------------- print：日志输出（语言关键字） ----------------
# print 是语言关键字，属语言内核（op.sc 默认导入、op.h 默认带入每个 C 单元、
# op_impl.c 始终链接）——无需 inc。编译器按实参静态类型拼接/补格式后生成
# 对本接口的调用（首参 chn 为 u1 日志通道，其后为 C printf 风格格式串与可变参数）：
#   print "x = ", x             # 无括号=拼接糖：字符串字面量=纯文本，
#                               # 变量按静态类型自动补 printf 说明符（i4→%d, char&→%s ...）
#   print("x = %d", x)          # 有括号=C printf 兼容模式：首参格式串，实参原样传递
#   print<3> "通道 3 的日志"     # <chn> 指定 u1 日志通道（默认 0）
#   print<s> "x = ", x          # <chn> 为 string 变量 → 不输出 stdout，改「追加进该串」
#                               # （等价 s.printf(...)：无时间戳/级别/通道修饰，纯格式化文本）
#   print "E: open ", p, " 失败" # fmt 文本前缀 "X:" 指定级别，X ∈ F/E/W/I/D/V
#   - 输出格式：HH:MM:SS.mmm L| 文本（chn!=0 时加通道标记；自动补换行）
#   - 级别过滤：环境变量 SC_LOG=F/E/W/I/D/V（默认 D；高于该级别的输出被丢弃）
# C ABI 见 op.h（默认带入），运行时见 op_impl.c（始终链接）。
@fnc print:: chn: u1, fmt: char&, ...

# ---------------- stringify(...)：JSON 字符串格式化（语言关键字）----------------
# 语言底层机制（默认导入，无需 inc）：编译器按实参静态类型生成格式化器（写入独立
# stringify.h 由生成的 .c include），区别于类型 string 与堆构造 string()。选项类型
# stringify_t 的 C ABI 见 op.h；格式化器返回堆专属 string&，故仍依赖 inc adt.sc。
#   var s: string& = stringify(x)       # 返回堆构造 string&（JSON 文本），用完需 s->drop()
#   var b[256]: char
#   var p: char& = stringify(x, b, 256) # 在给定缓存内构建（截断保证 NUL 结尾），
#                                       # 返回 char&（即缓存首址，无需 drop）
#   选项块 stringify<key:val, ...>(...)：以 (stringify_t){...} 传入格式化器
#     · stringify<compact:1>(x)         # 紧凑单行 {"x":3,"y":4}
#     · 默认（无选项 / compact:0）       # 多行美化（2 空格逐层缩进）
#   - 标量 → 数字；bool → true/false；char → 'a'
#   - char& / char 数组 → "文本"；adt string → "内容"
#   - 结构体/联合体 → {"字段": 值, ...} JSON 对象（链表 _prev/_next 不展开）
#     · 子成员为结构体（值）→ 递归展开
#     · 成员为结构体指针 → "类型名@0x地址"（不深递归）
#     · 成员为标量指针 → "&值"（nil → nil）
#     · 其它指针（void&/多级）→ "0x地址"（nil → nil）
#   - 一维数组 → [v, v, ...]
#   - 结构体一级指针（顶层实参）→ 解引用展开内容（nil → "nil"）
#   依赖 adt string（inc adt.sc），暂不支持多维数组

