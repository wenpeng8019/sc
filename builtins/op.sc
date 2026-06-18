
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
# 【E】异步 · com[...] 句柄（rpc 内）—— 规划中
#   语义：limit_read 的循环若遇 com.read 返回 again，则挂起 await、待设备就绪
#   （ioq 读队列回调兑现 future）后恢复续读，直至 ending/定长命中。当前未实现
#   （同步 limit_read 遇 again 即报错信号），留作 ioq 异步驱动落地时打通。
#
# 【F】异步 · rpc 序列化收发（rpc 内）—— 规划中
#   语义：把【C】的逐字段 write/read 替换为 com_write_async/com_read_async 的 await
#   切点（每字段一个让出点），收端读齐参数后再触发处理。当前未实现，待异步收发
#   原语真正落地（非"立即完成"桥）后与【D】统一展开。
#
# ---------------------- com_read_async / com_write_async ----------------------
# 二者是【D】异步收发点的「桥接原语」（C ABI 见 op.h，默认实现在 builtins/async/
# async_impl.c，仅 inc async 时链接）。目的：把一次 com 设备 io 封装成可被 await
# 的 future，使 rpc 状态机能以统一的 await 协议挂起/恢复。
#   com_read_async (&com, data, size) → future&   # 发起异步读，io 完成时 future_done 兑现
#   com_write_async(&com, buf,  size) → future&   # 发起异步写，对称
#   · 编译器只生成对它们的调用 + future_await 握手，不关心其内部如何驱动 io；
#   · 当前默认实现是「立即完成」桥：同步调一次 com.read/write 后立刻 future_done，
#     先打通 rpc↔await 的端到端链路；
#   · 真实异步驱动应改为：把请求入 com 的 rq/wq（ioq 循环缓冲），在设备 io 完成
#     回调里 future_done 延迟兑现（不阻塞循环线程）。接口契约不变，仅换实现。
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
def io: i4
    again = 1
    eof   = 2

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
}

