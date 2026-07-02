/* ============================================================
 * builtins/graph/graph_impl.c —— 有向图算法默认实现
 * ============================================================
 * 规范接口见同目录 graph.h。本实现编译进 scc 二进制（compiler/CMakeLists.txt），
 * 由编译期适配层调用，对 tok 依赖图做预计算并烘焙为生成代码常量。
 * 纯 C、AST 无关、无全局状态：所有工作缓冲在函数内 malloc/free。
 * ============================================================ */
#include "graph.h"
#include <stdlib.h>

/* 由边表 (eu, ev) 构建 CSR 出边邻接：
 *   head[nv+1] 前缀和（顶点 u 的出边落在 to[head[u] .. head[u+1])）、to[ne] 终点。 */
static void build_csr(sc_graph *g, int *head, int *to) {
    const int nv = g->nv, ne = g->ne;
    for (int i = 0; i <= nv; i++) head[i] = 0;
    for (int e = 0; e < ne; e++) head[g->eu[e] + 1]++;
    for (int i = 0; i < nv; i++) head[i + 1] += head[i];
    int *cur = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    for (int i = 0; i < nv; i++) cur[i] = head[i];
    for (int e = 0; e < ne; e++) to[cur[g->eu[e]]++] = g->ev[e];
    free(cur);
}

/* ---- 环检测：三色 DFS（递归），回边即回填环路径 ---- */
typedef struct {
    const int *head, *to;
    int *state;   /* 0=未访问 / 1=栈上 / 2=完成 */
    int *stk, sp; /* DFS 顶点栈 */
    int *cyc, cyclen, found;
} cyc_ctx;

static void cyc_dfs(cyc_ctx *c, int u) {
    c->state[u] = 1;
    c->stk[c->sp++] = u;
    for (int i = c->head[u]; i < c->head[u + 1] && !c->found; i++) {
        int v = c->to[i];
        if (c->state[v] == 0) {
            cyc_dfs(c, v);
        } else if (c->state[v] == 1) {       /* 回边 → 栈中 v..栈顶 即环，补回 v 闭合 */
            int start = c->sp - 1;
            while (start >= 0 && c->stk[start] != v) start--;
            int k = 0;
            for (int j = start; j < c->sp; j++) c->cyc[k++] = c->stk[j];
            c->cyc[k++] = v;
            c->cyclen = k;
            c->found = 1;
        }
    }
    c->sp--;
    c->state[u] = 2;
}

int sc_graph_cycle(sc_graph *g, int *cyc_out, int *cyc_len) {
    const int nv = g->nv, ne = g->ne;
    int *head = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to   = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);

    cyc_ctx c;
    c.head = head; c.to = to;
    c.state = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));
    c.stk   = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    c.sp = 0; c.cyc = cyc_out; c.cyclen = 0; c.found = 0;

    for (int i = 0; i < nv && !c.found; i++)
        if (c.state[i] == 0) cyc_dfs(&c, i);

    if (cyc_len) *cyc_len = c.cyclen;
    int r = c.found;
    free(head); free(to); free(c.state); free(c.stk);
    return r;
}

/* ---- 拓扑排序：Kahn 入度法 ---- */
int sc_graph_toposort(sc_graph *g, int *order_out) {
    const int nv = g->nv, ne = g->ne;
    int *head  = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to    = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);
    int *indeg = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));
    for (int e = 0; e < ne; e++) indeg[g->ev[e]]++;

    int *q = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    int qh = 0, qt = 0;
    for (int i = 0; i < nv; i++) if (indeg[i] == 0) q[qt++] = i;
    int cnt = 0;
    while (qh < qt) {
        int u = q[qh++];
        order_out[cnt++] = u;
        for (int i = head[u]; i < head[u + 1]; i++)
            if (--indeg[to[i]] == 0) q[qt++] = to[i];
    }
    free(head); free(to); free(indeg); free(q);
    return cnt == nv;                         /* 全部出队 = 无环 */
}

int sc_graph_revtoposort(sc_graph *g, int *order_out) {
    if (!sc_graph_toposort(g, order_out)) return 0;
    for (int i = 0, j = g->nv - 1; i < j; i++, j--) {
        int t = order_out[i]; order_out[i] = order_out[j]; order_out[j] = t;
    }
    return 1;
}

/* ---- 最长路径深度分层：沿拓扑序松弛 depth[v] = max(depth[u]+1) ---- */
void sc_graph_depth(sc_graph *g, int *depth_out) {
    const int nv = g->nv, ne = g->ne;
    int *order = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    for (int i = 0; i < nv; i++) depth_out[i] = 0;
    if (!sc_graph_toposort(g, order)) {       /* 有环：退化全 0（环检测应已先行拦截） */
        free(order);
        return;
    }
    int *head = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to   = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);
    for (int oi = 0; oi < nv; oi++) {
        int u = order[oi];
        for (int i = head[u]; i < head[u + 1]; i++)
            if (depth_out[u] + 1 > depth_out[to[i]]) depth_out[to[i]] = depth_out[u] + 1;
    }
    free(order); free(head); free(to);
}

/* ---- 关键路径 + 松弛：正向最早 e[v] + 反向最长 rd[v]， crit ⇔ e+rd==L，slack=L-(e+rd) ---- */
void sc_graph_critical(sc_graph *g, int *crit_out, int *slack_out) {
    const int nv = g->nv, ne = g->ne;
    for (int i = 0; i < nv; i++) { crit_out[i] = 0; if (slack_out) slack_out[i] = 0; }
    int *order = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    if (!sc_graph_toposort(g, order)) { free(order); return; }  /* 有环：退化（应已被环检测拦截） */
    int *head = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to   = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);
    int *e  = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));   /* 正向最早深度（源=0） */
    int *rd = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));   /* 反向最长深度（汇=0） */
    for (int oi = 0; oi < nv; oi++) {                  /* 拓扑序正松弛：e[w]=max(e[u]+1) */
        int u = order[oi];
        for (int i = head[u]; i < head[u + 1]; i++)
            if (e[u] + 1 > e[to[i]]) e[to[i]] = e[u] + 1;
    }
    for (int oi = nv - 1; oi >= 0; oi--) {             /* 逆拓扑序反松弛：rd[u]=max(rd[w]+1) */
        int u = order[oi];
        for (int i = head[u]; i < head[u + 1]; i++)
            if (rd[to[i]] + 1 > rd[u]) rd[u] = rd[to[i]] + 1;
    }
    int L = 0;
    for (int v = 0; v < nv; v++) if (e[v] + rd[v] > L) L = e[v] + rd[v];
    for (int v = 0; v < nv; v++) {
        int s = L - (e[v] + rd[v]);
        if (slack_out) slack_out[v] = s;
        crit_out[v] = (s == 0) ? 1 : 0;
    }
    free(order); free(head); free(to); free(e); free(rd);
}

/* ---- 扇入/扇出度：直接按边表计数 ---- */
void sc_graph_degree(sc_graph *g, int *indeg_out, int *outdeg_out) {
    const int nv = g->nv, ne = g->ne;
    if (indeg_out)  for (int i = 0; i < nv; i++) indeg_out[i]  = 0;
    if (outdeg_out) for (int i = 0; i < nv; i++) outdeg_out[i] = 0;
    for (int e = 0; e < ne; e++) {
        if (outdeg_out) outdeg_out[g->eu[e]]++;
        if (indeg_out)  indeg_out[g->ev[e]]++;
    }
}

/* ---- 可达性规模：逆拓扑序 + 位集传递闭包，reach[v]=可达其它顶点数 ---- */
void sc_graph_reach(sc_graph *g, int *reach_out) {
    const int nv = g->nv, ne = g->ne;
    for (int i = 0; i < nv; i++) reach_out[i] = 0;
    if (nv == 0) return;
    int *order = (int *)malloc((size_t)nv * sizeof(int));
    int dag = sc_graph_toposort(g, order);
    if (!dag) for (int i = 0; i < nv; i++) order[i] = i;  /* 有环：退化按编号序近似 */
    int *head = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to   = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);
    const int W = (nv + 31) / 32;                 /* 每顶点一行位集（可达集，含自身） */
    unsigned *reach = (unsigned *)calloc((size_t)nv * (size_t)W, sizeof(unsigned));
    for (int oi = nv - 1; oi >= 0; oi--) {        /* 逆拓扑序：先汇后源，子集已就绪 */
        int u = order[oi];
        unsigned *ru = reach + (size_t)u * W;
        ru[u >> 5] |= 1u << (u & 31);             /* 含自身 */
        for (int i = head[u]; i < head[u + 1]; i++) {
            unsigned *rv = reach + (size_t)to[i] * W;
            for (int w = 0; w < W; w++) ru[w] |= rv[w];   /* 并入后继的可达集 */
        }
    }
    for (int v = 0; v < nv; v++) {
        unsigned *rv = reach + (size_t)v * W;
        int cnt = 0;
        for (int w = 0; w < W; w++) {
            unsigned x = rv[w];
            while (x) { x &= x - 1; cnt++; }       /* popcount */
        }
        reach_out[v] = cnt - 1;                    /* 去掉自身 */
    }
    free(order); free(head); free(to); free(reach);
}

/* ---- 拓扑分批：batch=最长路径深度，width=同深度顶点数 ---- */
void sc_graph_batches(sc_graph *g, int *batch_out, int *width_out, int *nbatch_out) {
    const int nv = g->nv;
    int *depth = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    sc_graph_depth(g, depth);                      /* 复用最长路径分层（有环退化全 0） */
    int maxd = -1;
    for (int v = 0; v < nv; v++) if (depth[v] > maxd) maxd = depth[v];
    if (nbatch_out) *nbatch_out = (nv == 0) ? 0 : maxd + 1;
    if (batch_out) for (int v = 0; v < nv; v++) batch_out[v] = depth[v];
    if (width_out) {
        int nb = maxd + 1;
        int *cnt = (int *)calloc((size_t)(nb > 0 ? nb : 1), sizeof(int));
        for (int v = 0; v < nv; v++) cnt[depth[v]]++;
        for (int v = 0; v < nv; v++) width_out[v] = cnt[depth[v]];
        free(cnt);
    }
    free(depth);
}

/* ---- 支配树（Cooper–Harvey–Kennedy）：虚拟超级源 S 连各入度 0 顶点，求 idom ----
 * checkpoint[v]=是否支配≥1 其它顶点；domsize[v]=支配树后代数。 */
void sc_graph_dominators(sc_graph *g, int *checkpoint_out, int *domsize_out) {
    const int nv = g->nv, ne = g->ne;
    if (checkpoint_out) for (int i = 0; i < nv; i++) checkpoint_out[i] = 0;
    if (domsize_out)    for (int i = 0; i < nv; i++) domsize_out[i] = 0;
    if (nv == 0) return;
    const int N = nv + 1, S = nv;                 /* 顶点 0..nv-1 + 虚拟超级源 S=nv */
    /* 反向邻接（前驱）CSR：含 S→各入度 0 顶点。先算入度。 */
    int *indeg = (int *)calloc((size_t)nv, sizeof(int));
    for (int e = 0; e < ne; e++) indeg[g->ev[e]]++;
    int nse = 0;                                   /* S 出边数 = 入度 0 顶点数 */
    for (int v = 0; v < nv; v++) if (indeg[v] == 0) nse++;
    const int PE = ne + nse;                       /* 前驱边总数 */
    int *phead = (int *)calloc((size_t)(N + 1), sizeof(int));
    int *pto   = (int *)malloc((size_t)(PE ? PE : 1) * sizeof(int));
    for (int e = 0; e < ne; e++) phead[g->ev[e] + 1]++;   /* pred(ev) 含 eu */
    for (int v = 0; v < nv; v++) if (indeg[v] == 0) phead[v + 1]++;  /* pred(v) 含 S */
    for (int i = 0; i < N; i++) phead[i + 1] += phead[i];
    int *pcur = (int *)malloc((size_t)N * sizeof(int));
    for (int i = 0; i < N; i++) pcur[i] = phead[i];
    for (int e = 0; e < ne; e++) pto[pcur[g->ev[e]]++] = g->eu[e];
    for (int v = 0; v < nv; v++) if (indeg[v] == 0) pto[pcur[v]++] = S;
    /* 正向邻接（含 S）用于 DFS 求后序 */
    int *fhead = (int *)calloc((size_t)(N + 1), sizeof(int));
    int *fto   = (int *)malloc((size_t)(PE ? PE : 1) * sizeof(int));
    for (int e = 0; e < ne; e++) fhead[g->eu[e] + 1]++;
    for (int v = 0; v < nv; v++) if (indeg[v] == 0) fhead[S + 1]++;
    for (int i = 0; i < N; i++) fhead[i + 1] += fhead[i];
    int *fcur = (int *)malloc((size_t)N * sizeof(int));
    for (int i = 0; i < N; i++) fcur[i] = fhead[i];
    for (int e = 0; e < ne; e++) fto[fcur[g->eu[e]]++] = g->ev[e];
    for (int v = 0; v < nv; v++) if (indeg[v] == 0) fto[fcur[S]++] = v;
    /* 迭代 DFS 求后序编号 postnum（reverse-postorder = 后序逆序） */
    int *postnum = (int *)malloc((size_t)N * sizeof(int));
    for (int i = 0; i < N; i++) postnum[i] = -1;
    int *stk = (int *)malloc((size_t)N * sizeof(int));
    int *it  = (int *)malloc((size_t)N * sizeof(int));
    int *vis = (int *)calloc((size_t)N, sizeof(int));
    int sp = 0, pcnt = 0;
    stk[sp] = S; it[sp] = fhead[S]; vis[S] = 1; sp++;
    while (sp > 0) {
        int u = stk[sp - 1];
        if (it[sp - 1] < fhead[u + 1]) {
            int v = fto[it[sp - 1]++];
            if (!vis[v]) { vis[v] = 1; stk[sp] = v; it[sp] = fhead[v]; sp++; }
        } else {
            postnum[u] = pcnt++;                   /* 出栈即后序 */
            sp--;
        }
    }
    int *rpo = (int *)malloc((size_t)N * sizeof(int));   /* RPO：后序逆序（postnum→顶点） */
    for (int u = 0; u < N; u++) if (postnum[u] >= 0) rpo[postnum[u]] = u;
    int *idom = (int *)malloc((size_t)N * sizeof(int));
    for (int i = 0; i < N; i++) idom[i] = -1;
    idom[S] = S;                                   /* 超级源支配自身（迭代不动点起点） */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int k = N - 1; k >= 0; k--) {         /* RPO：后序逆序 */
            int b = rpo[k];
            if (b == S || postnum[b] < 0) continue;
            int newidom = -1;
            for (int i = phead[b]; i < phead[b + 1]; i++) {
                int p = pto[i];
                if (idom[p] < 0) continue;         /* 前驱尚未处理 */
                if (newidom < 0) { newidom = p; continue; }
                int a = p, c = newidom;            /* intersect（后序号小者沿 idom 上爬） */
                while (a != c) {
                    while (postnum[a] < postnum[c]) a = idom[a];
                    while (postnum[c] < postnum[a]) c = idom[c];
                }
                newidom = a;
            }
            if (newidom >= 0 && idom[b] != newidom) { idom[b] = newidom; changed = 1; }
        }
    }
    /* domsize：每顶点沿 idom 链上爬，途经祖先（排除自身与 S）后代计数 +1 */
    for (int v = 0; v < nv; v++) {
        int a = idom[v];
        while (a >= 0 && a != S) {
            if (domsize_out) domsize_out[a]++;
            if (a == idom[a]) break;
            a = idom[a];
        }
    }
    if (checkpoint_out)
        for (int v = 0; v < nv; v++) {
            int sz = 0, a;                          /* 重算一次后代数判定（避免依赖 domsize_out 非空） */
            for (int u = 0; u < nv; u++) {
                a = idom[u];
                while (a >= 0 && a != S) { if (a == v) { sz++; break; } if (a == idom[a]) break; a = idom[a]; }
            }
            checkpoint_out[v] = sz > 0 ? 1 : 0;
        }
    free(indeg); free(phead); free(pto); free(pcur);
    free(fhead); free(fto); free(fcur);
    free(postnum); free(rpo); free(stk); free(it); free(vis); free(idom);
}

/* ---- 强连通分量：Tarjan（递归）。低链回溯，根顶点弹栈成簇 ---- */
typedef struct {
    const int *head, *to;
    int *index, *low, *onstk;   /* DFS 发现序 / 低链值 / 是否在 Tarjan 栈 */
    int *stk, sp;               /* Tarjan 顶点栈 */
    int *comp;                  /* 输出：顶点 → SCC 编号 */
    int  idx, ncomp;            /* 全局 DFS 计数 / 已封顶 SCC 数 */
} scc_ctx;

static void scc_dfs(scc_ctx *c, int u) {
    c->index[u] = c->low[u] = c->idx++;
    c->stk[c->sp++] = u;
    c->onstk[u] = 1;
    for (int i = c->head[u]; i < c->head[u + 1]; i++) {
        int v = c->to[i];
        if (c->index[v] < 0) {                /* 未访问 → 递归，回传低链 */
            scc_dfs(c, v);
            if (c->low[v] < c->low[u]) c->low[u] = c->low[v];
        } else if (c->onstk[v]) {             /* 在栈中（回边/横叉到活跃簇）→ 用其发现序压低 */
            if (c->index[v] < c->low[u]) c->low[u] = c->index[v];
        }
    }
    if (c->low[u] == c->index[u]) {           /* u 为 SCC 根 → 弹出栈顶直到 u，整段同簇 */
        for (;;) {
            int w = c->stk[--c->sp];
            c->onstk[w] = 0;
            c->comp[w] = c->ncomp;
            if (w == u) break;
        }
        c->ncomp++;
    }
}

int sc_graph_scc(sc_graph *g, int *comp_out, int *ncomp_out) {
    const int nv = g->nv, ne = g->ne;
    int *head = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to   = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);

    scc_ctx c;
    c.head = head; c.to = to;
    c.index = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    c.low   = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    c.onstk = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));
    c.stk   = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    c.comp  = comp_out;
    c.sp = 0; c.idx = 0; c.ncomp = 0;
    for (int i = 0; i < nv; i++) c.index[i] = c.low[i] = -1;
    for (int i = 0; i < nv; i++)
        if (c.index[i] < 0) scc_dfs(&c, i);

    free(head); free(to); free(c.index); free(c.low); free(c.onstk); free(c.stk);
    if (ncomp_out) *ncomp_out = c.ncomp;
    return c.ncomp;
}

/* ---- 并查集（Union-Find，路径减半压缩 + 按秩合并）：连通分量 / MST 共用 ---- */
static int uf_find(int *parent, int x) {
    while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
    return x;
}
static int uf_union(int *parent, int *urank, int a, int b) {
    int ra = uf_find(parent, a), rb = uf_find(parent, b);
    if (ra == rb) return 0;                     /* 已同集，合并无效（成环） */
    if (urank[ra] < urank[rb]) { int t = ra; ra = rb; rb = t; }
    parent[rb] = ra;
    if (urank[ra] == urank[rb]) urank[ra]++;
    return 1;
}

/* ---- 无权最短路：BFS 层（自 src 按出边方向广度优先） ---- */
void sc_graph_bfs(sc_graph *g, int src, int *dist_out) {
    const int nv = g->nv, ne = g->ne;
    for (int i = 0; i < nv; i++) dist_out[i] = -1;
    if (nv == 0 || src < 0 || src >= nv) return;
    int *head = (int *)malloc((size_t)(nv + 1) * sizeof(int));
    int *to   = (int *)malloc((size_t)(ne ? ne : 1) * sizeof(int));
    build_csr(g, head, to);
    int *q = (int *)malloc((size_t)nv * sizeof(int));
    int qh = 0, qt = 0;
    dist_out[src] = 0; q[qt++] = src;
    while (qh < qt) {
        int u = q[qh++];
        for (int i = head[u]; i < head[u + 1]; i++) {
            int v = to[i];
            if (dist_out[v] < 0) { dist_out[v] = dist_out[u] + 1; q[qt++] = v; }
        }
    }
    free(head); free(to); free(q);
}

/* ---- 弱连通分量：并查集（忽略边方向，按首次出现序紧凑编号） ---- */
int sc_graph_components(sc_graph *g, int *comp_out, int *ncomp_out) {
    const int nv = g->nv, ne = g->ne;
    int *parent = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    int *urank  = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));
    for (int i = 0; i < nv; i++) parent[i] = i;
    for (int e = 0; e < ne; e++) uf_union(parent, urank, g->eu[e], g->ev[e]);
    int *label = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    for (int i = 0; i < nv; i++) label[i] = -1;
    int nc = 0;
    for (int i = 0; i < nv; i++) {
        int r = uf_find(parent, i);
        if (label[r] < 0) label[r] = nc++;
        comp_out[i] = label[r];
    }
    free(parent); free(urank); free(label);
    if (ncomp_out) *ncomp_out = nc;
    return nc;
}

/* ---- 最小生成树/森林：Kruskal（(权,边) 对按权升序 qsort + 并查集去环） ---- */
typedef struct { int w, e; } we_pair;
static int we_cmp(const void *a, const void *b) {
    const we_pair *x = (const we_pair *)a, *y = (const we_pair *)b;
    if (x->w != y->w) return x->w < y->w ? -1 : 1;
    return x->e - y->e;                         /* 权同 → 按边下标稳定定序 */
}
int sc_graph_mst(sc_graph *g, const int *ew, int *tree_edges_out, int *total_out) {
    const int nv = g->nv, ne = g->ne;
    we_pair *arr = (we_pair *)malloc((size_t)(ne ? ne : 1) * sizeof(we_pair));
    for (int e = 0; e < ne; e++) { arr[e].w = ew ? ew[e] : 1; arr[e].e = e; }
    qsort(arr, (size_t)ne, sizeof(we_pair), we_cmp);
    int *parent = (int *)malloc((size_t)(nv ? nv : 1) * sizeof(int));
    int *urank  = (int *)calloc((size_t)(nv ? nv : 1), sizeof(int));
    for (int i = 0; i < nv; i++) parent[i] = i;
    int cnt = 0, total = 0;
    for (int k = 0; k < ne && cnt < nv - 1; k++) {
        int e = arr[k].e;
        if (uf_union(parent, urank, g->eu[e], g->ev[e])) {   /* 不成环 → 入选 */
            if (tree_edges_out) tree_edges_out[cnt] = e;
            total += arr[k].w;
            cnt++;
        }
    }
    free(arr); free(parent); free(urank);
    if (total_out) *total_out = total;
    return cnt;
}
