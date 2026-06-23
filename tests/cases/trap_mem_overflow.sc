# 运行时守卫触发用例：--check=mem 栈数组缓冲区上溢（尾哨兵）
# 期望：超出逻辑维度的写入撞坏尾哨兵，退域校验时检出并报「尾哨兵被破坏」。
# 非致命（报告 stderr，不 abort）。比对 golden .trap（程序 stderr）。

@fnc main: i4
    var buf[4]: u1              # 4 字节逻辑容量；--check=mem 超额分配尾哨兵
    var i: i4 = 0
    while i < 8                 # 写 8 字节 → buf[4..7] 撞尾哨兵
        buf[i] = 1
        i = i + 1
    return 0                    # 退域校验尾哨兵 → 检出缓冲区上溢
