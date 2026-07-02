# ════════════════════════════════════════════════════════════════════════════
#  特性 20：future<ID> 事件派发机制（语言自有的消息泵 / reactor）
# ════════════════════════════════════════════════════════════════════════════
# 背景：libuv 只是一个「异步功能库」，不是机制本身。await 协程驱动与本特性的
#   id 派发驱动都由语言层面自有实现（async_impl.c），libuv 仅占「默认 poll 实现」一格。
#
# 四件套：
#   ① future<ID>(ctx?)     给 future 打编译期事件 id，并可挂一份「发起时上下文」ctx。
#                          编译器收集所有 ID，验证聚合成 future_id 枚举；
#                          --emit-c 转译工程时写出 type.h。
#   ② done future, result  生产者在「完成」时兑现 future（可来自任意线程/中断/外部库），
#                          带 id 的 future 即一条「消息」，入派发队列。
#   ③ async_loop(async_proc)
#                          主线程 looper 通用派发：逐条取出已完成的带 id future，
#                          交给 async_proc(id, fut) 处理，像消息队列。返回<0 停循环。
#   ④ f->ctx()             派发器取回发起时挂载的上下文（等价 libuv 的 req->data）：
#                          「这次操作对应哪条连接/会话」的归属信息。
#
# 上下文从哪来、到哪去：
#   · 设置点 = 发起时    future<ID>(&s)  把上下文指针 s 写进 future.ctx
#   · 获取点 = 派发时    f->ctx()        在 async_proc 里取回，定位归属对象
#   而 done 的 result 是「完成载荷」（完成那一刻才有的结果），两者分工不同。
#
# 与 await 协程并存：同一循环里，有协程等待者的 future 走 resume（特性 13/17），
#   无等待者但带 id 的 future 走 async_proc 派发。两条路径都语言自有。
# 注：事件 id 与 libc 符号（如 close/connect）同名会在 C 层冲突，故用 conn/data/term。
# ════════════════════════════════════════════════════════════════════════════

inc async.sc                 # 触发链接 libuv 运行时（future/async_* 默认实现）

# 会话上下文：发起异步操作时确定（哪条连接），派发完成时据此归属
# 注：类型名避开 op.h 内置 `session`（rpc 延迟应答会话），故用 sess。
def sess: {
    name: char&              # 连接/会话名
    seq:  i4                 # 序号
}

# ── 通用 id 派发器（消息泵处理器）─────────────────────────────────────────────
# 返回类型在前（i4）；id 为编译器聚合的 future_id 枚举；f 为已完成的 future。
# 返回 <0 请求 looper 停循环（如收到终止事件 term）。
fnc async_proc: i4, id: future_id, f: future&
    var v: i4 = f->get(): i4               # 完成载荷（done 时写入，: i4 还原擦除标量）
    var s: sess& = f->ctx(): sess&   # 发起时上下文（future<ID>(&s) 挂载）
    case id:
        conn:
            ::printf("派发 conn[%s#%d]: v=%d\n", s->name, s->seq, v)
        data:
            ::printf("派发 data[%s#%d]: v=%d\n", s->name, s->seq, v)
        term:
            ::printf("派发 term[%s#%d]: v=%d（终止事件，停循环）\n", s->name, s->seq, v)
            return -1                       # <0：请求停循环
    return 0

fnc main: i4
    async_init()                            # 建立当前线程事件循环

    # 两条会话作为上下文宿主（发起时确定归属）
    var s1: sess = {"alpha", 1}
    var s2: sess = {"beta", 2}

    # 生产者：造带 id+ctx 的 future（消息）并兑现。done 可发生在任意线程；此处同步
    # 兑现仅为演示确定的 FIFO 派发顺序。三种 id 复用 data（聚合后枚举仅一项 data）。
    var c: future& = future<conn>(&s1)
    done c, 7
    var d1: future& = future<data>(&s2)
    done d1, 42
    var d2: future& = future<data>(&s1)
    done d2, 43
    var x: future& = future<term>(&s2)
    done x, 0

    # looper：通用派发，逐条取完成的带 id future，按 id 交给 async_proc 处理
    async_loop(async_proc)

    async_final()                           # 销毁事件循环
    return 0
