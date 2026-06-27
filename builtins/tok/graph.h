#ifndef SC_BUILTINS_GRAPH_H
#define SC_BUILTINS_GRAPH_H
/* ============================================================
 * builtins/tok/graph —— 有向图算法（规范接口 / spec interface）
 * ============================================================
 * 图是 tok 机制的一部分（tok 依赖图 dep…map），故同居 tok 目录；但用途特殊：
 * 它**不随用户工程链接**，而是被编译器（scc）在编译期引入——对 tok 依赖图
 * （dep…map）做环检测 / 拓扑排序 / 深度分层 / 逆拓扑序等预计算，结果**烘焙**为
 * 生成代码中的隐藏常量（baked constants），运行时仅查表、不重算（类比游戏的
 * lightmap / navmesh 烘焙）。
 *
 * 本头是**编译期生产者（scc）与运行时消费者（tok）共享的同一份契约**，
 * 杜绝两端数据格式漂移。算法本体 AST 无关：顶点以 [0, nv) 整数编号，边以并列
 * 数组 (eu[i] -> ev[i]) 表达，可被任意场景复用（非 tok 专用）。
 *
 * 默认实现见同目录 graph_impl.c（编译进 scc 二进制，见 compiler/CMakeLists.txt）。
 * ============================================================ */
#ifdef __cplusplus
extern "C" {
#endif

/* 有向图（不可变视图）：调用方持有 eu/ev 存储，本模块只读不接管。 */
typedef struct sc_graph {
    int        nv;   /* 顶点数（编号 0 .. nv-1） */
    int        ne;   /* 边数 */
    const int *eu;   /* 各边起点 [ne] */
    const int *ev;   /* 各边终点 [ne] */
} sc_graph;

/* 有向环检测（三色 DFS）。
 *   有环 → 返回 1，回填 cyc_out 为环顶点序列（v -> … -> v，闭合首尾同顶点），
 *          长度写入 *cyc_len（缓冲须 ≥ nv + 1）；
 *   无环 → 返回 0，*cyc_len = 0。 */
int  sc_graph_cycle(const sc_graph *g, int *cyc_out, int *cyc_len);

/* 拓扑排序（Kahn 入度法）。
 *   DAG → 返回 1，order_out[nv] 为一个拓扑序（源在前）；
 *   有环 → 返回 0（order_out 内容未定义）。 */
int  sc_graph_toposort(const sc_graph *g, int *order_out);

/* 逆拓扑序 = 拓扑序反转（汇点在前）。反向传播（back）按此序回溯上游。
 *   DAG → 返回 1，order_out[nv] 回填；有环 → 返回 0。 */
int  sc_graph_revtoposort(const sc_graph *g, int *order_out);

/* 最长路径深度分层（源深度 = 0，多前驱取 max = 该顶点到任一源的最长距离）。
 *   DAG → 回填 depth_out[nv]；有环 → 全部置 0（退化）。
 *   语义：神经网络的「第几层」/ 流水线的「第几级」。 */
void sc_graph_depth(const sc_graph *g, int *depth_out);

/* 关键路径（最长链）+ 松弛分析（流水线瓶颈识别，无权）。
 *   正向最早深度 e[v]（= sc_graph_depth）+ 反向最长深度 rd[v]（v 到任一汇点的最长距离），
 *   关键路径总长 L = max(e[v]+rd[v])；某点在关键路径上 ⇔ e[v]+rd[v]==L（加长它即拖慢整条流水线）。
 *   回填 crit_out[v]=0/1（是否关键）；slack_out 非空时回填松弛余量 slack[v]=L-(e[v]+rd[v])（可深多少跳而不拖慢全局）。
 *   DAG → 回填；有环 → crit 全 0、slack 全 0（退化；环检测应已先行拦截）。 */
void sc_graph_critical(const sc_graph *g, int *crit_out, int *slack_out);

/* 扇入/扇出度（枢纽识别，hub）。统计每顶点的入边数 indeg（被多少上游依赖）与出边数 outdeg
 *   （驱动多少下游）。度数高者即依赖图枢纽（牵一发动全身的关键节点 / 广播源）。
 *   回填 indeg_out[v]、outdeg_out[v]（任一可为空表示不需要）。重边按重数计。 */
void sc_graph_degree(const sc_graph *g, int *indeg_out, int *outdeg_out);

/* 可达性 / 传递闭包规模（脏标记影响范围）。reach_out[v] = 从 v 出发可达的**其它**顶点数
 *   （不含 v 自身）——即 v 变更后须重算的下游 token 总数（脏标记波及面 / 失效爆炸半径）。
 *   逆拓扑序 + 位集传递闭包计数。DAG → 回填；有环 → 退化（按可达近似，仍终止）。 */
void sc_graph_reach(const sc_graph *g, int *reach_out);

/* 拓扑分批（antichain 并行波次，接 MT 调度）。按最长路径分层：同层顶点两两无路径（反链），
 *   可并行触发。batch_out[v] = v 的波次编号（= sc_graph_depth，源波=0）；width_out[v] = 与 v
 *   同波的顶点数（该波的并行宽度）；*nbatch_out 写入总波次数（= 最大深度 + 1）。任一出参可空。
 *   DAG → 回填；有环 → 退化全 0（环检测应已先行拦截）。 */
void sc_graph_batches(const sc_graph *g, int *batch_out, int *width_out, int *nbatch_out);

/* 支配树（检查点 / 缓存边界识别）。引入虚拟超级源汇连各入度 0 顶点后求支配关系：
 *   d 支配 n ⇔ 自源到 n 的每条路径都经过 d。支配者是流经其下游全部数据的**咽喉**——天然的
 *   检查点 / 缓存边界（在此缓存可覆盖其支配子树的全部重算）。
 *   checkpoint_out[v] = v 是否支配 ≥1 个其它顶点（1=咽喉检查点 / 0=非）；
 *   domsize_out[v] = v 在支配树中的后代数（其缓存可覆盖的下游规模）。任一出参可空。
 *   Cooper–Harvey–Kennedy 迭代求 idom。DAG → 回填；有环 → 按可达近似。 */
void sc_graph_dominators(const sc_graph *g, int *checkpoint_out, int *domsize_out);

/* 强连通分量（Tarjan）。受控反馈回路（dep loop）允许成环——编译期对 loop 边建图缩点，
 * 把多顶点 SCC（真正的反馈簇）识别出来，烘焙为每 token 的「簇编号 + 簇大小」常量，
 * 运行时据此驱动迭代收敛（lightmap 式预计算）。
 *   回填 comp_out[v] = 顶点 v 所属 SCC 编号（编号按逆拓扑序：汇簇在前）；
 *   *ncomp_out 写入 SCC 总数（非空时）；返回值同 *ncomp_out。
 *   单顶点且无自环 → 自成一簇（簇大小 1，非反馈）；簇大小 > 1（或含自环）= 反馈簇。 */
int  sc_graph_scc(const sc_graph *g, int *comp_out, int *ncomp_out);

#ifdef __cplusplus
}
#endif
#endif /* SC_BUILTINS_GRAPH_H */
