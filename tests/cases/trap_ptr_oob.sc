# 运行时守卫触发用例：--check=ptr 已知维度栈数组下标越界
# 期望：运行期拦截并 abort（致命），stderr 打印「数组下标越界」带下标/长度。
# 比对 golden .trap（程序 stderr）。

@fnc main: i4
    var a[3]: i4 = [10, 20, 30]
    var i: i4 = 5
    printf("%d\n", a[i])        # a[5]：--check=ptr 注入越界校验，命中即 abort
    return 0
