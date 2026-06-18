# async —— 异步功能库（libuv 叶子原语 + 默认运行时实现）
#
# 语言底层异步机制（future 类型、async/await/done 关键字、async_init/loop/final）
# 已内建于 op.sc（默认导入）+ op.h（默认带入每个 C 单元）。本模块只承载"异步功能
# 库"部分：基于 libuv 的叶子异步原语（如 delay），以及这些底层机制的默认运行时实现
# （见 async_impl.c —— async_init/loop/final/future_* 全部在此实现）。
#
# 用法：inc async.sc
#   - 引入 delay 等叶子原语；
#   - 触发链接 libuv 运行时（提供 future/async_* 的实现）。
#   ★ 用到 async/await/done/future/async_init 等却未 inc async.sc → 链接错误。
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
# C ABI 契约：future/async_* 见 op.h；本模块 C 实现见同目录 async_impl.c（libuv）。

# ---------------- delay：libuv 默认实现的叶子异步原语（uv_timer） ----------------
@fnc delay:: future&, ms: u4            # 启动 ms 毫秒定时器，立即返回 future

