# async —— 异步功能库（叶子异步原语声明）
#
# 语言底层异步机制（future 类型、async/await/done 关键字、async_init/loop/final）
# 已内建于 op.sc（默认导入）+ op.h（默认带入每个 C 单元）；其运行时实现（含
# future/async_* 与 delay 的全部实现）亦属语言自有异步内核 builtins/op_impl.c
# （始终随工程编译链接，POSIX poll + 自管道 + pthread，不依赖 libuv）。
# 本模块只承载"异步功能库"部分的叶子原语声明（如 delay）。
#
# 用法：inc async.sc
#   - 引入 delay 等叶子原语的声明（实现在始终链接的 op_impl.c，无需额外链库）。
#   ★ 未 inc async.sc 却调用 delay → 编译期报错（缺声明）。
#
# ── 并发模型：与 run（多线程）相对的第二套 —— 单线程协作式异步 ──────────────────
#   run   把 rpc 当线程入口（抢占式、多线程，依赖 m 模块）
#   async 把 rpc 当协程（协作式、单线程事件循环 + 状态机）
#
# ── 可 await 契约 ──────────────────────────────────────────────────────────
#   future()               造未就绪 future（伪类构造，挂当前事件循环）
#   done f [, result]      标记就绪 + 唤醒 waiter —— ★ 跨线程安全 ★（谁都能调）
#   任何自定义异步原语（线程回调 / 中断 / 外部库）都在"完成时"用 done 接入。
#
# C ABI 契约：future/async_* 见 op.h；本模块只有声明，delay 的 C 实现在 builtins/op_impl.c。

# ---------------- delay：单调时钟定时器叶子异步原语（实现在 op_impl.c） ----------------
@fnc delay:: future&, ms: u4            # 启动 ms 毫秒定时器，立即返回 future

