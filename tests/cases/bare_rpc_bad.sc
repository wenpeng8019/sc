# 负向用例：rpc 是「流程」原语，禁止裸 rpc() 直接调用。
# 当前线程直接执行须经 `sync work(args)` 驱动；另有 async/run/队列 << 形态。
rpc add: i4, a: i4, b: i4
    return a + b

fnc main: i4
    return add(3, 4)
