# 特性 13：异步机制（async / await / future）
# > 与 run（多线程）相对的第二套并发模型：单线程协作式异步（事件循环 + 状态机）
#
# ════════════════════════════════════════════════════════════════════════════
#  设计总览
# ════════════════════════════════════════════════════════════════════════════
#
# run 把 rpc 当线程入口（抢占式、多线程）；async 把 rpc 当协程（协作式、单线程）。
# 二者共用 rpc 这一"可打包的调用单元"，但执行形态正交：
#
#   run  work(a, b)          # 新线程跑 rpc，结束自释放/可 join（特性 9）
#   async greet(a, b)        # 当前线程事件循环里挂起式跑 rpc，返回 future
#
# 三个语言关键字 + 一个内置类型：
#
#   ┌─ await E ──────────────────────────────────────────────────────────────
#   │   只能出现在 rpc 体内。E 是一个产生 future 的表达式（叶子异步原语调用，
#   │   或对另一个 rpc 的调用）。await 挂起当前 rpc，待 E 就绪后恢复，
#   │   整个 await 表达式求值为 E 的结果（类型 = 被调 rpc 的返回类型）。
#   │   可选超时：await E, timeout_ms （超时后以错误/默认值恢复）。
#   ├─ async E ──────────────────────────────────────────────────────────────
#   │   可出现在任意 fnc/rpc 体内。把 rpc 调用 E 登记进当前线程事件循环，
#   │   立即返回 future&（不阻塞）。是"发起但不等待"。
#   ├─ future ────────────────────────────────────────────────────────────────
#   │   异步结果句柄（内置类型，由 async.sc 提供）。
#   │     f.ready()          # 是否已就绪（bool）
#   │     f.get(): T&        # 取结果（须已就绪；类型擦除，用 : T& 强转）
#   └─ async_init / async_loop / async_final ────────────────────────────────
#       当前线程事件循环生命周期（async.sc 提供，libuv 实现）：
#         async_init()       # 建立当前线程事件循环
#         async_loop()       # 驱动事件循环，推进所有挂起 rpc 直到全部完成
#         async_final()      # 销毁（尚有未决任务则报错）
#
# ════════════════════════════════════════════════════════════════════════════
#  编译器机制：rpc → 状态机（stackless coroutine）
# ════════════════════════════════════════════════════════════════════════════
#
# 含 await 的 rpc 被编译为状态机：
#   1. 跨 await 存活的局部变量 + 参数 + 返回槽，统一提升到 rpc 帧结构体；
#   2. 帧追加隐藏字段：_resume(状态机入口) / _ret(本次调用的结果 future) /
#      _state(当前阶段) / _fut(当前正在 await 的 future)；
#   3. 函数体按 await 切成多段，编译为 switch(_state) + goto 标号；
#   4. 每个 await 点：发起 → 把 (_resume, 本帧) 登记为 future 的 waiter → return 让出。
#
#   你写的：                          编译器生成（简化）：
#   ┌────────────────────────┐      ┌───────────────────────────────────────┐
#   │ rpc greet:             │      │ struct greet {                        │
#   │   name:char&, ms:u4    │      │   void(*_resume)(void*); future* _ret;│
#   │   : char&              │      │   int _state; future* _fut;           │
#   │                        │      │   char* _;            // 返回槽        │
#   │   await delay(ms)      │      │   char* name; u32 ms; // 参数          │
#   │   return name          │      │ };                                    │
#   └────────────────────────┘      │ void greet_rpc(struct greet* _p){     │
#                                   │  switch(_p->_state){case 0:..1:..}    │
#                                   │ _s0: _p->_fut = delay(_p->ms);        │
#                                   │      _p->_fut->waiter = _p;           │
#                                   │      _p->_state=1; return;            │
#                                   │ _s1: _p->_ = _p->name;                │
#                                   │      complete(_p); return; } ...      │
#                                   └───────────────────────────────────────┘
#
# ════════════════════════════════════════════════════════════════════════════
#  resume / IO 完成激活：async_loop 只是"泵"，真正的恢复在回调里
# ════════════════════════════════════════════════════════════════════════════
#
# 关键澄清：async_loop 自己不推进业务，它 == libuv 的 uv_run —— 阻塞在
# kqueue/epoll 上等事件，事件到了就把控制权交给注册的 C 回调。resume 发生在
# 两个"手写 sc 里看不到"的地方（运行时 C + 编译器生成码）：
#
#   async_loop()  ==  uv_run(loop, UV_RUN_DEFAULT)
#                            │ IO/定时器完成，libuv 调用注册的 C 回调
#                            ▼
#     on_timer(t):                              # ← resume 在这里(async_impl.c)
#         future* f = t->data;                  #   句柄完成时取回绑定的 future
#         f->ready = 1; f->result = ...;
#         if (f->waiter)                        #   ★ 谁在等我
#             ((frame*)f->waiter)->_resume(f->waiter);   # ← 恢复那个 rpc 状态机
#
# 三段绑定链（resume 目标如何与 IO 绑定）：
#   ① await 点(生成码)：  _p->_fut = delay(ms);
#                        _p->_fut->waiter = _p;      # 把"恢复谁"(本帧)写进 future
#                        _p->_state = 1; return;     # 让出给事件循环
#   ② IO 完成(libuv→C)：  f->ready=1; f->waiter->_resume(f->waiter);   # 激活目标
#   ③ rpc 跑完(complete)：_ret->ready=1; _ret->result=name;
#                        if (_ret->waiter) _ret->waiter->_resume(...)  # 唤醒上游
#
# 级联 await = ②→③→② 反向冒泡：叶子完成→resume 最内 rpc→其 complete 唤醒上游
# rpc→…，每个 future 只认识"我的 waiter"(本帧)，无需全局拓扑。
#   （注：用了 libuv 就不需要手写就绪队列——libuv 回调天然串行，回调里直接
#    resume 即可；ready_q 只是无 libuv 时自写事件循环的等价物。）
#
# ════════════════════════════════════════════════════════════════════════════
#  可 await 契约：await 只认 future，libuv 只是默认实现（可自定义/可替换）
# ════════════════════════════════════════════════════════════════════════════
#
# await 的生成码里没有任何 libuv 痕迹——它的全部依赖只有一条：
#   « 操作对象必须求值为 future& »。delay 不是关键字、不是编译器内置，
#   它和 print/thread_run 一样只是 async.sc 自带的一个普通叶子原语。
#
# 任何人都能写自己的可 await 原语，靠的是 async.sc 暴露的两个【公共契约原语】：
#   future_new()            造一个未就绪 future（挂到当前事件循环）
#   future_done(f, result)  标记就绪 + 唤醒 waiter —— 谁都能调，这就是“完成”的唯一入口
#       └─ 内部即： f->ready=1; f->result=result;
#                   if (f->waiter) ((frame*)f->waiter)->_resume(f->waiter);
#
# 于是分层清晰（libuv 仅占“默认实现”一格，可整层替换）：
#   await（语言关键字，只认 future 契约）
#     └── future 契约：ready/result/waiter + future_new/future_done   ← 通用入口/规范
#           ├── libuv 默认实现：delay、(未来) socket/file/COM     ← 发起 libuv 请求，
#           │                                                       回调里调 future_done
#           └── 用户自定义实现：任何在“完成时”调 future_done 的原语
#                 （线程池回调 / 硬件中断 / 手动触发 / 同步立即就绪 …均可）
#
# 关键细节：future 可能在 await 登记 waiter 之前就已就绪（如同步立即完成的自定义
# 原语）。故 await 生成码先判 ready，已就绪则不让出、直接进入下一段：
#       _p->_fut = E;
#       if (_p->_fut->ready) goto _s1;     # 已就绪：不让出，直接续跑
#       _p->_fut->waiter = _p;             # 未就绪：登记本帧后让出
#       _p->_state = 1; return;
# ════════════════════════════════════════════════════════════════════════════

inc stdio.h
inc m.sc                      # 多线程（特性 9 的 run）——自定义叶子原语 bg_square 用
inc async.sc                 # 异步功能库（delay）+ 触发链接 libuv 运行时（future/async_* 实现）

# ── 异步底层机制（op.sc 默认导入，op.h 默认带入；无需 inc）────────────────────
#   future                       # 异步结果句柄(类型擦除，内置类型)
#       future()                 # 伪类构造：造未就绪 future（挂当前事件循环）
#       fnc ready:: bool          # 是否就绪
#       fnc get:: &               # 取结果(须已就绪；调用点用 : T& 强转还原类型)
#   async_init()                  # 建立当前线程事件循环(uv_loop_init)
#   async_loop()                  # 驱动事件循环至全部完成(uv_run)；自身不做业务推进
#   async_final()                 # 销毁事件循环(uv_loop_close)
#   async E / await E / done f[,r] # 三个语言关键字（done 等价"标记就绪 + 唤醒 waiter"）
# ── 异步功能库（inc async.sc 引入）────────────────────────────────────────────
#   # delay：libuv 默认实现的一个叶子原语（底层 uv_timer）——并非编译器特例。
#   #   真正的 IO(socket/文件/COM)都是同形状：发起 libuv 请求→回调里 done。
#   fnc delay:: future&, ms: u4   # 启动 ms 毫秒定时器，立即返回 future(不阻塞)

# ── 异步组合 rpc ① ：await 一个叶子异步原语（libuv 定时器 delay）──────────────
# 含 await ⇒ 被编译为状态机。ms 与 name 跨 await 存活 ⇒ 提升到帧。
rpc greet: char&, name: char&, ms: u4
    printf("  [%s] 睡 %u ms...\n", name, ms)
    await delay(ms)                       # 挂起 ms 毫秒（不占线程）
    printf("  [%s] 醒来\n", name)
    return name

# ── 异步组合 rpc ② ：await 另一个 rpc（级联依赖）────────────────────────────
# x, y 是跨 await 局部 ⇒ 提升到帧；两次 await 顺序依赖。
rpc both: i4, a: char&, b: char&
    var x: char& = await greet(a, 60)     # await rpc：x = greet 的返回值
    var y: char& = await greet(b, 30)     # 第二段，依赖第一段完成
    printf("  both 收集: %s + %s\n", x, y)
    return 0

# ── 自定义可 await 原语：不用 libuv，把后台线程(特性 9 的 run)桁接进事件循环 ──
# 这才是“真异步”：future_new 与 future_done 在时间上分开——发起时只造一个未就绪
# future 立即返回（不阻塞、await 会让出）；后台线程算完后才 future_done 唤醒 waiter。
# 证明 delay 不是特例、libuv 只是默认实现：任何异步源(线程/中断/外部库)
# 都能在“完成时”调 future_done 接入。
#   注：future_done 跨线程安全——libuv 默认实现内部用 uv_async_send 唤醒事件循环
#   线程，再由循环线程 resume 对应的 rpc 状态机（resume 始终在单一循环线程上）。

# 后台线程体（rpc，run 起）：耗时计算，完成时跨线程兑现 future
rpc square_worker: f: future&, n: i4
    # …（此处可放真实耗时工作）…
    done f, n * n                          # “稍后”才发生：置就绪 + 跨线程唤醒 waiter（自动 void* 擦除）

# 自定义叶子原语：造未就绪 future + 起后台线程，立即返回（不阻塞、未就绪）
fnc bg_square: future&, n: i4
    var f: future& = future()             # ① 造未就绪 future（伪类构造，挂当前事件循环）
    run square_worker(f, n)               # ② 后台线程去算（detach）
    return f                               # ③ 立即返回 future，可被 await

# ── 异步组合 rpc ③ ：await 一个“自定义”原语（真让出，与 delay 用法完全一致）──
rpc compute: i4, n: i4
    var a: i4 = await bg_square(n)          # 真异步：让出，待线程算完才恢复
    var b: i4 = await bg_square(a)          # 串联：第二段依赖第一段结果
    printf("  compute: %d -> %d\n", n, b)
    return b

fnc main: i4

    async_init()                          # ① 建立当前线程事件循环

    # ── async：并发发起两个 greet，立即返回 future（不阻塞）──────────────────
    # 两者在事件循环里交错推进：B(30ms) 先醒，A(80ms) 后醒。
    var fa: future& = async greet("A", 80)
    var fb: future& = async greet("B", 30)

    # ── async_loop：驱动事件循环，推进所有挂起 rpc 直到完成 ───────────────────
    async_loop(nil)

    # ── future.get：取结果（此时已就绪，类型擦除用 : char& 强转）──────
    printf("fa = %s\n", fa->get(): char&)  # A
    printf("fb = %s\n", fb->get(): char&)  # B

    # ── 级联：both 内部 await 两个 greet rpc ───────────────────────
    var fc: future& = async both("X", "Y")
    async_loop(nil)
    printf("both ret = %d\n", fc->get(): i4)   # 0

    # ── 自定义原语：await compute(内部 await bg_square，线程桁接、不依赖 libuv)──
    var fd: future& = async compute(3)
    async_loop(nil)
    printf("compute ret = %d\n", fd->get(): i4)  # 81  (3²=9, 9²=81)

    async_final()                         # ③ 销毁事件循环
    return 0
