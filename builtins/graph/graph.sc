# graph —— 有向图算法组件（sc 侧封装 builtins/graph 的 C 图算法）
#
# 本模块既被编译器（scc）在编译期引入（对 tok 依赖图 dep…map 做环检测/拓扑/深度等
# 预计算并烘焙为生成代码常量），也可作为独立子模块供 sc 程序 inc 直接调用：
#
#   inc graph.sc
#   ...
#   var eu[6]: i4 = [0,0,1,2,3,3]     # 各边起点
#   var ev[6]: i4 = [1,2,3,3,4,5]     # 各边终点（边 i：eu[i] -> ev[i]）
#   var g: graph
#   g.nv = 6   g.ne = 6   g.eu = eu   g.ev = ev
#   var order[6]: i4
#   if g.toposort(order)              # 拓扑排序（DAG 返回 1）
#       ...
#
# 图为不可变视图：调用方持有 eu/ev（并列边数组，顶点以 [0, nv) 整数编号），graph 只读不接管。
# 所有回填型出参由调用方预分配（通常长度 nv，环检测缓冲须 ≥ nv+1；任一出参可传 nil 表示不需要）。
#
# @def 定义纯数据布局（C ABI 契约的一部分，见同目录 graph.h）；方法声明（无函数体）为
# extern 原型，实现在 C 侧 graph_impl.c（编译器自动编译并链接）。
# 自定义实现：inc graph.sc 后按同目录 graph.h 契约提供替代 graph_impl。

@def graph: {
    nv: i4          # 顶点数（编号 0 .. nv-1）
    ne: i4          # 边数
    eu: const i4&   # 各边起点 [ne]
    ev: const i4&   # 各边终点 [ne]

    # ---- 拓扑结构 ----
    fnc cycle:: i4, cyc_out: i4&, cyc_len: i4&                       # 有向环检测（三色 DFS）：有环返回 1 并回填环路径(须 ≥nv+1)+*cyc_len，无环返回 0
    fnc toposort:: i4, order_out: i4&                               # 拓扑排序（Kahn）：DAG 返回 1 并回填 order_out[nv]，有环返回 0
    fnc revtoposort:: i4, order_out: i4&                            # 逆拓扑序（汇点在前）：DAG 返回 1 回填，有环返回 0
    fnc scc:: i4, comp_out: i4&, ncomp_out: i4&                     # 强连通分量（Tarjan）：回填 comp_out[v]=簇编号，*ncomp_out=簇数；返回簇数
    fnc components:: i4, comp_out: i4&, ncomp_out: i4&              # 弱连通分量（并查集，忽略方向）：回填分量编号+*ncomp_out；返回分量数

    # ---- 层次 / 关键路径 / 调度 ----
    fnc depth:: depth_out: i4&                                     # 最长路径深度分层（源=0）：回填 depth_out[nv]（有环退化全 0）
    fnc critical:: crit_out: i4&, slack_out: i4&                    # 关键路径 + 松弛：crit_out[v]=是否关键，slack_out[v]=松弛余量（可传 nil）
    fnc batches:: batch_out: i4&, width_out: i4&, nbatch_out: i4&   # 拓扑分批（并行波次）：batch_out[v]=波次(=depth)，width_out[v]=同波宽度，*nbatch_out=总波数（任一可 nil）

    # ---- 度量 / 影响面 ----
    fnc degree:: indeg_out: i4&, outdeg_out: i4&                    # 扇入/扇出度（枢纽识别）：任一出参可传 nil
    fnc reach:: reach_out: i4&                                     # 可达性规模：reach_out[v]=从 v 可达的其它顶点数（脏标记波及面）
    fnc dominators:: checkpoint_out: i4&, domsize_out: i4&          # 支配树：checkpoint_out[v]=是否支配咽喉，domsize_out[v]=支配子树规模（任一可 nil）

    # ---- 路径 / 生成树 ----
    fnc bfs:: src: i4, dist_out: i4&                               # 无权最短路（BFS 层）：dist_out[v]=src→v 最短跳数（不可达 -1，src 自身 0）
    fnc mst:: i4, ew: const i4&, tree_edges_out: i4&, total_out: i4&  # 最小生成树/森林（Kruskal，无向带权，ew 为空→全 1）：回填入选边下标 + *total_out=权和；返回入选边数
}
