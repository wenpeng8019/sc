# graph 组件演示：构造一个小型 DAG，调用多种图算法并打印结果。
#
#   0 → 1 → 3 → 4
#   0 → 2 → 3 → 5
#
# 拓扑序、深度分层、关键路径、扇入扇出、可达面、BFS 最短路、
# 弱连通分量、最小生成树 —— 全部由 builtins/graph 的 C 实现（graph_impl.c）
# 经 inc 自动链接后驱动。

inc graph.sc

fnc main: i4
    var eu[6]: i4 = [0, 0, 1, 2, 3, 3]     # 各边起点
    var ev[6]: i4 = [1, 2, 3, 3, 4, 5]     # 各边终点

    var g: graph
    g.nv = 6
    g.ne = 6
    g.eu = eu
    g.ev = ev

    ::printf("graph: %d verts, %d edges\n", g.nv, g.ne)

    # 拓扑排序
    var order[6]: i4
    if g.toposort(order)
        ::printf("toposort:")
        var i: i4 = 0
        for i = 0; i < g.nv; i++
            ::printf(" %d", order[i])
        ::printf("\n")
    else
        ::printf("toposort: has cycle!\n")

    # 深度分层（最长路径）
    var depth[6]: i4
    g.depth(depth)
    ::printf("depth:   ")
    var i: i4 = 0
    for i = 0; i < g.nv; i++
        ::printf(" v%d=%d", i, depth[i])
    ::printf("\n")

    # 关键路径
    var crit[6]: i4
    var slack[6]: i4
    g.critical(crit, slack)
    ::printf("critical:")
    for i = 0; i < g.nv; i++
        if crit[i]
            ::printf(" v%d", i)
    ::printf("\n")

    # 扇入 / 扇出
    var indeg[6]: i4
    var outdeg[6]: i4
    g.degree(indeg, outdeg)
    ::printf("degree:  ")
    for i = 0; i < g.nv; i++
        ::printf(" v%d(in=%d,out=%d)", i, indeg[i], outdeg[i])
    ::printf("\n")

    # 可达面：从各点可达的其它顶点数
    var reach[6]: i4
    g.reach(reach)
    ::printf("reach:   ")
    for i = 0; i < g.nv; i++
        ::printf(" v%d=%d", i, reach[i])
    ::printf("\n")

    # BFS 无权最短路（从 0 出发）
    var dist[6]: i4
    g.bfs(0, dist)
    ::printf("bfs(0):  ")
    for i = 0; i < g.nv; i++
        ::printf(" v%d=%d", i, dist[i])
    ::printf("\n")

    # 弱连通分量
    var comp[6]: i4
    var ncomp: i4 = 0
    var nc: i4 = g.components(comp, &ncomp)
    ::printf("components: %d\n", nc)

    # 最小生成树（无向，边权=下标+1 做区分）
    var ew[6]: i4 = [1, 2, 3, 4, 5, 6]
    var tree[6]: i4
    var total: i4 = 0
    var nt: i4 = g.mst(ew, tree, &total)
    ::printf("mst: %d edges, total weight = %d\n", nt, total)

    return 0
