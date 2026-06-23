# 运行时守卫触发用例：自动指针 T@ 析构后仍持出边 → 报「未清理」
# 期望：node 定义了 drop，但其 drop 故意不清理 child 出边；root 归零（in→0）
# 触发 drop 后 out 仍为 1 → sc_ref_check 报告「未清理」，不释放（非致命）。
# 比对 golden .trap（程序 stderr）。

@def node: {
    v: i4
    child: node@

    # 故意不清理 child：析构后出边残留，应触发「未清理」报告
    drop: fnc
        var keep: i4 = this->v
}

@fnc main: i4
    var root: node@ = node()
    root->v = 1
    root->child = node()           # 成员出边：root.out=1
    root->child->v = 2
    return 0                        # root 归零 → drop 不清 child → out=1 → 未清理
