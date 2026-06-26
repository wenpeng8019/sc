# feature45：rpc 延迟应答（session）—— 裸 async 取会话，转延迟应答，将来 done 兑现。
#
# 与 future（fnc 单线程异步：async→future→done）对称的「rpc 延迟应答」：sync 驱动的 rpc 体
# 内裸 `async` 取出当前调用会话（求值为 session&），本次 sync 不再由 rpc 体 return 即时应答；
# 把会话存起来，待条件成熟后 `done s, result` 兑现——写回调用方返回槽 + 唤醒其阻塞。
# 关键区别：done 发生在 rpc 体返回「之后」（甚至另一线程），调用方一直阻塞到 done 那刻。

inc mt.sc

var g_sess: session& = nil      # 被延迟的会话（rpc 体里领走，待将来兑现）
var g_arg:  i4 = 0             # 连带存下的请求参数

# rpc 体：取会话转延迟应答，return 值被忽略（真正的结果由将来的 done 给出）。
rpc serve: i4, x: i4
    var s: session& = async    # 取当前会话：本次 sync 改为延迟应答
    g_sess = s
    g_arg  = x
    return 0                   # 立即返回，但调用方仍阻塞，等将来 done

# 服务消费线程：pull 进入 serve()（会话被领走），随后在 rpc 体「之外」才兑现 —— 这就是延迟。
rpc server: qq: queue&
    qq->pull(-1)               # 进入 serve()：会话被领走，转延迟应答（调用方仍阻塞）
    done g_sess, g_arg * 10    # 延迟应答：现在才写回最初调用方的返回槽并唤醒（跨线程）

fnc main: i4
    var sq: queue& = default_queue(nil)
    var st: thread& = nil
    run server(sq), &st               # 服务线程：成为 sq 消费者，处理 serve

    var r: i4 = sync serve(7), sq     # 阻塞至延迟应答兑现：期望 70
    printf("delayed response: r=%d\n", r)

    st->join()
    sq->drop()
    return 0
